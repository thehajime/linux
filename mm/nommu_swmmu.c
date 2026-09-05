#include <linux/nommu_swmmu.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <linux/mman.h>

#include "internal.h"

#define SWMMU_VA_BASE ((uintptr_t)0x1000000000ULL)

struct nommu_swmmu_clone_item {
	struct list_head node;
	struct vm_area_struct *src_vma;
	struct vm_area_struct *dst_vma;
	struct nommu_swmmu_vma *new_data;
	bool published;
};

static inline void *default_kzalloc(size_t size, gfp_t gfp)
{
	return kzalloc(size, gfp);
}

static inline void default_kfree(void *ptr)
{
	kfree(ptr);
}

static inline struct page *default_alloc_page(gfp_t gfp)
{
	return alloc_page(gfp);
}

static inline void default_free_page(struct page *page)
{
	__free_page(page);
}

static inline int default_copy_page(struct page *dst,
				const struct page *src)
{
	copy_highpage(dst, (struct page *)src);
	return 0;
}

static const struct nommu_swmmu_mem_ops default_ops = {
	.zalloc = default_kzalloc,
	.dealloc = default_kfree,
	.page_alloc = default_alloc_page,
	.page_free = default_free_page,
	.page_copy = default_copy_page,
};

static size_t page_align(size_t size)
{
	return (size + SWMMU_PAGE_SIZE - 1) &
	       ~(SWMMU_PAGE_SIZE - 1);
}

/* helper functions for maple tree */
static struct nommu_swmmu_backing *swmmu_backing_alloc(struct nommu_swmmu_space *space,
						unsigned long length)
{
	struct nommu_swmmu_backing *backing;
	size_t i;
	size_t allocated = 0;

	length = PAGE_ALIGN(length);
	if (!length)
		return NULL;

	backing = space->ops->zalloc(sizeof(*backing), GFP_KERNEL);
	if (!backing)
		return NULL;

	refcount_set(&backing->refs, 1);
	backing->page_count = length / SWMMU_PAGE_SIZE;

	backing->pages = space->ops->zalloc(
		backing->page_count * sizeof(*backing->pages),
		GFP_KERNEL);
	if (!backing->pages) {
		space->ops->dealloc(backing);
		return NULL;
	}

	for (i = 0; i < backing->page_count; i++) {
		backing->pages[i] = space->ops->page_alloc(GFP_KERNEL);
		if (!backing->pages[i])
			goto fail_pages;

		clear_highpage(backing->pages[i]);
		allocated++;
	}

	return backing;

fail_pages:
	while (allocated)
		space->ops->page_free(backing->pages[--allocated]);

	space->ops->dealloc(backing->pages);
	space->ops->dealloc(backing);
	return NULL;
}

/* XXX: correct only for whole-backing ownership */
static void swmmu_backing_release(struct nommu_swmmu_space *space,
				struct nommu_swmmu_backing *backing)
{
	size_t i;

	if (!space || !backing)
		return;

	if (!refcount_dec_and_test(&backing->refs))
		return;

	for (i = 0; i < backing->page_count; i++)
		space->ops->page_free(backing->pages[i]);

	space->ops->dealloc(backing->pages);
	space->ops->dealloc(backing);
}

static struct nommu_swmmu_vma *swmmu_vma_data_create(struct nommu_swmmu_space *space,
						struct nommu_swmmu_backing *backing)
{
	struct nommu_swmmu_vma *vma_data;

	vma_data = space->ops->zalloc(sizeof(*vma_data), GFP_KERNEL);
	if (!vma_data)
		return NULL;

	vma_data->backing = backing;
	vma_data->page_offset = 0;
	vma_data->page_count = backing->page_count;
	vma_data->access = NOMMU_SWMMU_NONE;

	return vma_data;
}

static void swmmu_vma_data_free(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma *data)
{
	if (!space || !data)
		return;

	space->ops->dealloc(data);
}

static void swmmu_vma_data_release(struct nommu_swmmu_space *space,
				   struct nommu_swmmu_vma *data)
{
	struct nommu_swmmu_backing *backing;

	if (!data)
		return;

	backing = data->backing;

	if (backing)
		swmmu_backing_release(space, backing);

	swmmu_vma_data_free(space, data);
}

static struct nommu_swmmu_vma *swmmu_vma_data_dup(struct nommu_swmmu_space *dst_space,
						const struct nommu_swmmu_vma *src)
{
	struct nommu_swmmu_vma *dst;
	size_t i;
	int ret;

	if (!dst_space || !src || !src->backing ||
		src->page_offset > src->backing->page_count ||
		src->page_count >
		src->backing->page_count - src->page_offset)
		return NULL;

	dst = dst_space->ops->zalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->backing = swmmu_backing_alloc(dst_space,
					   src->page_count *
					   SWMMU_PAGE_SIZE);
	if (!dst->backing) {
		dst_space->ops->dealloc(dst);
		return NULL;
	}

	dst->page_offset = 0;
	dst->page_count = src->page_count;
	dst->access = src->access;

	for (i = 0; i < src->page_count; i++) {
		ret = dst_space->ops->page_copy(
			dst->backing->pages[i],
			src->backing->pages[src->page_offset + i]);
		if (ret) {
			swmmu_vma_data_release(dst_space, dst);
			return NULL;
		}
	}

	return dst;
}

static void nommu_swmmu_clear_child_vmas(struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	for_each_vma(vmi, vma)
		vma->vm_swmmu_data = NULL;
}

void nommu_swmmu_vma_close(struct mm_struct *mm,
			   struct vm_area_struct *vma)
{
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_vma *data;

	if (!mm || !vma)
		return;

	data = vma->vm_swmmu_data;
	if (!data)
		return;

	space = mm->swmmu_space;

	vma->vm_swmmu_data = NULL;
	swmmu_vma_data_release(space, data);
}

static void nommu_swmmu_clone_item_abort(struct mm_struct *dst,
					struct nommu_swmmu_space *dst_space,
					struct nommu_swmmu_clone_item *item)
{
	struct vm_area_struct *removed;
	VMA_ITERATOR(vmi, dst, 0);

	if (!item->published) {
		swmmu_vma_data_release(dst_space, item->new_data);
		item->new_data = NULL;
		vm_area_free(item->dst_vma);
		return;
	}

	vma_iter_set(&vmi, item->dst_vma->vm_start);
	removed = mas_erase(&vmi.mas);

	if (WARN_ON_ONCE(removed != item->dst_vma))
		return;

	nommu_swmmu_vma_close(dst, item->dst_vma);
	dst->map_count--;
	vm_area_free(item->dst_vma);
}

static int swmmu_find_free_range(struct mm_struct *mm,
				unsigned long min,
				size_t length,
				unsigned long *start)
{
	VMA_ITERATOR(vmi, mm, 0);
	int ret;

	mmap_assert_write_locked(mm);

	if (!length || length > ULONG_MAX - min)
		return -EINVAL;

	if (min >= TASK_SIZE ||
		length > TASK_SIZE - min)
		return -ENOMEM;

	ret = vma_iter_area_lowest(&vmi, min, TASK_SIZE, length);
	if (ret)
		return ret;

	*start = vma_iter_addr(&vmi);
	return 0;
}

/*
 * Create a space using @mem_ops.
 * The operation table must remain alive for the lifetime of the space.
 */
static struct nommu_swmmu_space *
__nommu_swmmu_space_create(
	const struct nommu_swmmu_mem_ops *mem_ops)
{
	struct nommu_swmmu_space *space;

	if (!mem_ops)
		return NULL;

	space = mem_ops->zalloc(sizeof(*space), GFP_KERNEL);
	if (!space)
		return NULL;

	space->ops = mem_ops;
	refcount_set(&space->users, 1);
	init_rwsem(&space->lock);
	space->mm = NULL;

	return space;
}

/* for future extensions */
const __weak struct nommu_swmmu_mem_ops *nommu_swmmu_arch_mem_ops(void)
{
	return NULL;
}

struct nommu_swmmu_space *nommu_swmmu_space_create(void)
{
	const struct nommu_swmmu_mem_ops *mem_ops;

