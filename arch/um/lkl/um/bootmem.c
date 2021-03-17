// SPDX-License-Identifier: GPL-2.0
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/swap.h>

unsigned long memory_end, memory_start;
EXPORT_SYMBOL(memory_start);
static unsigned long _memory_start, mem_size;

unsigned long *empty_zero_page;

/* XXX: unused */
unsigned long long highmem;
int iomem_size;
int kmalloc_ok = 1;

void __init setup_physmem(unsigned long start, unsigned long reserve_end,
			  unsigned long mem_sz, unsigned long long _highmem)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	mem_size = mem_sz;

	_memory_start = (unsigned long)lkl_mem_alloc(mem_size);
	memory_start = _memory_start;
	WARN_ON(!memory_start);
	memory_end = memory_start + mem_size;

	if (PAGE_ALIGN(memory_start) != memory_start) {
		mem_size -= PAGE_ALIGN(memory_start) - memory_start;
		memory_start = PAGE_ALIGN(memory_start);
		mem_size = (mem_size / PAGE_SIZE) * PAGE_SIZE;
	}
	pr_info("memblock address range: 0x%lx - 0x%lx\n", memory_start,
		memory_start+mem_size);
	/*
	 * Give all the memory to the bootmap allocator, tell it to put the
	 * boot mem_map at the start of memory.
	 */
	max_low_pfn = virt_to_pfn(memory_end);
	min_low_pfn = virt_to_pfn(memory_start);
	memblock_add(memory_start, mem_size);

	empty_zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init(zones_size);

}

void __init mem_init(void)
{
	max_mapnr = (((unsigned long)high_memory) - PAGE_OFFSET) >> PAGE_SHIFT;
	/* this will put all memory onto the freelists */
	memblock_free_all();
	pr_info("Memory available: %luk/%luk RAM\n",
		(nr_free_pages() << PAGE_SHIFT) >> 10, mem_size >> 10);
}

/*
 * In our case __init memory is not part of the page allocator so there is
 * nothing to free.
 */
void free_initmem(void)
{
}

void free_mem(void)
{
	lkl_mem_free((void *)_memory_start);
}

void __init mem_total_pages(unsigned long physmem, unsigned long iomem,
		     unsigned long _highmem)
{
}

void __init paging_init(void)
{
}

void *uml_kmalloc(int size, int flags)
{
	return kmalloc(size, flags);
}
