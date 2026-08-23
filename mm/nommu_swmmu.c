#include <linux/nommu_swmmu.h>
#include <linux/slab.h>
#include <linux/mm.h>
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
	unsigned long next_address;
	struct mm_struct *mm;
	refcount_t users;
};

static size_t page_align(size_t size)
{
	return (size + SWMMU_PAGE_SIZE - 1) &
	       ~(SWMMU_PAGE_SIZE - 1);
}

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

static struct swmmu_mapping *
find_mapping(struct nommu_swmmu_space *space,
	     uintptr_t address,
	     size_t size)
{
	struct swmmu_mapping *mapping;
	unsigned long last;

	if (!space || size == 0)
		return NULL;

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

struct nommu_swmmu_space *nommu_swmmu_space_create(void)
{
	struct nommu_swmmu_space *space;

	space = kzalloc(sizeof(*space), GFP_KERNEL);
	if (!space)
		return NULL;

	refcount_set(&space->users, 1);

	mt_init(&space->mappings);
	space->next_address = SWMMU_VA_BASE;
	space->mm = NULL;

	return space;
}

void nommu_swmmu_space_attach(struct mm_struct *mm,
			       struct nommu_swmmu_space *space)
{
	space->mm = mm;
	mm->swmmu_space = space;
}

void nommu_swmmu_space_destroy(struct nommu_swmmu_space *space)
{
	struct swmmu_mapping *mapping;
	int i;

	if (!space)
		return;

	MA_STATE(mas, &space->mappings, 0, 0);
	mas_for_each(&mas, mapping, ULONG_MAX) {
		unsigned long start = mas.index;
		unsigned long last = mas.last;

		pr_debug("SWMMU destroying mapping: [%lx-%lx] %p\n",
			 start, last, mapping);
		for (i = 0; i < mapping->page_count; i++)
			__free_page(mapping->pages[i]);

		kfree(mapping->pages);
		kfree(mapping);
	}

	if (nommu_swmmu_current() == space)
		current->mm->swmmu_space = NULL;

	__mt_destroy(&space->mappings);
	kfree(space);
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

void *swmmu_alloc(size_t size)
{
	struct nommu_swmmu_space *space;
	struct swmmu_mapping *mapping;
	size_t length;
	size_t i;
	uintptr_t base;
	int ret;

	space = nommu_swmmu_current();
	if (!space || size == 0)
		return NULL;

	length = page_align(size);
	if (length < size)
		return NULL;

	base = page_align(space->next_address);

	if (base > UINTPTR_MAX - length)
		return NULL;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return NULL;

	mapping->base = base;
	mapping->length = length;

	mapping->page_count = length / SWMMU_PAGE_SIZE;
	mapping->pages = kcalloc(mapping->page_count,
				sizeof(*mapping->pages),
				GFP_KERNEL);
	if (!mapping->pages) {
		kfree(mapping);
		return NULL;
	}

	for (i = 0; i < mapping->page_count; i++) {
		mapping->pages[i] = alloc_page(GFP_KERNEL);
		if (!mapping->pages[i])
			goto error;
	}

	ret = mtree_store_range(&space->mappings,
				base,
				base + length - 1,
				mapping,
				GFP_KERNEL);
	if (ret) {
		pr_warn("mtree_store_range failure: %d", ret);
		goto error;
	}

	space->next_address = base + length;

	return (void *)base;
error:
	while (i > 0) {
		i--;
		__free_page(mapping->pages[i]);
	}

	kfree(mapping->pages);
	kfree(mapping);

	return NULL;
}

int swmmu_free(void *address)
{
	struct swmmu_mapping *mapping;
	size_t i;

	if (!nommu_swmmu_current() || !address)
		return -EINVAL;

	mapping = find_mapping(nommu_swmmu_current(), (uintptr_t)address, 1);
	if (!mapping)
		return -EINVAL;

	void *entry;

	entry = mtree_erase(&nommu_swmmu_current()->mappings, mapping->base);
	if (entry != mapping) {
		pr_warn("maple tree inconsistency");
		return -EINVAL;
	}

	for (i = 0; i < mapping->page_count; i++)
		__free_page(mapping->pages[i]);

	kfree(mapping->pages);
	kfree(mapping);

	return 0;
}

void *swmmu_translate(struct nommu_swmmu_space *space,
		      uintptr_t address,
		      size_t size,
		      int write)
{
	struct swmmu_mapping *mapping;
	size_t offset;
	size_t page_index;

	(void)write;

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

static int swmmu_copy_from_space(struct nommu_swmmu_space *space,
				  uintptr_t address,
				  void *destination,
				  size_t size)
{
	while (size) {
		size_t page_offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - page_offset;
		void *source;

		if (chunk > size)
			chunk = size;

		source = swmmu_translate(space, address, chunk, 0);
		if (!source)
			return -EFAULT;

		memcpy(destination, source, chunk);

		address += chunk;
		destination = (unsigned char *)destination + chunk;
		size -= chunk;
	}

	return 0;
}

static int swmmu_copy_to_space(struct nommu_swmmu_space *space,
				uintptr_t address,
				const void *source,
				size_t size)
{
	while (size) {
		size_t page_offset = address & (SWMMU_PAGE_SIZE - 1);
		size_t chunk = SWMMU_PAGE_SIZE - page_offset;
		void *destination;

		if (chunk > size)
			chunk = size;

		destination = swmmu_translate(space, address, chunk, 1);
		if (!destination)
			return -EFAULT;

		memcpy(destination, source, chunk);

		address += chunk;
		source = (const unsigned char *)source + chunk;
		size -= chunk;
	}

	return 0;
}

static void
release_mapping(struct nommu_swmmu_space *space,
		struct swmmu_mapping *mapping,
		size_t allocated_pages)
{
	size_t i;

	if (!mapping)
		return;

	for (i = 0; i < allocated_pages; i++) {
		__free_page(mapping->pages[i]);
	}

	if (mapping->pages)
		kfree(mapping->pages);

	kfree(mapping);
}

static int
clone_one_mapping(struct nommu_swmmu_space *child,
		  const struct swmmu_mapping *source)
{
	struct swmmu_mapping *copy;
	size_t i;
	int ret;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	copy->base = source->base;
	copy->length = source->length;
	copy->page_count = source->page_count;

	copy->pages = kzalloc(copy->page_count * sizeof(*copy->pages), GFP_KERNEL);
	if (!copy->pages) {
		kfree(copy);
		return -ENOMEM;
	}

	for (i = 0; i < copy->page_count; i++) {
		copy->pages[i] = alloc_page(GFP_KERNEL);
		if (!copy->pages[i]) {
			release_mapping(child, copy, i);
			return -ENOMEM;
		}

		copy_highpage(copy->pages[i], source->pages[i]);
	}

	ret = mtree_store_range(
		&child->mappings,
		copy->base,
		copy->base + copy->length - 1,
		copy,
		GFP_KERNEL);
	if (ret) {
		release_mapping(child, copy, copy->page_count);
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

	child = nommu_swmmu_space_create();
	if (!child)
		return -ENOMEM;

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

	*child_out = child;
	return 0;

error:
	nommu_swmmu_space_destroy(child);
	return -ENOMEM;
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

	ret = swmmu_copy_from_space(nommu_swmmu_current(),
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

	if (size != 1 && size != 2 && size != 4 && size != 8)
		BUG();

	if (!nommu_swmmu_current())
		BUG();

	ret = swmmu_copy_to_space(nommu_swmmu_current(),
				  (uintptr_t)address,
				  &value,
				  size);
	if (ret)
		BUG();
}

SYSCALL_DEFINE1(nommu_swmmu_alloc, size_t, size)
{
	void *address;

	address = swmmu_alloc(size);
	if (!address)
		return -ENOMEM;

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
