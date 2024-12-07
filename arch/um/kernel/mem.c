// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <asm/fixmap.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <as-layout.h>
#include <init.h>
#include <kern.h>
#include <kern_util.h>
#include <mem_user.h>
#include <os.h>
#include <um_malloc.h>
#include <linux/sched/task.h>

#ifdef CONFIG_KASAN
int kasan_um_is_ready;
void kasan_init(void)
{
	/*
	 * kasan_map_memory will map all of the required address space and
	 * the host machine will allocate physical memory as necessary.
	 */
	kasan_map_memory((void *)KASAN_SHADOW_START, KASAN_SHADOW_SIZE);
	init_task.kasan_depth = 0;
	kasan_um_is_ready = true;
}

static void (*kasan_init_ptr)(void)
__section(".kasan_init") __used
= kasan_init;
#endif

/* allocated in paging_init, zeroed in mem_init, and unchanged thereafter */
unsigned long *empty_zero_page = NULL;
EXPORT_SYMBOL(empty_zero_page);

/*
 * Initialized during boot, and readonly for initializing page tables
 * afterwards
 */
pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* Initialized at boot time, and readonly after that */
int kmalloc_ok = 0;

/* Used during early boot */
static unsigned long brk_end;

void __init mem_init(void)
{
	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* Map in the area just after the brk now that kmalloc is about
	 * to be turned on.
	 */
	brk_end = (unsigned long) UML_ROUND_UP(sbrk(0));
	map_memory(brk_end, __pa(brk_end), uml_reserved - brk_end, 1, 1, 0);
	memblock_free((void *)brk_end, uml_reserved - brk_end);
	uml_reserved = brk_end;

	/* this will put all low memory onto the freelists */
	memblock_free_all();
	max_pfn = max_low_pfn;
	kmalloc_ok = 1;
}

void __init paging_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

	empty_zero_page = (unsigned long *) memblock_alloc_low(PAGE_SIZE,
							       PAGE_SIZE);
	if (!empty_zero_page)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	max_zone_pfn[ZONE_NORMAL] = end_iomem >> PAGE_SHIFT;
	free_area_init(max_zone_pfn);

	um_fixrange_init();
}

/*
 * This can't do anything because nothing in the kernel image can be freed
 * since it's not in kernel physical memory.
 */

void free_initmem(void)
{
}

void *uml_kmalloc(int size, int flags)
{
	return kmalloc(size, flags);
}
