#include <linux/nommu_swmmu.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <linux/mman.h>

#include "internal.h"

#define SWMMU_VA_BASE ((uintptr_t)0x1000000000ULL)

struct swmmu_pte {
	struct page *page;
	unsigned int flags;
};

struct swmmu_pagetable {
	refcount_t refs;
	unsigned long nr_ptes;
	struct swmmu_pte *ptes;
};

struct swmmu_pagetable_range {
	struct swmmu_pagetable *pagetable;
	unsigned long first;
	unsigned long nr_ptes;
	unsigned int access;
};

enum swmmu_pagetable_tx_kind {
	SWMMU_TX_RESIZE,
	SWMMU_TX_SPLIT,
	SWMMU_TX_FIXED_REPLACE,
	SWMMU_TX_DROP_RANGE,
	SWMMU_TX_MOVE,
};

enum swmmu_pagetable_replace_kind {
	SWMMU_REPLACE_EXACT,
	SWMMU_REPLACE_HEAD,
	SWMMU_REPLACE_TAIL,
	SWMMU_REPLACE_MIDDLE,
};


struct swmmu_pagetable_tx {
	enum swmmu_pagetable_tx_kind kind;
	enum swmmu_pagetable_replace_kind replace_kind;

	struct vm_area_struct *old_vma;
	struct vm_area_struct *new_vma;
	struct vm_area_struct *right_vma;

	struct swmmu_pagetable_range *source;
	struct swmmu_pagetable_range *new_range;
	struct swmmu_pagetable_range *left_range;
	struct swmmu_pagetable_range *right_range;
	struct swmmu_pagetable_range *replacement_range;

	unsigned long replace_start;
	unsigned long replace_end;
	unsigned int replacement_access;
	unsigned long split_nr_ptes;
	struct swmmu_pagetable_range *drop_range;
	unsigned long drop_first;
	unsigned long drop_nr_ptes;
	struct swmmu_pte *drop_saved;
	bool move_reuses_pagetable;
};


struct nommu_swmmu_clone_item {
	struct list_head node;
	struct vm_area_struct *src_vma;
	struct vm_area_struct *dst_vma;
	struct swmmu_pagetable_range *new_data;
	bool published;
};

struct nommu_swmmu_space {
	struct rw_semaphore lock;
	struct mm_struct *mm;
	refcount_t users;
	const struct nommu_swmmu_mem_ops *ops;
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
static struct swmmu_pagetable *swmmu_pagetable_alloc(struct nommu_swmmu_space *space,
						unsigned long length)
{
	struct swmmu_pagetable *pt;
	size_t i;
	size_t allocated = 0;

	length = PAGE_ALIGN(length);
	if (!length)
		return NULL;

	pt = space->ops->zalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt)
		return NULL;

	refcount_set(&pt->refs, 1);
	pt->nr_ptes = length / SWMMU_PAGE_SIZE;

	pt->ptes = space->ops->zalloc(
		pt->nr_ptes * sizeof(*pt->ptes),
		GFP_KERNEL);
	if (!pt->ptes) {
		space->ops->dealloc(pt);
		return NULL;
	}

	for (i = 0; i < pt->nr_ptes; i++) {
		pt->ptes[i].page = space->ops->page_alloc(GFP_KERNEL);
		if (!pt->ptes[i].page)
			goto fail_pages;

		clear_highpage(pt->ptes[i].page);
		allocated++;
	}

	return pt;

fail_pages:
	while (allocated)
		space->ops->page_free(pt->ptes[--allocated].page);

	space->ops->dealloc(pt->ptes);
	space->ops->dealloc(pt);
	return NULL;
}

/* XXX: correct only for whole-pagetable ownership */
static void swmmu_pagetable_put(struct nommu_swmmu_space *space,
				struct swmmu_pagetable *pt)
{
	size_t i;

	if (!space || !pt)
		return;

	if (!refcount_dec_and_test(&pt->refs))
		return;

	for (i = 0; i < pt->nr_ptes; i++) {
		if (pt->ptes[i].page)
			space->ops->page_free(pt->ptes[i].page);
	}

	space->ops->dealloc(pt->ptes);
	space->ops->dealloc(pt);
}

static void swmmu_pagetable_get(struct swmmu_pagetable *pt)
{
	refcount_inc(&pt->refs);
}

static void swmmu_pagetable_drop_entries(struct nommu_swmmu_space *space,
					struct swmmu_pagetable *pt,
					unsigned long first,
					unsigned long nr_ptes)
{
	unsigned long i;

	lockdep_assert_held_write(&space->lock);

	for (i = 0; i < nr_ptes; i++) {
		struct swmmu_pte *pte = &pt->ptes[first + i];

		if (pte->page) {
			space->ops->page_free(pte->page);
			pte->page = NULL;
		}

		pte->flags = 0;
	}
}

static struct swmmu_pagetable_range *swmmu_pagetable_range_create(
	struct nommu_swmmu_space *space,
	struct swmmu_pagetable *pt)
{
	struct swmmu_pagetable_range *pt_range;

	if (!space || !pt)
		return NULL;

	pt_range = space->ops->zalloc(sizeof(*pt_range), GFP_KERNEL);
	if (!pt_range)
		return NULL;

	swmmu_pagetable_get(pt);

	pt_range->pagetable = pt;
	pt_range->first = 0;
	pt_range->nr_ptes = pt->nr_ptes;
	pt_range->access = NOMMU_SWMMU_NONE;

	return pt_range;
}

static void swmmu_pagetable_range_free(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_range *data)
{
	if (!space || !data)
		return;

	space->ops->dealloc(data);
}

static void swmmu_pagetable_range_release_locked(struct nommu_swmmu_space *space,
						struct swmmu_pagetable_range *range)
{
	struct swmmu_pagetable *pt;

	if (!space || !range)
		return;

	lockdep_assert_held_write(&space->lock);

	pt = range->pagetable;
	if (pt && range->nr_ptes)
		swmmu_pagetable_drop_entries(
			space, pt, range->first, range->nr_ptes);

