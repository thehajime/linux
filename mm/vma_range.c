

/*
 * Common VMA range-management helpers.
 */

#include <linux/kernel.h>
#include <linux/maple_tree.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/security.h>

#include "vma.h"

void __vma_set_range(struct vm_area_struct *vma,
		     unsigned long start,
		     unsigned long end)
{
	vma->vm_start = start;
	vma->vm_end = end;
}

void vma_set_range(struct vm_area_struct *vma,
		   unsigned long start,
		   unsigned long end,
		   pgoff_t pgoff,
		   pgoff_t anon_pgoff)
{
	__vma_set_range(vma, start, end);
	vma_set_pgoff(vma, pgoff);
	vma_set_anon_pgoff(vma, anon_pgoff);
}