	mem_ops = nommu_swmmu_arch_mem_ops();
	if (!mem_ops)
		mem_ops = &default_ops;

	return __nommu_swmmu_space_create(mem_ops);
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
struct nommu_swmmu_space *
nommu_swmmu_space_create_with_ops(
	const struct nommu_swmmu_mem_ops *mem_ops)
{
	return __nommu_swmmu_space_create(mem_ops);
}
#endif

static void swmmu_space_destroy_final(struct nommu_swmmu_space *space)
{
	const struct nommu_swmmu_mem_ops *mem_ops;

	if (!space)
		return;

	mem_ops = space->ops;
	WARN_ON_ONCE(space->mm);

	mem_ops->dealloc(space);
}

int nommu_swmmu_space_attach(struct mm_struct *mm,
			struct nommu_swmmu_space *space)
{
	if (!mm || !space)
		return -EINVAL;

	if (mm->swmmu_space || space->mm)
		return -EBUSY;

	mm->swmmu_space = space;
	space->mm = mm;

	return 0;
}

/*
 * Detach and release the mm_struct-owned reference to its SWMMU space.
 */
void nommu_swmmu_space_detach(struct mm_struct *mm)
{
	struct nommu_swmmu_space *space;

	if (!mm)
		return;

	space = mm->swmmu_space;
	if (!space)
		return;

	if (space->mm == mm)
		space->mm = NULL;

	mm->swmmu_space = NULL;

	nommu_swmmu_space_put(space);
}

void nommu_swmmu_space_get(struct nommu_swmmu_space *space)
{
	if (!space)
		BUG();

	refcount_inc(&space->users);
}

void nommu_swmmu_space_put(struct nommu_swmmu_space *space)
{
	if (!space)
		return;

	if (!refcount_dec_and_test(&space->users))
		return;

	swmmu_space_destroy_final(space);
}

static bool
nommu_swmmu_space_empty(struct nommu_swmmu_space *space)
{
	struct mm_struct *mm = space->mm;
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);

	for_each_vma(vmi, vma) {
		if (vma->vm_swmmu_data)
			return false;
	}

	return true;
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
static struct nommu_swmmu_space *kunit_test_space;
#endif

struct nommu_swmmu_space *nommu_swmmu_current(void)
{
#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
	/*
	 * KUnit tests may run from a kernel thread without an mm_struct.
	 * This override exists only to exercise compiler-generated calls
	 * against an explicitly selected test space.
	 */
	if (unlikely(kunit_test_space))
		return kunit_test_space;
#endif

	if (!current->mm ||
		READ_ONCE(current->mm->swmmu_mode) != NOMMU_SWMMU_ON)
		return NULL;

	return current->mm->swmmu_space;
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
void nommu_swmmu_kunit_set_space(struct nommu_swmmu_space *space)
{
	kunit_test_space = space;
}

void nommu_swmmu_kunit_clear_space(void)
{
	kunit_test_space = NULL;
}

int nommu_swmmu_kunit_backing_alloc(
	struct nommu_swmmu_space *space,
	size_t size,
	struct nommu_swmmu_backing **out)
{
	if (!space || !out || !size)
		return -EINVAL;

	*out = swmmu_backing_alloc(space, size);
	if (!*out)
		return -ENOMEM;

	return 0;
}

void nommu_swmmu_kunit_backing_release(
	struct nommu_swmmu_space *space,
	struct nommu_swmmu_backing *backing)
{
	swmmu_backing_release(space, backing);
}

int nommu_swmmu_kunit_vma_dup(
	struct nommu_swmmu_space *space,
	const struct nommu_swmmu_vma *src,
	struct nommu_swmmu_vma **out)
{
	if (!space || !src || !out)
		return -EINVAL;

	*out = swmmu_vma_data_dup(space, src);
	if (!*out)
		return -ENOMEM;