	range->first = 0;
	range->nr_ptes = 0;
	range->pagetable = NULL;

	if (pt)
		swmmu_pagetable_put(space, pt);

	swmmu_pagetable_range_free(space, range);
}

static void swmmu_pagetable_range_release(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_range *range)
{
	if (!space || !range)
		return;

	down_write(&space->lock);
	swmmu_pagetable_range_release_locked(space, range);
	up_write(&space->lock);
}

static void swmmu_pagetable_range_put_locked(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_range *range)
{
	struct swmmu_pagetable *pt;

	if (!space || !range)
		return;

	lockdep_assert_held_write(&space->lock);

	pt = range->pagetable;

	range->pagetable = NULL;
	range->first = 0;
	range->nr_ptes = 0;

	if (pt)
		swmmu_pagetable_put(space, pt);

	swmmu_pagetable_range_free(space, range);
}

static void swmmu_pagetable_range_put(
	struct nommu_swmmu_space *space,
	struct swmmu_pagetable_range *range)
{
	if (!space || !range)
		return;

	down_write(&space->lock);
	swmmu_pagetable_range_put_locked(space, range);
	up_write(&space->lock);
}

static struct swmmu_pagetable_range *swmmu_pagetable_range_clone(
	struct nommu_swmmu_space *dst_space,
	const struct swmmu_pagetable_range *src)
{
	struct swmmu_pagetable_range *dst;
	size_t i;
	int ret;

	if (!dst_space || !src || !src->pagetable ||
		src->nr_ptes > src->pagetable->nr_ptes ||
		src->nr_ptes >
		src->pagetable->nr_ptes - src->first)
		return NULL;

	lockdep_assert_held_write(&dst_space->lock);

	dst = dst_space->ops->zalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->pagetable = swmmu_pagetable_alloc(dst_space,
					src->nr_ptes * SWMMU_PAGE_SIZE);
	if (!dst->pagetable) {
		dst_space->ops->dealloc(dst);
		return NULL;
	}

	dst->first = 0;
	dst->nr_ptes = src->nr_ptes;
	dst->access = src->access;

	for (i = 0; i < src->nr_ptes; i++) {
		ret = dst_space->ops->page_copy(
			dst->pagetable->ptes[i].page,
			src->pagetable->ptes[src->first + i].page);
		if (ret) {
			swmmu_pagetable_range_release_locked(dst_space, dst);
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
		vma->vm_swmmu_pt_range = NULL;
}

void nommu_swmmu_vma_close(struct mm_struct *mm,
			   struct vm_area_struct *vma)
{
	struct nommu_swmmu_space *space;
	struct swmmu_pagetable_range *range;

	if (!mm || !vma)
		return;

	range = vma->vm_swmmu_pt_range;
	if (!range)
		return;

	space = mm->swmmu_space;
	vma->vm_swmmu_pt_range = NULL;

	swmmu_pagetable_range_release(space, range);
}

static struct swmmu_pagetable_range *swmmu_pagetable_range_share(
	struct nommu_swmmu_space *space,
	const struct swmmu_pagetable_range *source,
	unsigned long first,
	unsigned long nr_ptes)
{
	struct swmmu_pagetable_range *range;

	if (!space || !source || !source->pagetable || !nr_ptes)
		return NULL;

	if (first < source->first ||
	    nr_ptes > source->nr_ptes -
		       (first - source->first))
		return NULL;

	range = space->ops->zalloc(sizeof(*range), GFP_KERNEL);
	if (!range)
		return NULL;

	swmmu_pagetable_get(source->pagetable);

	range->pagetable = source->pagetable;
	range->first = first;
	range->nr_ptes = nr_ptes;
	range->access = source->access;

	return range;
}

static void nommu_swmmu_clone_item_abort(struct mm_struct *dst,
					struct nommu_swmmu_space *dst_space,
					struct nommu_swmmu_clone_item *item)
{
	struct vm_area_struct *removed;
	VMA_ITERATOR(vmi, dst, 0);

	if (!item->published) {
		swmmu_pagetable_range_release_locked(dst_space, item->new_data);
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
		if (vma->vm_swmmu_pt_range)
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

int nommu_swmmu_kunit_pagetable_alloc(
	struct nommu_swmmu_space *space,
	size_t size,
	struct swmmu_pagetable **out)
{
	if (!space || !out || !size)
		return -EINVAL;

	*out = swmmu_pagetable_alloc(space, size);
	if (!*out)
		return -ENOMEM;

	return 0;
}

void nommu_swmmu_kunit_pagetable_release(
	struct nommu_swmmu_space *space,
	struct swmmu_pagetable *pt)
{
	swmmu_pagetable_put(space, pt);
}

int nommu_swmmu_kunit_pagetable_range_clone(
	struct nommu_swmmu_space *space,
	const struct swmmu_pagetable_range *src,
	struct swmmu_pagetable_range **out)
{
	if (!space || !src || !out)
		return -EINVAL;

	down_write(&space->lock);
	*out = swmmu_pagetable_range_clone(space, src);
	up_write(&space->lock);
	if (!*out)
		return -ENOMEM;

	return 0;
}

void nommu_swmmu_kunit_pagetable_range_release(
	struct nommu_swmmu_space *space,
	struct swmmu_pagetable_range *data)
{
	swmmu_pagetable_range_release(space, data);
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
		struct swmmu_pagetable_range *data;

		if (!vma->vm_swmmu_pt_range)
			continue;

		data = vma->vm_swmmu_pt_range;

		VM_BUG_ON_MM(!data->pagetable, mm);
		VM_BUG_ON_MM(!data->nr_ptes, mm);
		VM_BUG_ON_MM(
			vma->vm_end - vma->vm_start !=
			data->nr_ptes * SWMMU_PAGE_SIZE,
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
	struct swmmu_pagetable_range *swmmu_vma;
	struct swmmu_pagetable *pt;
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
		!vma->vm_swmmu_pt_range ||
		address < vma->vm_start ||
		size > vma->vm_end - address) {
		*status = -EFAULT;
		return NULL;
	}

	swmmu_vma = vma->vm_swmmu_pt_range;
	pt = swmmu_vma->pagetable;

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
	page_index = swmmu_vma->first +
		vma_offset / SWMMU_PAGE_SIZE;
	page_offset = vma_offset % SWMMU_PAGE_SIZE;

	if (page_index >= pt->nr_ptes ||
		page_offset + size > SWMMU_PAGE_SIZE) {
		*status = -EINVAL;
		return NULL;
	}

	return page_address(pt->ptes[page_index].page) + page_offset;
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

const void *nommu_swmmu_kunit_range_pagetable(
	const struct swmmu_pagetable_range *range)
{
	return range ? range->pagetable : NULL;
}

unsigned long nommu_swmmu_kunit_range_first(
	const struct swmmu_pagetable_range *range)
{
	return range ? range->first : 0;
}

unsigned long nommu_swmmu_kunit_range_nr_ptes(
	const struct swmmu_pagetable_range *range)
{
	return range ? range->nr_ptes : 0;
}

unsigned int nommu_swmmu_kunit_range_access(
	const struct swmmu_pagetable_range *range)
{
	return range ? range->access : NOMMU_SWMMU_NONE;
}

unsigned long nommu_swmmu_kunit_pagetable_nr_ptes(
	const struct swmmu_pagetable *pt)
{
	return pt ? pt->nr_ptes : 0;
}

struct page *nommu_swmmu_kunit_pagetable_page(
	const struct swmmu_pagetable *pagetable,
	unsigned long index)
{
	if (!pagetable || index >= pagetable->nr_ptes)
		return NULL;

	return pagetable->ptes[index].page;
}

int nommu_swmmu_kunit_range_create(
	struct nommu_swmmu_space *space,
	struct swmmu_pagetable *pagetable,
	unsigned long first,
	unsigned long nr_ptes,
	unsigned int access,
	struct swmmu_pagetable_range **out)
{
	struct swmmu_pagetable_range *pt_range;

	pt_range = swmmu_pagetable_range_create(space, pagetable);
	if (!pt_range)
		return -1;
	pt_range->first = first;
	pt_range->nr_ptes = nr_ptes;
	pt_range->access = access;

	*out = pt_range;
	return 0;
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
		struct swmmu_pagetable_range *src_data;

		src_data = src_vma->vm_swmmu_pt_range;
		if (!src_data)
			continue;

		if (!src_data->pagetable ||
			src_data->first > src_data->pagetable->nr_ptes ||
			src_data->nr_ptes >
			src_data->pagetable->nr_ptes - src_data->first) {
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
		item->dst_vma->vm_swmmu_pt_range = NULL;

		/* exclude generic-nommu region */
		if (item->dst_vma->vm_region) {
			vm_area_free(item->dst_vma);
			dst_space->ops->dealloc(item);
			ret = -EOPNOTSUPP;
			goto rollback;
		}

		item->new_data = swmmu_pagetable_range_clone(dst_space, src_data);
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

		item->dst_vma->vm_swmmu_pt_range = item->new_data;
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

static void swmmu_resize_abort(struct nommu_swmmu_space *space,
			struct swmmu_pagetable_tx *tx)
{
	if (!space || !tx)
		return;

	if (tx->new_range) {
		swmmu_pagetable_range_release(
			space, tx->new_range);
		tx->new_range = NULL;
	}

	tx->source = NULL;
	tx->kind = 0;
}

static void swmmu_resize_commit(struct nommu_swmmu_space *space,
				struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable_range *source;
	struct swmmu_pagetable_range *new_range;
	struct swmmu_pagetable *old_pt;
	struct swmmu_pagetable *new_pt;

	if (!space || !tx || tx->kind != SWMMU_TX_RESIZE ||
	    !tx->source || !tx->new_range)
		return;

	source = tx->source;
	new_range = tx->new_range;
	old_pt = source->pagetable;
	new_pt = new_range->pagetable;

	source->pagetable = new_pt;
	source->first = new_range->first;
	source->nr_ptes = new_range->nr_ptes;
	source->access = new_range->access;

	new_range->pagetable = NULL;
	swmmu_pagetable_range_free(space, new_range);

	tx->source = NULL;
	tx->new_range = NULL;
	tx->kind = 0;

	swmmu_pagetable_put(space, old_pt);
}

static int swmmu_pagetable_drop_range_prepare(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_range *range,
					unsigned long first,
					unsigned long nr_ptes,
					struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable *pt;
	size_t bytes;

	if (!range || !range->pagetable || !tx || !nr_ptes)
		return -EINVAL;

	pt = range->pagetable;

	if (first != range->first ||
	    nr_ptes != range->nr_ptes)
		return -EINVAL;

	if (first > pt->nr_ptes ||
	    nr_ptes > pt->nr_ptes - first)
		return -EINVAL;

	if (nr_ptes > SIZE_MAX / sizeof(*tx->drop_saved))
		return -EOVERFLOW;

	bytes = nr_ptes * sizeof(*tx->drop_saved);

	memset(tx, 0, sizeof(*tx));

	tx->drop_saved = space->ops->zalloc(bytes, GFP_KERNEL);
	if (!tx->drop_saved)
		return -ENOMEM;

	memcpy(tx->drop_saved, &pt->ptes[first], bytes);

	tx->kind = SWMMU_TX_DROP_RANGE;
	tx->drop_range = range;
	tx->drop_first = first;
	tx->drop_nr_ptes = nr_ptes;

	return 0;
}

static void swmmu_pagetable_drop_range_commit(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable_range *range;

	if (!space || !tx ||
	    tx->kind != SWMMU_TX_DROP_RANGE ||
	    !tx->drop_range)
		return;

	range = tx->drop_range;

	swmmu_pagetable_drop_entries(
		space, range->pagetable,
		tx->drop_first,  tx->drop_nr_ptes);

	/*
	 * This first implementation requires the dropped range to be
	 * exactly represented by @range.
	 */
	range->first = 0;
	range->nr_ptes = 0;

	if (tx->drop_saved) {
		space->ops->dealloc(tx->drop_saved);
		tx->drop_saved = NULL;
	}
	tx->drop_range = NULL;
	tx->drop_nr_ptes = 0;
	tx->kind = 0;
}

static void swmmu_pagetable_drop_range_abort(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_tx *tx)
{
	if (!tx || tx->kind != SWMMU_TX_DROP_RANGE)
		return;

	if (tx->drop_saved) {
		space->ops->dealloc(tx->drop_saved);
		tx->drop_saved = NULL;
	}

	tx->drop_saved = NULL;
	tx->drop_range = NULL;
	tx->drop_nr_ptes = 0;
	tx->kind = 0;
}

static int swmmu_pagetable_resize_prepare(struct nommu_swmmu_space *space,
					struct swmmu_pagetable_range *source,
					size_t new_size,
					struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable_range *new_range;
	struct swmmu_pagetable *old_pt;
	struct swmmu_pagetable *new_pt;
	size_t old_nr_ptes;
	size_t new_nr_ptes;
	size_t copy_count;
	size_t i;
	int ret;

	if (!space || !source || !source->pagetable ||
	    !tx || !new_size)
		return -EINVAL;

	if (!IS_ALIGNED(new_size, SWMMU_PAGE_SIZE))
		return -EINVAL;

	old_pt = source->pagetable;
	old_nr_ptes = source->nr_ptes;
	new_nr_ptes = new_size / SWMMU_PAGE_SIZE;

	if (!old_nr_ptes || !new_nr_ptes)
		return -EINVAL;

	if (source->first > old_pt->nr_ptes ||
	    old_nr_ptes > old_pt->nr_ptes - source->first)
		return -EINVAL;

	if (new_nr_ptes == old_nr_ptes)
		return -EINVAL;

	memset(tx, 0, sizeof(*tx));

	new_pt = swmmu_pagetable_alloc(
		space, new_nr_ptes * SWMMU_PAGE_SIZE);
	if (!new_pt)
		return -ENOMEM;

	new_range = swmmu_pagetable_range_create(space, new_pt);
	if (!new_range) {
		swmmu_pagetable_put(space, new_pt);
		return -ENOMEM;
	}

	new_range->first = 0;
	new_range->nr_ptes = new_nr_ptes;
	new_range->access = source->access;

	copy_count = min(old_nr_ptes, new_nr_ptes);

	for (i = 0; i < copy_count; i++) {
		ret = space->ops->page_copy(
			new_pt->ptes[i].page,
			old_pt->ptes[source->first + i].page);
		if (ret) {
			swmmu_pagetable_range_release(space, new_range);
			return ret;
		}
	}

	tx->kind = SWMMU_TX_RESIZE;
	tx->source = source;
	tx->new_range = new_range;

	return 0;
}

static int swmmu_expand_prepare(struct vm_area_struct *vma,
				unsigned long new_end,
				void **state)
{
	struct swmmu_pagetable_tx *tx;
	struct nommu_swmmu_space *space;
	size_t new_size;
	int ret;

	if (!vma || !vma->vm_swmmu_pt_range ||
	    new_end <= vma->vm_start)
		return -EINVAL;

	space = vma->vm_mm->swmmu_space;
	if (!space)
		return -EINVAL;

	new_size = new_end - vma->vm_start;

	down_write(&space->lock);

	tx = kzalloc(sizeof(*tx), GFP_KERNEL);
	if (!tx) {
		up_write(&space->lock);
		return -ENOMEM;
	}

	ret = swmmu_pagetable_resize_prepare(
		space,
		vma->vm_swmmu_pt_range,
		new_size,
		tx);
	if (ret) {
		kfree(tx);
		up_write(&space->lock);
		return ret;
	}

	*state = tx;
	return 0;
}

static void swmmu_expand_commit(struct vm_area_struct *vma,
				void *state)
{
	struct swmmu_pagetable_tx *tx = state;
	struct nommu_swmmu_space *space;

	if (!tx)
		return;

	space = vma->vm_mm->swmmu_space;

	swmmu_resize_commit(space, tx);
	kfree(tx);
	up_write(&space->lock);
}

static void swmmu_expand_abort(struct vm_area_struct *vma,
			       void *state)
{
	struct swmmu_pagetable_tx *tx = state;
	struct nommu_swmmu_space *space;

	if (!tx)
		return;

	space = vma->vm_mm->swmmu_space;

	swmmu_resize_abort(space, tx);
	kfree(tx);
	up_write(&space->lock);
}

/* old helpers */


static int swmmu_split_prepare(struct vm_area_struct *vma,
			struct vm_area_struct *new,
			unsigned long addr,
			bool new_below,
			void **state)
{
	struct nommu_swmmu_space *space = vma->vm_mm->swmmu_space;
	struct swmmu_pagetable_range *old_pt_range;
	struct swmmu_pagetable_range *new_pt_range;
	struct swmmu_pagetable_tx *tx;
	unsigned long split_pages;

	old_pt_range = vma->vm_swmmu_pt_range;
	if (!old_pt_range || !old_pt_range->pagetable)
		return -EINVAL;

	if (addr <= vma->vm_start || addr >= vma->vm_end)
		return -EINVAL;

	if (!IS_ALIGNED(addr - vma->vm_start, SWMMU_PAGE_SIZE))
		return -EINVAL;

	split_pages = (addr - vma->vm_start) / SWMMU_PAGE_SIZE;

	if (!split_pages || split_pages >= old_pt_range->nr_ptes)
		return -EINVAL;

	if (old_pt_range->first >
	    old_pt_range->pagetable->nr_ptes)
		return -EINVAL;

	if (old_pt_range->nr_ptes >
	    old_pt_range->pagetable->nr_ptes - old_pt_range->first)
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

	new_pt_range = space->ops->zalloc(sizeof(*new_pt_range), GFP_KERNEL);
	if (!new_pt_range) {
		space->ops->dealloc(tx);
		return -ENOMEM;
	}

	tx->kind = SWMMU_TX_SPLIT;
	tx->source = old_pt_range;
	tx->new_range = new_pt_range;
	tx->source->pagetable = old_pt_range->pagetable;
	tx->source->nr_ptes = old_pt_range->nr_ptes;
	tx->source->first = old_pt_range->first;
	tx->split_nr_ptes = split_pages;

	/*
	 * Both VMAs refer to the same immutable pagetable object.
	 */
	swmmu_pagetable_get(tx->source->pagetable);

	new_pt_range->pagetable = tx->source->pagetable;
	new_pt_range->access = old_pt_range->access;

	if (new_below) {
		new_pt_range->first = old_pt_range->first;
		new_pt_range->nr_ptes = split_pages;
	} else {
		new_pt_range->first =
			old_pt_range->first + split_pages;
		new_pt_range->nr_ptes =
			old_pt_range->nr_ptes - split_pages;
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
	struct swmmu_pagetable_tx *tx = state;

	if (new_below) {
		tx->source->first += tx->split_nr_ptes;
		tx->source->nr_ptes -= tx->split_nr_ptes;
	} else {
		tx->source->nr_ptes = tx->split_nr_ptes;
	}

	new->vm_swmmu_pt_range = tx->new_range;
	tx->new_range = NULL;

	space->ops->dealloc(tx);
}

static void swmmu_split_abort(struct vm_area_struct *vma,
			struct vm_area_struct *new,
			void *state)
{
	struct nommu_swmmu_space *space = vma->vm_mm->swmmu_space;
	struct swmmu_pagetable_tx *tx = state;

	if (!tx)
		return;

	if (tx->new_range) {
		swmmu_pagetable_range_put(space, tx->new_range);
		tx->new_range = NULL;
	}
	space->ops->dealloc(tx);
}

static void swmmu_remove_detached_vma(struct mm_struct *mm,
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

static int swmmu_replace_prepare(struct vma_replace_struct *vrs)
{
	struct swmmu_pagetable_tx *tx;
	struct vm_area_struct *insert;
	struct mm_struct *mm;
	struct nommu_swmmu_space *space;
	struct swmmu_pagetable *pt;
	struct swmmu_pagetable_range *data;
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

	pt = swmmu_pagetable_alloc(space, length);
	if (!pt)
		return -ENOMEM;

	data = swmmu_pagetable_range_create(space, pt);
	if (!data) {
		swmmu_pagetable_put(space, pt);
		return -ENOMEM;
	}

	data->access = tx->replacement_access;
	insert->vm_swmmu_pt_range = data;
	vrs->backend_state = data;

	return 0;
}

static void swmmu_replace_abort(struct vma_replace_struct *vrs)
{
	struct vm_area_struct *insert;
	struct mm_struct *mm;

	if (!vrs || !vrs->insert)
		return;

	insert = vrs->insert;
	mm = insert->vm_mm;

	if (insert->vm_swmmu_pt_range) {
		swmmu_pagetable_range_put(mm->swmmu_space,
				       insert->vm_swmmu_pt_range);
		insert->vm_swmmu_pt_range = NULL;
	}

	vrs->backend_state = NULL;
}

static void swmmu_replace_commit(struct vma_replace_struct *vrs)
{
	if (!vrs)
		return;

	vrs->backend_state = NULL;
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
	vma->vm_swmmu_pt_range = NULL;

	vm_flags_init(vma, vma_flags_to_legacy(vma_flags));
	vma_set_pgoff(vma, pgoff);

	return vma;
}

static int swmmu_move_prepare(struct vma_remap_struct *vrm,
			struct vm_area_struct *src,
			struct vm_area_struct **dst,
			bool *dst_linked,
			void **state)
{
	struct swmmu_pagetable_tx *tx;
	struct swmmu_pagetable_range *src_range;
	struct swmmu_pagetable_range *new_range;
	struct swmmu_pagetable *new_pt;
	struct nommu_swmmu_space *space;
	struct vm_area_struct *new_vma;
	unsigned long old_nr;
	unsigned long new_nr;
	unsigned long i;
	int ret;

	if (!vrm || !src || !dst || !dst_linked || !state)
		return -EINVAL;

	src_range = src->vm_swmmu_pt_range;
	if (!src_range || !src_range->pagetable)
		return -EINVAL;

	space = src->vm_mm->swmmu_space;
	if (!space)
		return -EINVAL;

	old_nr = vrm->old_len / SWMMU_PAGE_SIZE;
	new_nr = vrm->new_len / SWMMU_PAGE_SIZE;

	if (!old_nr || !new_nr ||
	    old_nr > src_range->nr_ptes)
		return -EINVAL;

	new_vma = swmmu_fixed_alloc_vma(src->vm_mm, src, vrm->new_addr,
		vrm->new_addr + vrm->new_len, src->flags, src->vm_pgoff);
	if (!new_vma)
		return -ENOMEM;

	new_vma->vm_swmmu_pt_range = NULL;

	down_write(&space->lock);

	tx = space->ops->zalloc(sizeof(*tx), GFP_KERNEL);
	if (!tx) {
		ret = -ENOMEM;
		goto out_unlock_free_vma;
	}

	tx->kind = SWMMU_TX_MOVE;
	tx->source = src_range;
	tx->new_vma = new_vma;

	if (old_nr == new_nr) {
		new_range = swmmu_pagetable_range_share(
			space,
			src_range,
			src_range->first,
			old_nr);
		if (!new_range) {
			ret = -ENOMEM;
			goto out_unlock_free_tx;
		}

		tx->new_range = new_range;
		tx->move_reuses_pagetable = true;
		*dst = new_vma;
		*dst_linked = false;
		*state = tx;
		return 0;
	}

	new_pt = swmmu_pagetable_alloc(
		space, new_nr * SWMMU_PAGE_SIZE);
	if (!new_pt) {
		ret = -ENOMEM;
		goto out_unlock_free_tx;
	}

	new_range = swmmu_pagetable_range_create(space, new_pt);
	swmmu_pagetable_put(space, new_pt);
	if (!new_range) {
		ret = -ENOMEM;
		goto out_unlock_free_tx;
	}

	new_range->first = 0;
	new_range->nr_ptes = new_nr;
	new_range->access = src_range->access;

	for (i = 0; i < min(old_nr, new_nr); i++) {
		ret = space->ops->page_copy(
			new_range->pagetable->ptes[i].page,
			src_range->pagetable->ptes[
				src_range->first + i].page);
		if (ret) {
			swmmu_pagetable_range_release_locked(
				space, new_range);
			goto out_unlock_free_tx;
		}
	}

	tx->new_range = new_range;
	*dst = new_vma;
	*dst_linked = false;
	*state = tx;
	return 0;

out_unlock_free_tx:
	space->ops->dealloc(tx);
out_unlock_free_vma:
	up_write(&space->lock);
	vm_area_free(new_vma);
	return ret;
}

static void swmmu_move_commit(struct vma_remap_struct *vrm,
			      struct vm_area_struct *src,
			      struct vm_area_struct *dst,
			      void *state)
{
	struct swmmu_pagetable_tx *tx = state;
	struct nommu_swmmu_space *space;

	if (!tx || tx->kind != SWMMU_TX_MOVE)
		return;

	space = src->vm_mm->swmmu_space;

	dst->vm_swmmu_pt_range = tx->new_range;
	tx->new_range = NULL;

	if (tx->move_reuses_pagetable) {
		tx->source->first = 0;
		tx->source->nr_ptes = 0;
	}

	tx->source = NULL;
	tx->new_vma = NULL;
	tx->kind = 0;

	space->ops->dealloc(tx);
	up_write(&space->lock);
}

static void swmmu_move_abort(struct vma_remap_struct *vrm,
			     struct vm_area_struct *src,
			     struct vm_area_struct *dst,
			     void *state)
{
	struct swmmu_pagetable_tx *tx = state;
	struct nommu_swmmu_space *space;

	if (!tx || tx->kind != SWMMU_TX_MOVE)
		return;

	space = src->vm_mm->swmmu_space;

	if (tx->new_range) {
		if (tx->move_reuses_pagetable)
			swmmu_pagetable_range_put_locked(space, tx->new_range);
		else
			swmmu_pagetable_range_release_locked(space,
							tx->new_range);

		tx->new_range = NULL;
	}

	tx->source = NULL;
	tx->new_vma = NULL;
	tx->kind = 0;

	space->ops->dealloc(tx);
	up_write(&space->lock);
}

static unsigned long swmmu_move_mapping(struct vma_remap_struct *vrm,
					struct pagetable_move_control *pmc)
{
	(void)pmc;

	/*
	 * SWMMU entry preparation is already performed by
	 * swmmu_move_prepare().
	 */
	return vrm->old_len;
}

static unsigned long swmmu_get_unmapped_area(struct vma_remap_struct *vrm)
{
	unsigned long addr;
	int ret;

	if (vrm->flags & MREMAP_FIXED)
		return vrm->new_addr;

	ret = swmmu_find_free_range(
		vrm->mm,
		SWMMU_VA_BASE,
		vrm->new_len,
		&addr);
	if (ret)
		return ret;

	return addr;
}

static int swmmu_check_remap(struct vma_remap_struct *vrm)
{
	struct vm_area_struct *vma;

	if (!vrm || !vrm->vma)
		return -EINVAL;

	vma = vrm->vma;

	if (!vma->vm_swmmu_pt_range)
		return -EINVAL;

	/*
	 * Current SWMMU remap supports complete VMA ranges only.
	 */
	if (vma->vm_start != vrm->addr ||
	    vma->vm_end != vrm->addr + vrm->old_len)
		return -EINVAL;

	if ((vrm->flags & MREMAP_DONTUNMAP) ||
	    ((vrm->flags & MREMAP_FIXED) &&
	     vrm->new_len < vrm->old_len))
		return -EOPNOTSUPP;

	return 0;
}

static const struct vma_mapping_ops swmmu_vma_mapping_ops = {
	.split_prepare = swmmu_split_prepare,
	.split_commit = swmmu_split_commit,
	.split_abort = swmmu_split_abort,
	.remove_detached = swmmu_remove_detached_vma,
	.replace_prepare = swmmu_replace_prepare,
	.replace_commit = swmmu_replace_commit,
	.replace_abort = swmmu_replace_abort,
	.expand_prepare = swmmu_expand_prepare,
	.expand_commit = swmmu_expand_commit,
	.expand_abort = swmmu_expand_abort,
	.move_prepare = swmmu_move_prepare,
	.move_commit = swmmu_move_commit,
	.move_abort = swmmu_move_abort,
	.move_mapping = swmmu_move_mapping,
	.get_unmapped_area = swmmu_get_unmapped_area,
	.check_remap = swmmu_check_remap,
};

const struct vma_mapping_ops *vma_mapping_ops_for_mm(struct mm_struct *mm)
{
	return &swmmu_vma_mapping_ops;
}

static void swmmu_fixed_replace_commit(struct nommu_swmmu_space *space,
				struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable_range *old_pt_range;
	struct swmmu_pagetable_range *retained;

	if (!space || !tx || tx->kind != SWMMU_TX_FIXED_REPLACE)
		return;

	old_pt_range = tx->source;

	if (tx->replace_kind == SWMMU_REPLACE_HEAD)
		retained = tx->right_range;
	else
		retained = tx->left_range;

	if (WARN_ON_ONCE(!tx->old_vma ||
			 !tx->new_vma ||
			 !tx->new_vma->vm_swmmu_pt_range ||
			 !old_pt_range ||
			 !retained))
		return;

	tx->old_vma->vm_swmmu_pt_range = retained;

	if (tx->replace_kind == SWMMU_REPLACE_HEAD)
		tx->right_range = NULL;
	else
		tx->left_range = NULL;

	tx->source = NULL;

	swmmu_pagetable_range_put(space, old_pt_range);

	tx->old_vma = NULL;
	tx->new_vma = NULL;
	tx->kind = 0;
}

static void swmmu_fixed_replace_abort(struct nommu_swmmu_space *space,
				struct swmmu_pagetable_tx *tx)
{
	if (!space || !tx || tx->kind != SWMMU_TX_FIXED_REPLACE)
		return;

	if (tx->left_range) {
		swmmu_pagetable_range_put(space, tx->left_range);
		tx->left_range = NULL;
	}

	if (tx->right_range) {
		swmmu_pagetable_range_put(space, tx->right_range);
		tx->right_range = NULL;
	}

	if (tx->replacement_range) {
		swmmu_pagetable_range_release(space, tx->replacement_range);
		tx->replacement_range = NULL;
	}

	if (tx->new_vma)
		vm_area_free(tx->new_vma);

	tx->new_vma = NULL;
	tx->old_vma = NULL;
	tx->source = NULL;
	tx->kind = 0;
}

static int swmmu_fixed_replace_prepare(struct mm_struct *mm,
				struct nommu_swmmu_space *space,
				struct vm_area_struct *old_vma,
				unsigned long start,
				unsigned long end,
				unsigned long prot,
				vma_flags_t vma_flags,
				unsigned long pgoff,
				enum swmmu_pagetable_replace_kind kind,
				struct swmmu_pagetable_tx *tx)
{
	struct swmmu_pagetable_range *old_range;
	struct swmmu_pagetable_range *retained;
	struct swmmu_pagetable_range *replacement;
	struct swmmu_pagetable *pt;
	struct vm_area_struct *new_vma;
	unsigned long split_pages;
	unsigned long retained_first;
	size_t retained_nr;
	int ret;

	if (!mm || !space || !old_vma || !tx ||
	    start >= end)
		return -EINVAL;

	old_range = old_vma->vm_swmmu_pt_range;
	if (!old_range || !old_range->pagetable)
		return -EINVAL;

	if (old_vma->vm_file || old_vma->vm_region)
		return -EOPNOTSUPP;

	if (kind != SWMMU_REPLACE_HEAD &&
	    kind != SWMMU_REPLACE_TAIL)
		return -EOPNOTSUPP;

	if (start < old_vma->vm_start ||
	    end > old_vma->vm_end)
		return -EINVAL;

	if (kind == SWMMU_REPLACE_HEAD) {
		if (start != old_vma->vm_start ||
		    end >= old_vma->vm_end)
			return -EINVAL;

		split_pages = (end - old_vma->vm_start) /
			      SWMMU_PAGE_SIZE;
		retained_first = old_range->first + split_pages;
		retained_nr = old_range->nr_ptes - split_pages;
	} else {
		if (start <= old_vma->vm_start ||
		    end != old_vma->vm_end)
			return -EINVAL;

		split_pages = (start - old_vma->vm_start) /
			      SWMMU_PAGE_SIZE;
		retained_first = old_range->first;
		retained_nr = split_pages;
	}

	if (!split_pages || !retained_nr ||
	    !IS_ALIGNED(split_pages * SWMMU_PAGE_SIZE,
			SWMMU_PAGE_SIZE))
		return -EINVAL;

	if (old_range->first > old_range->pagetable->nr_ptes ||
	    old_range->nr_ptes >
	    old_range->pagetable->nr_ptes - old_range->first)
		return -EINVAL;

	memset(tx, 0, sizeof(*tx));

	tx->kind = SWMMU_TX_FIXED_REPLACE;
	tx->replace_kind = kind;
	tx->source = old_range;
	tx->old_vma = old_vma;
	tx->replace_start = start;
	tx->replace_end = end;

	retained = space->ops->zalloc(sizeof(*retained), GFP_KERNEL);
	if (!retained)
		return -ENOMEM;

	swmmu_pagetable_get(old_range->pagetable);

	retained->pagetable = old_range->pagetable;
	retained->first = retained_first;
	retained->nr_ptes = retained_nr;
	retained->access = old_range->access;

	if (kind == SWMMU_REPLACE_HEAD)
		tx->right_range = retained;
	else
		tx->left_range = retained;

	pt = swmmu_pagetable_alloc(space, end - start);
	if (!pt) {
		ret = -ENOMEM;
		goto abort;
	}

	replacement = swmmu_pagetable_range_create(space, pt);
	swmmu_pagetable_put(space, pt);
	if (!replacement) {
		ret = -ENOMEM;
		goto abort;
	}

	replacement->access = swmmu_access_from_prot(prot);
	tx->replacement_range = replacement;

	new_vma = swmmu_fixed_alloc_vma(mm, old_vma,
					start, end,
					vma_flags, pgoff);
	if (!new_vma) {
		ret = -ENOMEM;
		goto abort;
	}

	tx->new_vma = new_vma;
	return 0;

abort:
	swmmu_fixed_replace_abort(space, tx);
	return ret;
}


static unsigned long swmmu_mmap_fixed_replace(struct mm_struct *mm,
					enum swmmu_pagetable_replace_kind kind,
					struct nommu_swmmu_space *space,
					struct vm_area_struct *old_vma,
					unsigned long start,
					unsigned long end,
					unsigned long prot,
					vma_flags_t vma_flags,
					unsigned long pgoff)
{
	struct swmmu_pagetable_tx tx = {};
	struct vm_area_struct *new_vma;
	VMA_ITERATOR(vmi, mm, start);
	unsigned long split_pages;
	int ret;

	if (!old_vma->vm_swmmu_pt_range ||
	    old_vma->vm_file ||
	    old_vma->vm_region)
		return -EOPNOTSUPP;

	ret = swmmu_fixed_replace_prepare(mm, space, old_vma, start, end,
					prot, vma_flags, pgoff, kind, &tx);
	if (ret)
		return ret;
	new_vma = tx.new_vma;

	/*
	 * The store range is the replacement prefix. Maple Tree keeps
	 * the remainder of old_vma's original range as the suffix.
	 */
	vma_iter_config(&vmi, start, end);
	ret = vma_iter_prealloc(&vmi, new_vma);
	if (ret)
		goto abort;

	new_vma->vm_swmmu_pt_range = tx.replacement_range;
	tx.replacement_range = NULL;

	vma_start_write(old_vma);
	vma_start_write(new_vma);

	split_pages = (end - old_vma->vm_start) / SWMMU_PAGE_SIZE;

	/*
	 * This is the point of no return: preallocation succeeded and
	 * the following store must not fail.
	 */
	if (kind == SWMMU_REPLACE_HEAD) {
		old_vma->vm_start = end;
		vma_add_pgoff(old_vma, split_pages);
	} else if  (kind == SWMMU_REPLACE_TAIL) {
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
	struct swmmu_pagetable_tx tx = {};
	int ret;

	if (!old_vma->vm_swmmu_pt_range ||
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
			&swmmu_vma_mapping_ops);

	vma_replace_init(&vrs, &vms, new_vma,
			 &swmmu_vma_mapping_ops);
	vrs.backend_state = &tx;

	ret = vma_replace_prepare(&vrs, &mas_detach);
	if (ret)
		goto abort;

	vma_replace_commit(&vrs, &mas_detach, mm, swmmu_remove_detached_vma);

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
	struct swmmu_pagetable_tx tx = {};
	int ret;

	insert = swmmu_fixed_alloc_vma(mm, NULL, start, end,
				vma_flags, pgoff);
	if (!insert)
		return -ENOMEM;

	insert->vm_swmmu_pt_range = NULL;

	tx.replacement_access = swmmu_access_from_prot(prot);

	mt_init_flags(&mt_detach,
		      vmi.mas.tree->ma_flags &
		      MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	vma_init_munmap(&vms, &vmi, vma_find(&vmi, end),
			start, end, NULL, false,
			&swmmu_vma_mapping_ops);

	vma_replace_init(&vrs, &vms, insert,
			 &swmmu_vma_mapping_ops);
	vrs.backend_state = &tx;

	ret = vma_replace_prepare(&vrs, &mas_detach);
	if (ret)
		goto abort;

	vma_replace_commit(&vrs, &mas_detach, mm, swmmu_remove_detached_vma);

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
	struct swmmu_pagetable *pt;
	struct swmmu_pagetable_range *swmmu_vma;
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
		enum swmmu_pagetable_replace_kind kind = SWMMU_REPLACE_EXACT;

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
			if (!replace_vma->vm_swmmu_pt_range)
				return -EOPNOTSUPP;

			if (replace_vma->vm_start == addr &&
				replace_vma->vm_end == end)
				kind = SWMMU_REPLACE_EXACT;
			else if (replace_vma->vm_start == addr &&
				end < replace_vma->vm_end)
				kind = SWMMU_REPLACE_HEAD;
			else if (addr > replace_vma->vm_start &&
				replace_vma->vm_end == end)
				kind = SWMMU_REPLACE_TAIL;
			else if (addr > replace_vma->vm_start &&
				end < replace_vma->vm_end)
				kind = SWMMU_REPLACE_MIDDLE;
			else
				return -EINVAL;

			if (kind == SWMMU_REPLACE_MIDDLE)
				return swmmu_mmap_fixed_middle_replace(
					mm, space, replace_vma,
					addr, end, prot,
					vma_flags, pgoff);
			else if (kind != SWMMU_REPLACE_EXACT)
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

	pt = swmmu_pagetable_alloc(space, end - start);
	if (!pt)
		return -ENOMEM;

	vma = swmmu_fixed_alloc_vma(mm, NULL, start, end,
					vma_flags, pgoff);
	if (!vma) {
		swmmu_pagetable_put(space, pt);
		return -ENOMEM;
	}

	swmmu_vma = swmmu_pagetable_range_create(space, pt);
	if (!swmmu_vma) {
		vm_area_free(vma);
		swmmu_pagetable_put(space, pt);
		return -ENOMEM;
	}

	access = swmmu_access_from_prot(prot);
	swmmu_vma->access = access;
	vma->vm_swmmu_pt_range = swmmu_vma;

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
	vma->vm_swmmu_pt_range = NULL;
	swmmu_pagetable_range_release(space, swmmu_vma);
	vm_area_free(vma);
	swmmu_pagetable_put(space, pt);
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

int nommu_swmmu_kunit_drop_range(struct nommu_swmmu_space *space,
				struct swmmu_pagetable_range *range,
				unsigned long first,
				unsigned long nr_ptes,
				bool commit)
{
	struct swmmu_pagetable_tx tx = {};
	int ret;

	if (!space || !range)
		return -EINVAL;

	down_write(&space->lock);

	ret = swmmu_pagetable_drop_range_prepare(space, range, first, nr_ptes, &tx);
	if (ret)
		goto out_unlock;

	if (commit)
		swmmu_pagetable_drop_range_commit(space, &tx);
	else
		swmmu_pagetable_drop_range_abort(space, &tx);

out_unlock:
	up_write(&space->lock);
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
			&swmmu_vma_mapping_ops);

	ret = vma_gather_range(&vms, &mas_detach);
	if (ret)
		goto out_destroy;

	ret = vma_iter_clear_gfp(vmi, start, end, GFP_KERNEL);
	if (ret) {
		vma_reattach_vmas(&mas_detach);
		goto out_destroy;
	}

	mm->map_count -= vms.vma_count;

	vma_remove_detached(&vms, &mas_detach, mm, swmmu_remove_detached_vma);

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
	if (vma && vma->vm_swmmu_pt_range) {
		return nommu_swmmu_unmap_shared_range(
			mm, &vmi, vma, start, end, uf);
	}

	return do_munmap_nommu(mm, start, len, uf);
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
	struct vma_remap_struct vrm = {
		.addr = untagged_addr(addr),
		.old_len = old_len,
		.new_len = new_len,
		.flags = flags,
		.new_addr = new_addr,

		.remap_type = MREMAP_INVALID, /* We set later. */
		.mm = mm,
		.backend = vma_mapping_ops_for_mm(mm),
		.backend_state = NULL,
	};


	if (!mm)
		return -EINVAL;

	ret = do_mremap(&vrm);

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
		!vma->vm_swmmu_pt_range) {
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
