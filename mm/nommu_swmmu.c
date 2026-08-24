#include <linux/nommu_swmmu.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>

#define SWMMU_VA_BASE ((uintptr_t)0x1000000000ULL)

struct swmmu_mapping {
	uintptr_t base;
	size_t length;
	size_t page_count;
	struct page **pages;

	struct swmmu_mapping *next;
};

struct nommu_swmmu_space {
	struct maple_tree mappings;
	struct rw_semaphore lock;

	unsigned long next_address;
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

#ifdef CONFIG_NOMMU_SWMMU_DEBUG
static void
dump_mappings(struct nommu_swmmu_space *space)
{
	struct swmmu_mapping *mapping;

	MA_STATE(mas, &space->mappings, 0, 0);

	mas_for_each(&mas, mapping, ULONG_MAX) {
		unsigned long start = mas.index;
		unsigned long last = mas.last;

		pr_debug("SWMMU mapping: [%lx-%lx] %p\n",
			 start, last, mapping);
	}
}

static void
dump_mapping_range(struct nommu_swmmu_space *space,
		   unsigned long start,
		   unsigned long end)
{
	struct swmmu_mapping *mapping;

	MA_STATE(mas, &space->mappings, start, end);

	mas_for_each(&mas, mapping, end) {
		pr_debug("mapping: [%lx-%lx] %p\n",
			 mas.index, mas.last, mapping);
	}
}
#endif /* CONFIG_NOMMU_SWMMU_DEBUG */

static struct swmmu_mapping *
find_mapping(struct nommu_swmmu_space *space,
	     uintptr_t address,
	     size_t size)
{
	struct swmmu_mapping *mapping;
	unsigned long last;

	if (!space || size == 0)
		return NULL;

	lockdep_assert_held(&space->lock);

	if (address > ULONG_MAX - (size - 1))
		return NULL;

	last = address + size - 1;

	MA_STATE(mas, &space->mappings, address, last);

	mapping = mas_walk(&mas);
	if (!mapping)
		return NULL;

	/*
	 * mas.index and mas.last describe the range containing
	 * the returned entry.
	 */
	if (address < mas.index || last > mas.last)
		return NULL;

	/*
	 * Equivalent check using the mapping metadata itself.
	 */
	if (address < mapping->base ||
	    last >= mapping->base + mapping->length)
		return NULL;

	return mapping;
}

static void
release_mapping(struct nommu_swmmu_space *space,
		struct swmmu_mapping *mapping,
		size_t allocated_pages)
{
	size_t i;

	if (!space || !mapping)
		return;

	for (i = 0; i < allocated_pages; i++) {
		space->ops->page_free(mapping->pages[i]);
	}

	if (mapping->pages)
		space->ops->dealloc(mapping->pages);

	space->ops->dealloc(mapping);
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
	mt_init_flags(&space->mappings,
		MT_FLAGS_LOCK_EXTERN);
	mt_set_external_lock(&space->mappings,
			&space->lock);
	space->next_address = SWMMU_VA_BASE;
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

void nommu_swmmu_space_attach(struct mm_struct *mm,
			       struct nommu_swmmu_space *space)
{
	space->mm = mm;
	mm->swmmu_space = space;
}

void nommu_swmmu_space_destroy(struct nommu_swmmu_space *space)
{
	const struct nommu_swmmu_mem_ops *mem_ops;
	struct swmmu_mapping *mapping;
	int i;

	if (!space)
		return;

	down_write(&space->lock);

	mem_ops = space->ops;

	MA_STATE(mas, &space->mappings, 0, 0);
	mas_for_each(&mas, mapping, ULONG_MAX) {
		unsigned long start = mas.index;
		unsigned long last = mas.last;

		pr_debug("SWMMU destroying mapping: [%lx-%lx] %p\n",
			 start, last, mapping);
		for (i = 0; i < mapping->page_count; i++)
			mem_ops->page_free(mapping->pages[i]);

		mem_ops->dealloc(mapping->pages);
		mem_ops->dealloc(mapping);
	}

	if (space->mm && space->mm->swmmu_space == space)
		space->mm->swmmu_space = NULL;
	space->mm = NULL;
	__mt_destroy(&space->mappings);

	up_write(&space->lock);

	mem_ops->dealloc(space);
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

	if (!current->mm)
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
#endif

static int
swmmu_store_range_locked(struct nommu_swmmu_space *space,
			 unsigned long first,
			 unsigned long last,
			 void *entry,
			 gfp_t gfp)
{
	MA_STATE(mas, &space->mappings, first, last);
	int ret;

	lockdep_assert_held_write(&space->lock);

	ret = mas_store_gfp(&mas, entry, gfp);
	mas_destroy(&mas);

	return ret;
}

static void *
swmmu_erase_locked(struct nommu_swmmu_space *space,
		   unsigned long index)
{
	MA_STATE(mas, &space->mappings, index, index);
	void *entry;

	lockdep_assert_held_write(&space->lock);

	entry = mas_erase(&mas);
	mas_destroy(&mas);

	return entry;
}

long swmmu_alloc(size_t size)
{
	struct nommu_swmmu_space *space;
	struct swmmu_mapping *mapping;
	size_t length;
	size_t i;
	uintptr_t base;
	long ret;

	space = nommu_swmmu_current();
	if (!space || size == 0)
		return -EINVAL;

	length = page_align(size);
	if (length < size)
		return -EINVAL;

	down_write(&space->lock);

	base = page_align(space->next_address);
	if (base > UINTPTR_MAX - length) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (base > LONG_MAX) {
		ret = -EOVERFLOW;
		goto out_unlock;
	}

	mapping = space->ops->zalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		pr_warn("%s: page metadata allocation failure", __func__);
		ret = -ENOMEM;
		goto out_unlock;
	}

	mapping->base = base;
	mapping->length = length;

	mapping->page_count = length / SWMMU_PAGE_SIZE;
	mapping->pages = space->ops->zalloc(mapping->page_count *
					sizeof(*mapping->pages),
					GFP_KERNEL);
	if (!mapping->pages) {
		pr_warn("%s: page array allocation failure", __func__);
		ret = -ENOMEM;
		goto free_mapping;
	}

	for (i = 0; i < mapping->page_count; i++) {
		mapping->pages[i] = space->ops->page_alloc(GFP_KERNEL);
		if (!mapping->pages[i]) {
			ret = -ENOMEM;
			pr_warn("%s: page allocation failure at [%lu]", __func__, i);
			goto free_pages;
		}
	}

	ret = swmmu_store_range_locked(space,
				base,
				base + length - 1,
				mapping,
				GFP_KERNEL);
	if (ret) {
		pr_warn("%s: mtree_store_range failure: %ld", __func__, ret);
		goto free_pages;
	}

	space->next_address = base + length;
	ret = (long)base;
	goto out_unlock;

free_pages:
	while (i > 0)
		space->ops->page_free(mapping->pages[--i]);

	space->ops->dealloc(mapping->pages);
free_mapping:
	space->ops->dealloc(mapping);

out_unlock:
	up_write(&space->lock);
	return ret;
}

int swmmu_free(void *address)
{
	struct nommu_swmmu_space *space;
	struct swmmu_mapping *mapping;
	uintptr_t base = (uintptr_t)address;
	void *entry;

	space = nommu_swmmu_current();
	if (!space || !address)
		return -EINVAL;

	down_write(&space->lock);
	mapping = find_mapping(space, base, 1);
	if (!mapping || mapping->base != base) {
		up_write(&space->lock);
		return -EINVAL;
	}

	entry = swmmu_erase_locked(space, mapping->base);
	if (entry != mapping) {
		pr_warn("%s: maple tree inconsistency", __func__);
		up_write(&space->lock);
		return -EINVAL;
	}

	release_mapping(space, mapping, mapping->page_count);

	up_write(&space->lock);
	return 0;
}

static void *__swmmu_translate(struct nommu_swmmu_space *space,
			uintptr_t address,
			size_t size,
			int write)
{
	struct swmmu_mapping *mapping;
	size_t offset;
	size_t page_index;

	(void)write;

	lockdep_assert_held(&space->lock);

	mapping = find_mapping(space, address, size);
	if (!mapping)
		return NULL;

	offset = address - mapping->base;
	page_index = offset / SWMMU_PAGE_SIZE;
	offset %= SWMMU_PAGE_SIZE;

	/*
	 * This function returns a contiguous pointer from one page only.
	 * Multi-page copies are handled by swmmu_load_u64/store_u64.
	 */
	if (offset + size > SWMMU_PAGE_SIZE)
		return NULL;

	return page_address(mapping->pages[page_index]) + offset;
}

int nommu_swmmu_check_access(struct nommu_swmmu_space *space,
			uintptr_t address,
			size_t size,
			int write)
{
	int ret;

	if (!space || !size)
		return -EINVAL;

	down_read(&space->lock);

	ret = __swmmu_translate(space, address, size, write) ?
	      0 : -EFAULT;

	up_read(&space->lock);

	return ret;
}

static int swmmu_copy_from_space(struct nommu_swmmu_space *space,
				  uintptr_t address,
				  void *destination,
				  size_t size)
{
	int ret = 0;

	down_read(&space->lock);

	while (size) {
		size_t page_offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - page_offset;
		void *source;

		if (chunk > size)
			chunk = size;

		source = __swmmu_translate(space, address, chunk, 0);
		if (!source) {
			ret = -EFAULT;
			goto out;
		}

		memcpy(destination, source, chunk);

		address += chunk;
		destination = (unsigned char *)destination + chunk;
		size -= chunk;
	}

out:
	up_read(&space->lock);
	return ret;
}

static int swmmu_copy_to_space(struct nommu_swmmu_space *space,
				uintptr_t address,
				const void *source,
				size_t size)
{
	int ret = 0;

	down_read(&space->lock);

	while (size) {
		size_t page_offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - page_offset;
		void *destination;

		if (chunk > size)
			chunk = size;

		destination = __swmmu_translate(space, address, chunk, 1);
		if (!destination) {
			ret = -EFAULT;
			goto out;
		}

		memcpy(destination, source, chunk);

		address += chunk;
		source = (const unsigned char *)source + chunk;
		size -= chunk;
	}

out:
	up_read(&space->lock);
	return ret;
}


static int
clone_one_mapping(struct nommu_swmmu_space *child,
		  const struct swmmu_mapping *source)
{
	struct swmmu_mapping *copy;
	size_t i;
	int ret;

	copy = child->ops->zalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy) {
		pr_warn("%s: page metadata allocation failure", __func__);
		return -ENOMEM;
	}

	copy->base = source->base;
	copy->length = source->length;
	copy->page_count = source->page_count;

	copy->pages = child->ops->zalloc(copy->page_count * sizeof(*copy->pages),
				GFP_KERNEL);
	if (!copy->pages) {
		child->ops->dealloc(copy);
		pr_warn("%s: page array allocation failure", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < copy->page_count; i++) {
		copy->pages[i] = child->ops->page_alloc(GFP_KERNEL);
		if (!copy->pages[i]) {
			release_mapping(child, copy, i);
			pr_warn("%s: page allocation failure at [%lu]", __func__, i);
			return -ENOMEM;
		}

		ret = child->ops->page_copy(copy->pages[i],
					source->pages[i]);
		if (ret) {
			release_mapping(child, copy, i + 1);
			pr_warn("%s: page copy failure at [%lu]", __func__, i);
			return ret;
		}
	}

	ret = swmmu_store_range_locked(child,
				copy->base,
				copy->base + copy->length - 1,
				copy,
				GFP_KERNEL);
	if (ret) {
		release_mapping(child, copy, copy->page_count);
		pr_warn("%s: mtree_store_range failure: %d", __func__, ret);
		return ret;
	}

	return 0;
}

/*
 * Clone @parent into a new software address space.
 *
 * @parent is not modified, but must be protected by the caller's
 * read-side mapping lock while this function executes.
 */
int swmmu_clone_space(struct nommu_swmmu_space *parent,
		      struct nommu_swmmu_space **child_out)
{
	struct nommu_swmmu_space *child;
	struct swmmu_mapping *source;
	int ret;

	if (!parent || !child_out)
		return -EINVAL;

	*child_out = NULL;

	child = __nommu_swmmu_space_create(parent->ops);
	if (!child) {
		pr_warn("%s: page metadata allocation failure", __func__);
		return -ENOMEM;
	}

	down_read(&parent->lock);
	down_write_nested(&child->lock, SINGLE_DEPTH_NESTING);

	/*
	 * Preserve the allocator position so future allocations also
	 * remain deterministic.
	 */
	child->next_address = parent->next_address;

	MA_STATE(mas, &parent->mappings, 0, 0);
	mas_for_each(&mas, source, ULONG_MAX) {
		ret = clone_one_mapping(child, source);
		if (ret)
			goto error;
	}

	up_write(&child->lock);
	up_read(&parent->lock);

	*child_out = child;
	return 0;

error:
	up_write(&child->lock);
	up_read(&parent->lock);
	nommu_swmmu_space_destroy(child);
	*child_out = NULL;
	return ret;
}

uint64_t nommu_swmmu_load_u64(const void *address, size_t size)
{
	uint64_t value = 0;
	int ret;
	struct nommu_swmmu_space *space;

	space = nommu_swmmu_current();

	if (size != 1 && size != 2 && size != 4 && size != 8)
		BUG();

	if (!space)
		BUG();

	ret = swmmu_copy_from_space(space,
				    (uintptr_t)address,
				    &value,
				    size);
	if (ret)
		BUG();

	return value;
}

void nommu_swmmu_store_u64(void *address, size_t size, uint64_t value)
{
	int ret;
	struct nommu_swmmu_space *space;

	space = nommu_swmmu_current();

	if (size != 1 && size != 2 && size != 4 && size != 8)
		BUG();

	if (!space)
		BUG();

	ret = swmmu_copy_to_space(space,
				  (uintptr_t)address,
				  &value,
				  size);
	if (ret)
		BUG();
}

SYSCALL_DEFINE1(nommu_swmmu_alloc, size_t, size)
{
	long address;

	address = swmmu_alloc(size);
	if (address < 0)
		return address;

	return (unsigned long)address;
}

SYSCALL_DEFINE1(nommu_swmmu_free, void __user *, address)
{
	return swmmu_free(address);
}

SYSCALL_DEFINE3(nommu_swmmu_load,
		void __user *, address,
		size_t, size,
		u64 __user *, result)
{
	u64 value;

	value = nommu_swmmu_load_u64(address, size);

	if (copy_to_user(result, &value, sizeof(value)))
		return -EFAULT;
	return (long)value;
}

SYSCALL_DEFINE3(nommu_swmmu_store, void __user *, address, size_t, size, uint64_t, value)
{
	nommu_swmmu_store_u64(address, size, value);
	return 0;
}