	return 0;
}

void nommu_swmmu_kunit_vma_release(
	struct nommu_swmmu_space *space,
	struct nommu_swmmu_vma *data)
{
	swmmu_vma_data_release(space, data);
}
#endif


#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
#define SWMMU_VALIDATE_FAIL(fmt, ...)				\
	do {							\
		pr_err("SWMMU validation failed: " fmt,		\
		       ##__VA_ARGS__);				\
		return -EINVAL;					\
	} while (0)

/*
 * Validate the committed SWMMU/VMA state.
 *
 * The caller must hold mmap_lock and space->lock.
 */
static int __nommu_swmmu_validate(struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	mmap_assert_locked(mm);

	for_each_vma(vmi, vma) {
		struct nommu_swmmu_vma *data;

		if (!vma->vm_swmmu_data)
			continue;

		data = vma->vm_swmmu_data;

		VM_BUG_ON_MM(!data->backing, mm);
		VM_BUG_ON_MM(!data->page_count, mm);
		VM_BUG_ON_MM(
			vma->vm_end - vma->vm_start !=
			data->page_count * SWMMU_PAGE_SIZE,
			mm);
	}

	return 0;
}

/*
 * Validate the committed SWMMU/VMA state.
 *
 * The caller must hold mmap_lock. This function acquires
 * the SWMMU space read lock.
 */
void nommu_swmmu_validate(struct mm_struct *mm)
{
	struct nommu_swmmu_space *space;
	int ret;

	if (!mm)
		return;

	space = mm->swmmu_space;
	if (!space)
		return;

	mmap_assert_locked(mm);

	/*
	 * mmap_lock is held by the caller. Acquire the SWMMU
	 * space lock after it.
	 */
	down_read(&space->lock);
	ret = __nommu_swmmu_validate(mm);
	up_read(&space->lock);

	VM_BUG_ON_MM(ret, mm);
}

#else
static inline int __nommu_swmmu_validate(struct mm_struct *mm)
{
	return 0;
}

#endif


static void *__swmmu_translate_mm(struct mm_struct *mm,
				unsigned long address,
				size_t size,
				int *status,
				int write)
{
	struct vm_area_struct *vma;
	struct nommu_swmmu_vma *swmmu_vma;
	struct nommu_swmmu_backing *backing;
	size_t vma_offset;
	size_t page_index;
	size_t page_offset;

	mmap_assert_locked(mm);

	if (!size || address > ULONG_MAX - (size - 1)) {
		*status = -EFAULT;
		return NULL;
	}

	vma = find_vma(mm, address);
	if (!vma ||
		!vma->vm_swmmu_data ||
		address < vma->vm_start ||
		size > vma->vm_end - address) {
		*status = -EFAULT;
		return NULL;
	}

	swmmu_vma = vma->vm_swmmu_data;
	backing = swmmu_vma->backing;

	if (write) {
		if (!(swmmu_vma->access & NOMMU_SWMMU_WRITE)) {
			*status = -EACCES;
			return NULL;
		}
	} else if (!(swmmu_vma->access & NOMMU_SWMMU_READ)) {
		*status = -EACCES;
		return NULL;
	}

	vma_offset = address - vma->vm_start;
	page_index = swmmu_vma->page_offset +
		vma_offset / SWMMU_PAGE_SIZE;
	page_offset = vma_offset % SWMMU_PAGE_SIZE;

	if (page_index >= backing->page_count ||
		page_offset + size > SWMMU_PAGE_SIZE) {
		*status = -EINVAL;
		return NULL;
	}

	return page_address(backing->pages[page_index]) + page_offset;
}

static int swmmu_copy_from_mm(struct mm_struct *mm,
			      unsigned long address,
			      void *destination,
			      size_t size)
{
	int ret = 0;

	mmap_read_lock(mm);

	while (size) {
		size_t offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - offset;
		void *source;

		if (chunk > size)
			chunk = size;

		source = __swmmu_translate_mm(mm, address, chunk,
					      &ret, false);
		if (!source)
			break;

		memcpy(destination, source, chunk);

		address += chunk;
		destination = (unsigned char *)destination + chunk;
		size -= chunk;
	}

	mmap_read_unlock(mm);
	return ret;
}

static int swmmu_copy_to_mm(struct mm_struct *mm,
			uintptr_t address, const void *source,
			size_t size)
{
	int ret = 0;

	mmap_read_lock(mm);

	while (size) {
		size_t page_offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - page_offset;
		void *destination;

		if (chunk > size)
			chunk = size;

		destination = __swmmu_translate_mm(mm, address, chunk, &ret, 1);
		if (!destination)
			goto out;

		memcpy(destination, source, chunk);

		address += chunk;
		source = (const unsigned char *)source + chunk;
		size -= chunk;
	}

out:
	mmap_read_unlock(mm);
	return ret;
}

static int __swmmu_check_access_mm(struct mm_struct *mm,
				   unsigned long address,
				   size_t size,
				   int write)
{
	int ret = 0;

	mmap_assert_locked(mm);

	while (size) {
		size_t offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - offset;

		if (chunk > size)
			chunk = size;

		if (!__swmmu_translate_mm(mm, address, chunk,
					  &ret, write))
			return ret;

		address += chunk;
		size -= chunk;
	}

	return 0;
}

int nommu_swmmu_check_access(struct nommu_swmmu_space *space,
			uintptr_t address,
			size_t size,
			int write)
{
	struct mm_struct *mm;
	int ret;

	if (!space || !size)
		return -EINVAL;

	mm = space->mm;
	if (!mm)
		return -ENODEV;

	mmap_read_lock(mm);
	ret = __swmmu_check_access_mm(mm, address, size, write);
	mmap_read_unlock(mm);

	return ret;
}



int
nommu_swmmu_load_u64_checked(const void *address,
			     size_t size,
			     u64 *value)
{
	struct mm_struct *mm = current->mm;

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		return -EFAULT;

	if (!value ||
	    (size != 1 && size != 2 && size != 4 && size != 8))
		return -EINVAL;

	return swmmu_copy_from_mm(mm, (unsigned long)address,
				  value, size);

}

int
nommu_swmmu_store_u64_checked(void *address,
			      size_t size,
			      u64 value)
{
	int ret;
	struct mm_struct *mm = current->mm;

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		return -EFAULT;

	if (size != 1 && size != 2 && size != 4 && size != 8)
		return -EINVAL;

	ret = swmmu_copy_to_mm(mm,
			(uintptr_t)address,
			&value,
			size);

	return ret;
}

uint64_t nommu_swmmu_load_u64(const void *address, size_t size)
{
	uint64_t value = 0;
	int ret;
	struct mm_struct *mm = current->mm;

	if (size != 1 && size != 2 && size != 4 && size != 8)
		BUG();

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		BUG();

	ret = swmmu_copy_from_mm(mm,
				    (uintptr_t)address,
				    &value,
				    size);
	if (ret)
		BUG();

	return value;
}

long nommu_swmmu_store_u64(void *address, size_t size, uint64_t value)
{
	int ret;
	struct mm_struct *mm = current->mm;

	if (size != 1 && size != 2 && size != 4 && size != 8)
		BUG();

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		BUG();

	ret = swmmu_copy_to_mm(mm,
			(uintptr_t)address,
			&value,
			size);
	if (ret)
		BUG();

	return ret;
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
int nommu_swmmu_kunit_load_mm(struct mm_struct *mm,
			      uintptr_t address,
			      size_t size,
			      u64 *value)
{
	if (!mm || !value)
		return -EINVAL;

	if (size != 1 && size != 2 &&
	    size != 4 && size != 8)
		return -EINVAL;

	return swmmu_copy_from_mm(mm, address, value, size);
}

int nommu_swmmu_kunit_store_mm(struct mm_struct *mm,
			       uintptr_t address,
			       size_t size,
			       u64 value)
{
	if (!mm)
		return -EINVAL;

	if (size != 1 && size != 2 &&
	    size != 4 && size != 8)
		return -EINVAL;

	return swmmu_copy_to_mm(mm, address, &value, size);
}
#endif

int nommu_swmmu_unmap(struct nommu_swmmu_space *space,
		      unsigned long address,
		      size_t size)
{
	return -EOPNOTSUPP;

}
long nommu_swmmu_remap(struct nommu_swmmu_space *space,
			unsigned long address,
			size_t old_size,
			size_t new_size)
{
	return -EOPNOTSUPP;
}

int nommu_swmmu_dup_mmap(struct mm_struct *dst,
			 struct mm_struct *src)
{
	struct nommu_swmmu_space *src_space;
	struct nommu_swmmu_space *dst_space;
	struct nommu_swmmu_clone_item *item;
	struct nommu_swmmu_clone_item *tmp;
	struct vm_area_struct *src_vma;
	LIST_HEAD(prepared);
	VMA_ITERATOR(src_vmi, src, 0);
	int ret = 0;

	if (!dst || !src)
		return -EINVAL;

	/* dup_mmap() holds both locks when calling us. */
	mmap_assert_write_locked(src);
	mmap_assert_write_locked(dst);

	src_space = src->swmmu_space;
	dst_space = dst->swmmu_space;

	if (!src_space || !dst_space || src_space == dst_space)
		return -EINVAL;

	down_read(&src_space->lock);
	down_write_nested(&dst_space->lock, SINGLE_DEPTH_NESTING);

	nommu_swmmu_clear_child_vmas(dst);

	for_each_vma(src_vmi, src_vma) {
		struct nommu_swmmu_vma *src_data;

		src_data = src_vma->vm_swmmu_data;
		if (!src_data)
			continue;

		if (!src_data->backing ||
			src_data->page_offset > src_data->backing->page_count ||
			src_data->page_count >
			src_data->backing->page_count - src_data->page_offset) {
			ret = -EINVAL;
			goto rollback;
		}

		item = dst_space->ops->zalloc(sizeof(*item), GFP_KERNEL);
		if (!item) {
			ret = -ENOMEM;
			goto rollback;
		}

		item->src_vma = src_vma;
		item->dst_vma = vm_area_dup(src_vma);
		if (!item->dst_vma) {
			dst_space->ops->dealloc(item);
			ret = -ENOMEM;
			goto rollback;
		}

		item->dst_vma->vm_mm = dst;
		item->dst_vma->vm_swmmu_data = NULL;

		/* exclude generic-nommu region */
		if (item->dst_vma->vm_region) {
			vm_area_free(item->dst_vma);
			dst_space->ops->dealloc(item);
			ret = -EOPNOTSUPP;
			goto rollback;
		}

		item->new_data = swmmu_vma_data_dup(dst_space, src_data);
		if (!item->new_data) {
			vm_area_free(item->dst_vma);
			dst_space->ops->dealloc(item);
			ret = -ENOMEM;
			goto rollback;
		}

		list_add_tail(&item->node, &prepared);
	}

	/*
	 * No operation below this point may fail.
	 * Publish all child metadata only after every clone succeeded.
	 */
	list_for_each_entry(item, &prepared, node) {
		VMA_ITERATOR(dst_vmi, dst, item->dst_vma->vm_start);

		vma_iter_config(&dst_vmi,
				item->dst_vma->vm_start,
				item->dst_vma->vm_end);

		ret = vma_iter_prealloc(&dst_vmi, item->dst_vma);
		if (ret)
			goto rollback;

		vma_iter_store_new(&dst_vmi, item->dst_vma);
		dst->map_count++;

		item->dst_vma->vm_swmmu_data = item->new_data;
		item->new_data = NULL;
		item->published = true;
	}

	list_for_each_entry_safe(item, tmp, &prepared, node) {
		list_del(&item->node);
		dst_space->ops->dealloc(item);
	}

	up_write(&dst_space->lock);
	up_read(&src_space->lock);

	return 0;

rollback:
	list_for_each_entry_safe(item, tmp, &prepared, node) {
		list_del(&item->node);

		nommu_swmmu_clone_item_abort(dst, dst_space, item);
		dst_space->ops->dealloc(item);
	}

	up_write(&dst_space->lock);
	up_read(&src_space->lock);

	return ret;
}

int swmmu_clone_space(struct nommu_swmmu_space *parent,
		struct nommu_swmmu_space **child_out)
{
	return -EOPNOTSUPP;
}

static unsigned int swmmu_access_from_prot(unsigned long prot)
{
	unsigned int access = NOMMU_SWMMU_NONE;

	if (prot & PROT_READ)
		access |= NOMMU_SWMMU_READ;
	if (prot & PROT_WRITE)
		access |= NOMMU_SWMMU_WRITE;
	if (prot & PROT_EXEC)
		access |= NOMMU_SWMMU_EXEC;

	return access;
}

static int swmmu_resize_prepare(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma *vma_data,
				size_t new_size,
				struct nommu_swmmu_vma_tx *tx)
{
	struct nommu_swmmu_backing *old_backing;
	struct nommu_swmmu_backing *new_backing;
	size_t old_page_count;
	size_t new_page_count;
	size_t copy_count;
	size_t i;
	int ret;

	if (!space || !vma_data || !vma_data->backing || !new_size)
		return -EINVAL;

	if (!IS_ALIGNED(new_size, SWMMU_PAGE_SIZE))
		return -EINVAL;

	old_backing = vma_data->backing;
	old_page_count = vma_data->page_count;
	new_page_count = new_size / SWMMU_PAGE_SIZE;

	if (!old_page_count || !new_page_count)
		return -EINVAL;

	if (vma_data->page_offset > old_backing->page_count ||
	    old_page_count >
		old_backing->page_count - vma_data->page_offset)
		return -EINVAL;

	if (new_page_count > SIZE_MAX / SWMMU_PAGE_SIZE)
		return -EOVERFLOW;

	/*
	 * The caller normally handles equal-size remaps before reaching
	 * this function.
	 */
	if (new_page_count == old_page_count)
		return -EINVAL;

	memset(tx, 0, sizeof(*tx));

	tx->vma_data = vma_data;
	tx->old_backing = old_backing;
	tx->old_page_count = old_page_count;
	tx->new_page_count = new_page_count;
	tx->type = NOMMU_SWMMU_VMA_TX_RESIZE;

	/*
	 * Allocate a new backing for the resized VMA view.
	 * The new backing starts at page offset zero.
	 */
	new_backing = swmmu_backing_alloc(space,
					  new_page_count * SWMMU_PAGE_SIZE);
	if (!new_backing)
		return -ENOMEM;

	copy_count = min(old_page_count, new_page_count);

	for (i = 0; i < copy_count; i++) {
		ret = space->ops->page_copy(
			new_backing->pages[i],
			old_backing->pages[vma_data->page_offset + i]);
		if (ret) {
			swmmu_backing_release(space, new_backing);
			return ret;
		}
	}

	/*
	 * Pages beyond copy_count remain zero-filled as prepared by
	 * swmmu_backing_alloc().
	 */
	tx->new_backing = new_backing;
	return 0;
}

static void swmmu_resize_abort(struct nommu_swmmu_space *space,
			struct nommu_swmmu_vma_tx *tx)
{
	if (!space || !tx || !tx->new_backing)
		return;

	swmmu_backing_release(space, tx->new_backing);
	tx->new_backing = NULL;
}

static void swmmu_resize_commit(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma_tx *tx)
{
	struct nommu_swmmu_backing *old_backing;

	if (!space || !tx || !tx->vma_data || !tx->new_backing)
		return;

	old_backing = tx->vma_data->backing;

	/*
	 * The new backing represents exactly the VMA's new view and
	 * always starts at page zero.
	 */
	tx->vma_data->backing = tx->new_backing;
	tx->vma_data->page_offset = 0;
	tx->vma_data->page_count = tx->new_page_count;

	tx->new_backing = NULL;

	swmmu_backing_release(space, old_backing);
}

static int swmmu_split_prepare(struct vm_area_struct *vma,
			struct vm_area_struct *new,
			unsigned long addr,
			bool new_below,
			void **state)
{
	struct nommu_swmmu_space *space = vma->vm_mm->swmmu_space;
	struct nommu_swmmu_vma *old_data;
	struct nommu_swmmu_vma *new_data;
	struct nommu_swmmu_vma_tx *tx;
	unsigned long split_pages;

	old_data = vma->vm_swmmu_data;
	if (!old_data || !old_data->backing)
		return -EINVAL;

	if (addr <= vma->vm_start || addr >= vma->vm_end)
		return -EINVAL;

	if (!IS_ALIGNED(addr - vma->vm_start, SWMMU_PAGE_SIZE))
		return -EINVAL;

	split_pages = (addr - vma->vm_start) / SWMMU_PAGE_SIZE;

	if (!split_pages || split_pages >= old_data->page_count)
		return -EINVAL;

	if (old_data->page_offset >
	    old_data->backing->page_count)
		return -EINVAL;

	if (old_data->page_count >
	    old_data->backing->page_count - old_data->page_offset)
		return -EINVAL;

	if (new_below) {
		if (new->vm_start != vma->vm_start ||
			new->vm_end != addr)
			return -EINVAL;
	} else {
		if (new->vm_start != addr ||
			new->vm_end != vma->vm_end)
			return -EINVAL;
	}

	tx = space->ops->zalloc(sizeof(*tx), GFP_KERNEL);
	if (!tx)
		return -ENOMEM;

	new_data = space->ops->zalloc(sizeof(*new_data), GFP_KERNEL);
	if (!new_data) {
		space->ops->dealloc(tx);
		return -ENOMEM;
	}

	tx->type = NOMMU_SWMMU_VMA_TX_SPLIT;
	tx->vma_data = old_data;
	tx->new_vma_data = new_data;
	tx->old_backing = old_data->backing;
	tx->old_page_count = old_data->page_count;
	tx->old_page_offset = old_data->page_offset;
	tx->split_page_count = split_pages;

	/*
	 * Both VMAs refer to the same immutable backing object.
	 */
	refcount_inc(&tx->old_backing->refs);
	tx->backing_ref_held = true;

	new_data->backing = tx->old_backing;
	new_data->access = old_data->access;

	if (new_below) {
		new_data->page_offset = old_data->page_offset;
		new_data->page_count = split_pages;
	} else {
		new_data->page_offset =
			old_data->page_offset + split_pages;
		new_data->page_count =
			old_data->page_count - split_pages;
	}

	*state = tx;
	return 0;
}

static void swmmu_split_commit(struct vm_area_struct *vma,
			       struct vm_area_struct *new,
			       unsigned long addr,
			       bool new_below,
			       void *state)
{
	struct nommu_swmmu_space *space = vma->vm_mm->swmmu_space;
	struct nommu_swmmu_vma_tx *tx = state;

	if (new_below) {
		tx->vma_data->page_offset += tx->split_page_count;
		tx->vma_data->page_count -= tx->split_page_count;
	} else {
		tx->vma_data->page_count = tx->split_page_count;
	}

	new->vm_swmmu_data = tx->new_vma_data;
	tx->new_vma_data = NULL;
	tx->backing_ref_held = false;

	space->ops->dealloc(tx);
}

static void swmmu_split_abort(struct vm_area_struct *vma,
			struct vm_area_struct *new,
			void *state)
{
	struct nommu_swmmu_space *space = vma->vm_mm->swmmu_space;
	struct nommu_swmmu_vma_tx *tx = state;

	if (!tx)
		return;

	if (tx->new_vma_data)
		space->ops->dealloc(tx->new_vma_data);

	if (tx->backing_ref_held)
		swmmu_backing_release(space, tx->old_backing);

	space->ops->dealloc(tx);
}

static void nommu_swmmu_remove_detached_vma(struct mm_struct *mm,
					struct vm_area_struct *vma)
{
	if (!mm || !vma)
		return;

	nommu_swmmu_vma_close(mm, vma);
	vma_close(vma);

	if (vma->vm_file)
		fput(vma->vm_file);

	vm_area_free(vma);
}

static int nommu_swmmu_replace_prepare(struct vma_replace_struct *vrs)
{
	struct nommu_swmmu_vma_tx *tx;
	struct vm_area_struct *insert;
	struct mm_struct *mm;
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_backing *backing;
	struct nommu_swmmu_vma *data;
	unsigned long length;
	unsigned int access = 0;

	if (!vrs || !vrs->vms || !vrs->insert)
		return -EINVAL;

	tx = vrs->backend_state;
	if (!tx)
		return -EINVAL;

	insert = vrs->insert;
	mm = insert->vm_mm;
	space = mm->swmmu_space;
	length = insert->vm_end - insert->vm_start;

	if (!space || !length)
		return -EINVAL;

	if (insert->vm_flags & VM_READ)
		access |= NOMMU_SWMMU_READ;
	if (insert->vm_flags & VM_WRITE)
		access |= NOMMU_SWMMU_WRITE;
	if (insert->vm_flags & VM_EXEC)
		access |= NOMMU_SWMMU_EXEC;

	backing = swmmu_backing_alloc(space, length);
	if (!backing)
		return -ENOMEM;

	data = swmmu_vma_data_create(space, backing);
	if (!data) {
		swmmu_backing_release(space, backing);
		return -ENOMEM;
	}

	data->access = tx->replacement_access;
	insert->vm_swmmu_data = data;
	vrs->backend_state = data;

	return 0;
}

static void nommu_swmmu_replace_abort(struct vma_replace_struct *vrs)
{
	struct vm_area_struct *insert;
	struct mm_struct *mm;

	if (!vrs || !vrs->insert)
		return;

	insert = vrs->insert;
	mm = insert->vm_mm;

	if (insert->vm_swmmu_data) {
		swmmu_vma_data_release(mm->swmmu_space,
				       insert->vm_swmmu_data);
		insert->vm_swmmu_data = NULL;
	}

	vrs->backend_state = NULL;
}

static void nommu_swmmu_replace_commit(
	struct vma_replace_struct *vrs)
{
	if (!vrs)
		return;

	vrs->backend_state = NULL;
}

static const struct vma_backend_ops nommu_swmmu_vma_backend_ops = {
	.split_prepare = swmmu_split_prepare,
	.split_commit = swmmu_split_commit,
	.split_abort = swmmu_split_abort,
	.remove_detached = nommu_swmmu_remove_detached_vma,
	.replace_prepare = nommu_swmmu_replace_prepare,
	.replace_commit = nommu_swmmu_replace_commit,
	.replace_abort = nommu_swmmu_replace_abort,
};

int nommu_swmmu_expand_prepare(struct nommu_swmmu_space *space,
				struct vm_area_struct *vma,
				unsigned long new_end,
				struct nommu_swmmu_vma_tx *tx)
{
	size_t new_size;
	int ret;

	if (!space || !vma || !tx)
		return -EINVAL;

	memset(tx, 0, sizeof(*tx));

	if (!vma->vm_swmmu_data)
		return 0;

	if (new_end <= vma->vm_end)
		return -EINVAL;

	/*
	 * Current vma_expand() only supports extending the VMA's end.
	 * Head expansion and merging with next are intentionally deferred.
	 */
	new_size = new_end - vma->vm_start;
	if (!IS_ALIGNED(new_size, SWMMU_PAGE_SIZE))
		return -EINVAL;

	ret = swmmu_resize_prepare(space,
				vma->vm_swmmu_data,
				new_size,
				tx);

	if (ret)
		return ret;

	tx->type = NOMMU_SWMMU_VMA_TX_EXPAND;
	return 0;
}

void nommu_swmmu_vma_expand_commit(struct vm_area_struct *target,
				   struct nommu_swmmu_vma_tx *tx)
{
	struct nommu_swmmu_space *space;

	if (!target || !tx ||
	    tx->type != NOMMU_SWMMU_VMA_TX_EXPAND)
		return;

	space = target->vm_mm->swmmu_space;
	if (!space)
		return;

	if (tx->type != NOMMU_SWMMU_VMA_TX_EXPAND)
		return;

	swmmu_resize_commit(space, tx);
}

void nommu_swmmu_vma_expand_abort(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma_tx *tx)
{
	if (tx->type != NOMMU_SWMMU_VMA_TX_EXPAND)
		return;

	if (!space)
		return;

	swmmu_resize_abort(space, tx);
}

static struct vm_area_struct *swmmu_fixed_alloc_vma(struct mm_struct *mm,
						struct vm_area_struct *template,
						unsigned long start,
						unsigned long end,
						vma_flags_t vma_flags,
						unsigned long pgoff)
{
	struct vm_area_struct *vma;

	if (template)
		vma = vm_area_dup(template);
	else
		vma = vm_area_alloc(mm);

	if (!vma)
		return NULL;

	vma->vm_mm = mm;
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_swmmu_data = NULL;

	vm_flags_init(vma, vma_flags_to_legacy(vma_flags));
	vma_set_pgoff(vma, pgoff);

	return vma;
}

static int swmmu_fixed_replace_prepare(struct nommu_swmmu_space *space,
				struct vm_area_struct *old_vma,
				unsigned long start,
				unsigned long end,
				enum nommu_swmmu_replace_kind kind,
				struct nommu_swmmu_vma_tx *tx)
{
	struct nommu_swmmu_vma *old_data;
	struct nommu_swmmu_vma *retained;
	unsigned long split_pages;
	unsigned long retained_offset;
	size_t retained_pages;

	if (!space || !old_vma || !tx)
		return -EINVAL;

	old_data = old_vma->vm_swmmu_data;
	if (!old_data || !old_data->backing)
		return -EINVAL;

	if (start < old_vma->vm_start ||
	    end > old_vma->vm_end ||
	    start >= end)
		return -EINVAL;

	if (kind == NOMMU_SWMMU_REPLACE_HEAD) {
		if (start != old_vma->vm_start ||
		    end >= old_vma->vm_end)
			return -EINVAL;

		split_pages = (end - old_vma->vm_start) /
			      SWMMU_PAGE_SIZE;
		retained_offset = old_data->page_offset + split_pages;
		retained_pages = old_data->page_count - split_pages;
	} else if (kind == NOMMU_SWMMU_REPLACE_TAIL) {
		if (start <= old_vma->vm_start ||
		    end != old_vma->vm_end)
			return -EINVAL;

		split_pages = (start - old_vma->vm_start) /
			      SWMMU_PAGE_SIZE;
		retained_offset = old_data->page_offset;
		retained_pages = split_pages;
	} else {
		return -EOPNOTSUPP;
	}

	if (!split_pages ||
	    !retained_pages ||
	    old_data->page_offset > old_data->backing->page_count ||
	    old_data->page_count >
	    old_data->backing->page_count - old_data->page_offset)
		return -EINVAL;

	memset(tx, 0, sizeof(*tx));

	retained = space->ops->zalloc(sizeof(*retained), GFP_KERNEL);
	if (!retained)
		return -ENOMEM;

	refcount_inc(&old_data->backing->refs);

	retained->backing = old_data->backing;
	retained->page_offset = retained_offset;
	retained->page_count = retained_pages;
	retained->access = old_data->access;

	tx->type = NOMMU_SWMMU_VMA_TX_FIXED_REPLACE;
	tx->kind = kind;
	tx->old_vma = old_vma;
	tx->vma_data = old_data;
	tx->replace_start = start;
	tx->replace_end = end;

	if (kind == NOMMU_SWMMU_REPLACE_HEAD)
		tx->right_data = retained;
	else
		tx->left_data = retained;

	return 0;
}

static void swmmu_fixed_replace_commit(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma_tx *tx)
{
	struct nommu_swmmu_vma *old_data;
	struct nommu_swmmu_vma *retained;

	if (!space || !tx ||
	    tx->type != NOMMU_SWMMU_VMA_TX_FIXED_REPLACE)
		return;

	old_data = tx->vma_data;

	if (tx->kind == NOMMU_SWMMU_REPLACE_HEAD)
		retained = tx->right_data;
	else
		retained = tx->left_data;

	if (WARN_ON_ONCE(!tx->old_vma ||
			 !tx->new_vma ||
			 !tx->new_vma->vm_swmmu_data ||
			 !old_data ||
			 !retained))
		return;

	tx->old_vma->vm_swmmu_data = retained;

	if (tx->kind == NOMMU_SWMMU_REPLACE_HEAD)
		tx->right_data = NULL;
	else
		tx->left_data = NULL;

	tx->vma_data = NULL;

	swmmu_vma_data_release(space, old_data);

	tx->old_vma = NULL;
	tx->new_vma = NULL;
	tx->type = 0;
}

static void swmmu_fixed_replace_abort(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma_tx *tx)
{
	if (!space || !tx ||
	    tx->type != NOMMU_SWMMU_VMA_TX_FIXED_REPLACE)
		return;

	swmmu_vma_data_release(space, tx->left_data);
	swmmu_vma_data_release(space, tx->right_data);
	swmmu_vma_data_release(space, tx->replacement_data);

	if (tx->new_vma)
		vm_area_free(tx->new_vma);

	tx->left_data = NULL;
	tx->right_data = NULL;
	tx->replacement_data = NULL;
	tx->new_vma = NULL;
	tx->old_vma = NULL;
	tx->vma_data = NULL;
	tx->type = 0;
}


static unsigned long swmmu_mmap_fixed_replace(struct mm_struct *mm,
					enum nommu_swmmu_replace_kind kind,
					struct nommu_swmmu_space *space,
					struct vm_area_struct *old_vma,
					unsigned long start,
					unsigned long end,
					unsigned long prot,
					vma_flags_t vma_flags,
					unsigned long pgoff)
{
	struct nommu_swmmu_vma_tx tx = {};
	struct nommu_swmmu_backing *backing;
	struct vm_area_struct *new_vma;
	VMA_ITERATOR(vmi, mm, start);
	unsigned long length = end - start;
	unsigned int access = 0;
	unsigned long split_pages;
	int ret;

	if (!old_vma->vm_swmmu_data ||
	    old_vma->vm_file ||
	    old_vma->vm_region)
		return -EOPNOTSUPP;

	ret = swmmu_fixed_replace_prepare(space, old_vma, start, end, kind, &tx);
	if (ret)
		return ret;

	backing = swmmu_backing_alloc(space, length);
	if (!backing) {
		ret = -ENOMEM;
		goto abort;
	}

	tx.replacement_data =
		swmmu_vma_data_create(space, backing);
	if (!tx.replacement_data) {
		swmmu_backing_release(space, backing);
		ret = -ENOMEM;
		goto abort;
	}

	new_vma = swmmu_fixed_alloc_vma(mm, old_vma, start, end, vma_flags,
					pgoff);
	if (!new_vma) {
		ret = -ENOMEM;
		goto abort;
	}

	tx.new_vma = new_vma;
	access = swmmu_access_from_prot(prot);
	tx.replacement_data->access = access;

	/*
	 * The store range is the replacement prefix. Maple Tree keeps
	 * the remainder of old_vma's original range as the suffix.
	 */
	vma_iter_config(&vmi, start, end);
	ret = vma_iter_prealloc(&vmi, new_vma);
	if (ret)
		goto abort;

	new_vma->vm_swmmu_data = tx.replacement_data;
	tx.replacement_data = NULL;

	vma_start_write(old_vma);
	vma_start_write(new_vma);

	split_pages = (end - old_vma->vm_start) / SWMMU_PAGE_SIZE;

	/*
	 * This is the point of no return: preallocation succeeded and
	 * the following store must not fail.
	 */
	if (kind == NOMMU_SWMMU_REPLACE_HEAD) {
		old_vma->vm_start = end;
		vma_add_pgoff(old_vma, split_pages);
	} else if  (kind == NOMMU_SWMMU_REPLACE_TAIL) {
		old_vma->vm_end = start;
		vma_add_pgoff(new_vma,
			(start - old_vma->vm_start) >> PAGE_SHIFT);
	}

	vma_iter_store_new(&vmi, new_vma);

	mm->map_count++;
	swmmu_fixed_replace_commit(space, &tx);

	validate_mm(mm);
	nommu_swmmu_validate(mm);

	return start;

abort:
	swmmu_fixed_replace_abort(space, &tx);
	return ret;
}

static unsigned long swmmu_mmap_fixed_middle_replace(struct mm_struct *mm,
						struct nommu_swmmu_space *space,
						struct vm_area_struct *old_vma,
						unsigned long start,
						unsigned long end,
						unsigned long prot,
						vma_flags_t vma_flags,
						unsigned long pgoff)
{
	struct vm_area_struct *new_vma;
	struct vma_munmap_struct vms;
	struct vma_replace_struct vrs;
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	VMA_ITERATOR(vmi, mm, start);
	struct nommu_swmmu_vma_tx tx = {};
	int ret;

	if (!old_vma->vm_swmmu_data ||
	    old_vma->vm_file ||
	    old_vma->vm_region)
		return -EOPNOTSUPP;

	new_vma = swmmu_fixed_alloc_vma(mm, old_vma, start, end,
					vma_flags, pgoff);
	if (!new_vma)
		return -ENOMEM;

	tx.replacement_access = swmmu_access_from_prot(prot);

	mt_init_flags(&mt_detach,
		      vmi.mas.tree->ma_flags &
		      MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	vma_init_munmap(&vms, &vmi, old_vma,
			start, end, NULL, false,
			&nommu_swmmu_vma_backend_ops);

	vma_replace_init(&vrs, &vms, new_vma,
			 &nommu_swmmu_vma_backend_ops);
	vrs.backend_state = &tx;

	ret = vma_replace_prepare(&vrs, &mas_detach);
	if (ret)
		goto abort;

	vma_replace_commit(&vrs, &mas_detach, mm,
			   nommu_swmmu_remove_detached_vma);

	__mt_destroy(&mt_detach);

	validate_mm(mm);
	nommu_swmmu_validate(mm);

	return start;

abort:
	vma_replace_abort(&vrs, &mas_detach);
	__mt_destroy(&mt_detach);
	return ret;
}

static unsigned long swmmu_mmap_fixed_range_replace(struct mm_struct *mm,
						struct nommu_swmmu_space *space,
						unsigned long start,
						unsigned long end,
						unsigned long prot,
						vma_flags_t vma_flags,
						unsigned long pgoff)
{
	struct vm_area_struct *insert;
	struct vma_munmap_struct vms;
	struct vma_replace_struct vrs;
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	VMA_ITERATOR(vmi, mm, start);
	struct nommu_swmmu_vma_tx tx = {};
	int ret;

	insert = swmmu_fixed_alloc_vma(mm, NULL, start, end,
				vma_flags, pgoff);
	if (!insert)
		return -ENOMEM;

	insert->vm_swmmu_data = NULL;

	tx.replacement_access = swmmu_access_from_prot(prot);

	mt_init_flags(&mt_detach,
		      vmi.mas.tree->ma_flags &
		      MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	vma_init_munmap(&vms, &vmi, vma_find(&vmi, end),
			start, end, NULL, false,
			&nommu_swmmu_vma_backend_ops);

	vma_replace_init(&vrs, &vms, insert,
			 &nommu_swmmu_vma_backend_ops);
	vrs.backend_state = &tx;

	ret = vma_replace_prepare(&vrs, &mas_detach);
	if (ret)
		goto abort;

	vma_replace_commit(&vrs, &mas_detach, mm,
			   nommu_swmmu_remove_detached_vma);

	__mt_destroy(&mt_detach);

	validate_mm(mm);
	nommu_swmmu_validate(mm);

	return start;

abort:
	vma_replace_abort(&vrs, &mas_detach);
	__mt_destroy(&mt_detach);
	return ret;
}

static unsigned long
__do_mmap_swmmu(struct mm_struct *mm,
		struct file *file,
		unsigned long addr,
		unsigned long len,
		unsigned long prot,
		unsigned long flags,
		vma_flags_t vma_flags,
		unsigned long pgoff,
		unsigned long *populate,
		struct list_head *uf)
{
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_backing *backing;
	struct nommu_swmmu_vma *swmmu_vma;
	struct vm_area_struct *vma;
	struct vm_area_struct *replace_vma = NULL;
	VMA_ITERATOR(vmi, mm, 0);
	unsigned long start;
	unsigned long end;
	int ret;
	unsigned int access;
	int overlap_count;

	if (!mm)
		return -EINVAL;

	space = mm->swmmu_space;
	if (!space)
		return -EINVAL;

	mmap_assert_write_locked(mm);

	/*
	 * Select start/end using VMA/range management.
	 * This replaces swmmu_find_free_range().
	 */
	/*
	 * currently partially implemented:
	 *
	 * address == 0, no fixed flag:
	 *     allocate at next_address
	 *
	 * nonzero address, no fixed flag:
	 *     reject with -EINVAL
	 *
	 * MAP_FIXED:
	 *     exact address, but reject overlap for now
	 *
	 * MAP_FIXED_NOREPLACE:
	 *     exact address, reject overlap
	 */
	if (flags & MAP_FIXED || flags & MAP_FIXED_NOREPLACE) {
		enum nommu_swmmu_replace_kind kind = NOMMU_SWMMU_REPLACE_EXACT;

		if (!addr || !PAGE_ALIGNED(addr)) {
			return -EINVAL;
		}

		len = PAGE_ALIGN(len);
		if (!len || addr > ULONG_MAX - len)
			return -EINVAL;
		end = addr + len;

		overlap_count = vma_range_count_overlaps(mm, addr, end, &replace_vma);
		if (overlap_count < 0)
			return overlap_count;

		if (flags & MAP_FIXED_NOREPLACE) {
			if (overlap_count)
				return -EEXIST;
		} else if (overlap_count > 1) {
			return swmmu_mmap_fixed_range_replace(
				mm, space, addr, end,
				prot, vma_flags, pgoff);
		}

		if (overlap_count == 1) {
			if (!replace_vma->vm_swmmu_data)
				return -EOPNOTSUPP;

			if (replace_vma->vm_start == addr &&
				replace_vma->vm_end == end)
				kind = NOMMU_SWMMU_REPLACE_EXACT;
			else if (replace_vma->vm_start == addr &&
				end < replace_vma->vm_end)
				kind = NOMMU_SWMMU_REPLACE_HEAD;
			else if (addr > replace_vma->vm_start &&
				replace_vma->vm_end == end)
				kind = NOMMU_SWMMU_REPLACE_TAIL;
			else if (addr > replace_vma->vm_start &&
				end < replace_vma->vm_end)
				kind = NOMMU_SWMMU_REPLACE_MIDDLE;
			else
				return -EINVAL;

			if (kind == NOMMU_SWMMU_REPLACE_MIDDLE)
				return swmmu_mmap_fixed_middle_replace(
					mm, space, replace_vma,
					addr, end, prot,
					vma_flags, pgoff);
			else if (kind != NOMMU_SWMMU_REPLACE_EXACT)
				return swmmu_mmap_fixed_replace(mm, kind,
								space, replace_vma, addr, end,
								prot, vma_flags, pgoff);
		}
		start = addr;
	}
	else if (addr != 0) {
		ret = swmmu_find_free_range(mm, page_align(addr), len, &start);
		if (ret)
			ret = swmmu_find_free_range(mm, SWMMU_VA_BASE, len, &start);
		if (ret)
			return ret;
	}
	else {
		ret = swmmu_find_free_range(mm, SWMMU_VA_BASE, len, &start);
		if (ret)
			return ret;
	}
	end = start + PAGE_ALIGN(len);

	backing = swmmu_backing_alloc(space, end - start);
	if (!backing)
		return -ENOMEM;

	vma = swmmu_fixed_alloc_vma(mm, NULL, start, end,
					vma_flags, pgoff);
	if (!vma) {
		swmmu_backing_release(space, backing);
		return -ENOMEM;
	}

	swmmu_vma = swmmu_vma_data_create(space, backing);
	if (!swmmu_vma) {
		vm_area_free(vma);
		swmmu_backing_release(space, backing);
		return -ENOMEM;
	}

	access = swmmu_access_from_prot(prot);
	swmmu_vma->access = access;
	vma->vm_swmmu_data = swmmu_vma;

	vma_iter_config(&vmi, start, end);
	ret = vma_iter_prealloc(&vmi, vma);
	if (ret)
		goto rollback_new_mapping;

	if (replace_vma) {
		vma_start_write(replace_vma);
		vma_start_write(vma);
	}

	vma_iter_store_new(&vmi, vma);

	/* MAP_FIXED with exact replacement */
	if (replace_vma) {
		vma_mark_detached(replace_vma);
		mm->map_count--;

		nommu_swmmu_vma_close(mm, replace_vma);
		vma_close(replace_vma);

		if (replace_vma->vm_file)
			fput(replace_vma->vm_file);

		vm_area_free(replace_vma);
	}

	mm->map_count++;
	return start;

rollback_new_mapping:
	vma->vm_swmmu_data = NULL;
	swmmu_vma_data_free(space, swmmu_vma);
	vm_area_free(vma);
	swmmu_backing_release(space, backing);
	return ret;
}

/*
 * currently, file, prot, populate, uf are not used and ignored.
 * will be updated.
 */
static unsigned long do_mmap_swmmu(struct file *file,
			unsigned long addr,
			unsigned long len,
			unsigned long prot,
			unsigned long flags,
			vma_flags_t vma_flags,
			unsigned long pgoff,
			unsigned long *populate,
			struct list_head *uf)
{
	if (!current->mm)
		return -EINVAL;

	return __do_mmap_swmmu(current->mm, file, addr, len,
			       prot, flags, vma_flags,
			       pgoff, populate, uf);
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
unsigned long nommu_swmmu_kunit_mmap_mm(
	struct mm_struct *mm,
	unsigned long addr,
	unsigned long len,
	unsigned long prot,
	unsigned long flags)
{
	unsigned long populate = 0;
	unsigned long ret;

	if (!mm || !len)
		return -EINVAL;

	mmap_write_lock(mm);

	ret = __do_mmap_swmmu(mm, NULL, addr, len,
			      prot, flags,
			      EMPTY_VMA_FLAGS, 0,
			      &populate, NULL);

	mmap_write_unlock(mm);

	return ret;
}

int nommu_swmmu_kunit_unmap_mm(struct mm_struct *mm,
			       unsigned long address,
			       size_t size)
{
	int ret;

	if (!mm || !size)
		return -EINVAL;

	mmap_write_lock(mm);
	ret = do_munmap(mm, address, size, NULL);
	mmap_write_unlock(mm);

	return ret;
}
#endif

static int nommu_swmmu_unmap_shared_range(struct mm_struct *mm,
					  struct vma_iterator *vmi,
					  struct vm_area_struct *vma,
					  unsigned long start,
					  unsigned long end,
					  struct list_head *uf)
{
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	struct vma_munmap_struct vms;
	int ret;

	mt_init_flags(&mt_detach,
		      vmi->mas.tree->ma_flags &
		      MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	vma_init_munmap(&vms, vmi, vma, start, end,
			uf, false,
			&nommu_swmmu_vma_backend_ops);

	ret = vma_gather_range(&vms, &mas_detach);
	if (ret)
		goto out_destroy;

	ret = vma_iter_clear_gfp(vmi, start, end, GFP_KERNEL);
	if (ret) {
		vma_reattach_vmas(&mas_detach);
		goto out_destroy;
	}

	mm->map_count -= vms.vma_count;

	vma_remove_detached(&vms, &mas_detach, mm,
			    nommu_swmmu_remove_detached_vma);

out_destroy:
	__mt_destroy(&mt_detach);

	validate_mm(mm);
	nommu_swmmu_validate(mm);

	return ret;
}

static int
nommu_swmmu_validate_mmap_request(struct file *file,
				  unsigned long addr,
				  unsigned long len,
				  unsigned long prot,
				  unsigned long flags,
				  vma_flags_t vma_flags,
				  unsigned long pgoff,
				  unsigned long *populate,
				  struct list_head *uf)
{
	if (file ||
	    !(flags & MAP_ANONYMOUS) ||
	    !(flags & MAP_PRIVATE))
		return -EOPNOTSUPP;

	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;

	if (prot == PROT_WRITE ||
		prot == PROT_EXEC ||
		prot == (PROT_WRITE | PROT_EXEC))
		return -EOPNOTSUPP;

	return 0;
}

unsigned long do_mmap(struct file *file,
			unsigned long addr,
			unsigned long len,
			unsigned long prot,
			unsigned long flags,
			vma_flags_t vma_flags,
			unsigned long pgoff,
			unsigned long *populate,
			struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (nommu_swmmu_get_mode(mm) == NOMMU_SWMMU_ON) {
		ret = nommu_swmmu_validate_mmap_request(
			file, addr, len, prot, flags,
			vma_flags, pgoff, populate, uf);

		if (ret == 0)
			return do_mmap_swmmu(file, addr, len, prot, flags,
					     vma_flags, pgoff,
					     populate, uf);

		/* EOPNOTSUPP fall back to nommu backend */
		if (ret != -EOPNOTSUPP)
			return ret;
	}

	return do_mmap_nommu(file, addr, len, prot, flags,
			     vma_flags, pgoff, populate, uf);
}

int do_munmap(struct mm_struct *mm,
	unsigned long start, size_t len, struct list_head *uf)
{
	VMA_ITERATOR(vmi, mm, start);
	struct vm_area_struct *vma;
	unsigned long end;

	len = PAGE_ALIGN(len);
	if (len == 0)
		return -EINVAL;

	if (len > ULONG_MAX - start)
		return -EINVAL;

	end = start + len;

	vma = vma_find(&vmi, end);
	if (vma && vma->vm_swmmu_data) {
		return nommu_swmmu_unmap_shared_range(
			mm, &vmi, vma, start, end, uf);
	}

	return do_munmap_nommu(mm, start, len, uf);
}

static unsigned long
__do_mremap(struct mm_struct *mm,
	    unsigned long addr,
	    unsigned long old_len,
	    unsigned long new_len,
	    unsigned long flags,
	    unsigned long new_addr)
{
	VMA_ITERATOR(vmi, mm, addr);
	struct vm_area_struct *vma;
	struct nommu_swmmu_vma_tx vma_tx;
	struct vm_area_struct *next;
	int ret;
	unsigned long end;

	mmap_assert_write_locked(mm);

	/* not implemented yet */
	if (new_addr)
		return -EINVAL;

	old_len = PAGE_ALIGN(old_len);
	if (old_len == 0)
		return -EINVAL;

	if (old_len > ULONG_MAX - addr)
		return -EINVAL;

	end = addr + old_len;
	vma = vma_find(&vmi, end);

	if (!vma || !vma->vm_swmmu_data)
		return do_mremap_nommu(addr, old_len, new_len,
				flags, new_addr);

	/*
	 * The current SWMMU implementation only supports remapping
	 * a complete VMA in place.
	 */
	if (vma->vm_start != addr ||
		end != vma->vm_end)
		return -EINVAL;

	if (flags & (MREMAP_MAYMOVE | MREMAP_FIXED))
		return -EINVAL;

	next = vma_next(&vmi);

	if (new_len > ULONG_MAX - (PAGE_SIZE - 1))
		return -EINVAL;

	new_len = PAGE_ALIGN(new_len);
	if (!new_len)
		return -EINVAL;

	if (new_len == old_len)
		return vma->vm_start;

	if (new_len < old_len) {
		/* shrink */
		ret = swmmu_resize_prepare(mm->swmmu_space,
					vma->vm_swmmu_data,
					new_len,
					&vma_tx);

		if (ret)
			return ret;

		ret = vma_shrink(&vmi, vma, vma->vm_start + new_len);
		if (ret) {
			swmmu_resize_abort(mm->swmmu_space, &vma_tx);
			return ret;
		}

		down_write(&mm->swmmu_space->lock);
		swmmu_resize_commit(mm->swmmu_space, &vma_tx);
		up_write(&mm->swmmu_space->lock);
	} else {
		/* growth */
		struct vma_merge_struct vmg = {
			.mm = mm,
			.vmi = &vmi,
			.start = vma->vm_start,
			.end = vma->vm_start + new_len,
			.target = vma,
			.next = next,
			.just_expand = true,
		};

		ret = vma_expand(&vmg);
		if (ret)
			return ret;
	}

	validate_mm(mm);
	nommu_swmmu_validate(mm);

	return vma->vm_start;
}

unsigned long do_mremap(unsigned long addr,
			unsigned long old_len, unsigned long new_len,
			unsigned long flags, unsigned long new_addr)
{
	if (!current->mm)
		return -EINVAL;

	return __do_mremap(current->mm, addr, old_len, new_len,
			   flags, new_addr);
}

#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
unsigned long nommu_swmmu_kunit_mremap_mm(struct mm_struct *mm,
					unsigned long addr,
					unsigned long old_len,
					unsigned long new_len,
					unsigned long flags,
					unsigned long new_addr)
{
	unsigned long ret;

	if (!mm)
		return -EINVAL;

	mmap_write_lock(mm);

	ret = __do_mremap(mm, addr, old_len, new_len,
			  flags, new_addr);

	mmap_write_unlock(mm);

	return ret;
}
#endif

/* prctl interface */
int nommu_swmmu_set_mode(struct mm_struct *mm,
			 unsigned long mode)
{
	struct nommu_swmmu_space *space;
	bool empty;

	if (!mm)
		return -EINVAL;

	if (SWMMU_VA_BASE >= TASK_SIZE)
		BUG();

	if (mode != NOMMU_SWMMU_OFF &&
	    mode != NOMMU_SWMMU_ON)
		return -EINVAL;

	mmap_write_lock(mm);

	space = mm->swmmu_space;
	if (!space) {
		mmap_write_unlock(mm);
		return -EINVAL;
	}

	down_read(&space->lock);
	empty = nommu_swmmu_space_empty(space);
	up_read(&space->lock);

	/*
	 * Do not disable while SWMMU mappings still exist.
	 * Otherwise existing pointers would change meaning.
	 */
	if (mode == NOMMU_SWMMU_OFF && !empty) {
		mmap_write_unlock(mm);
		return -EBUSY;
	}

	WRITE_ONCE(mm->swmmu_mode, mode);

	mmap_write_unlock(mm);
	return 0;
}

long nommu_swmmu_get_mode(struct mm_struct *mm)
{
	if (!mm)
		return -EINVAL;

	return READ_ONCE(mm->swmmu_mode);
}

SYSCALL_DEFINE1(nommu_swmmu_alloc, size_t, size)
{
	struct mm_struct *mm = current->mm;

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		return -EOPNOTSUPP;

	return vm_mmap(NULL, 0, size,
		       PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS,
		       0);
}

SYSCALL_DEFINE1(nommu_swmmu_free, void __user *, address)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start = (unsigned long)address;
	unsigned long length;
	int ret;

	if (!mm || mm->swmmu_mode != NOMMU_SWMMU_ON)
		return -EOPNOTSUPP;

	mmap_write_lock(mm);

	vma = find_vma(mm, start);
	if (!vma ||
		vma->vm_start != start ||
		!vma->vm_swmmu_data) {
		ret = -EINVAL;
		goto out;
	}

	length = vma->vm_end - vma->vm_start;
	ret = do_munmap(mm, start, length, NULL);

out:
	mmap_write_unlock(mm);
	return ret;
}

SYSCALL_DEFINE3(nommu_swmmu_load,
		void __user *, address,
		size_t, size,
		u64 __user *, result)
{
	u64 value;
	int ret;

	ret = nommu_swmmu_load_u64_checked(address, size, &value);
	if (ret)
		return ret;

	if (copy_to_user(result, &value, sizeof(value)))
		return -EFAULT;

	return 0;
}

SYSCALL_DEFINE3(nommu_swmmu_store, void __user *, address, size_t, size, uint64_t, value)
{
	return nommu_swmmu_store_u64_checked(address, size, value);
}

SYSCALL_DEFINE3(nommu_swmmu_remap,
		void __user *, address,
		size_t, old_size,
		size_t, new_size)
{
	struct nommu_swmmu_space *space;

	space = nommu_swmmu_current();
	if (!space)
		return -EINVAL;

	return nommu_swmmu_remap(space,
				(unsigned long)address,
				old_size,
				new_size);
}
