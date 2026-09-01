// SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef CONFIG_MMU

void vma_backend_prepare(struct vma_prepare *vp,
			 struct vm_area_struct *vma)
{
	memset(vp, 0, sizeof(*vp));
	vp->vma = vma;
}

void vma_backend_adjust_range(struct vm_area_struct *vma,
			      unsigned long start,
			      unsigned long end)
{
}

void vma_backend_complete(struct vma_prepare *vp,
			  struct vma_iterator *vmi,
			  struct mm_struct *mm)
{
}

#endif /* !CONFIG_MMU */

/**
 * vma_shrink() - Shrink the end of a VMA
 * @vmi: The vma iterator
 * @vma: The VMA to modify
 * @end: The new end
 *
 * Note that the caller may only shrink the end of the VMA.
 *
 * Returns: 0 on success, -ENOMEM otherwise
 */
int vma_shrink(struct vma_iterator *vmi, struct vm_area_struct *vma,
	       unsigned long end)
{
	struct vma_prepare vp;
	unsigned long start = vma->vm_start;

	mmap_assert_write_locked(vma->vm_mm);

	if (end <= start || end > vma->vm_end)
		return -EINVAL;

	VM_WARN_ON_ONCE(end > vma->vm_end);

	vma_iter_config(vmi, end, vma->vm_end);
	if (vma_iter_prealloc(vmi, NULL))
		return -ENOMEM;

	vma_start_write(vma);

	vma_backend_prepare(&vp, vma);
	vma_backend_adjust_range(vma, start, end);

	vma_iter_clear(vmi);
	__vma_set_range(vma, start, end);

	vma_backend_complete(&vp, vmi, vma->vm_mm);
	validate_mm(vma->vm_mm);

	return 0;
}

#ifndef CONFIG_MMU
int vma_expand(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *vma;
	struct vma_iterator *vmi;

	if (!vmg || !vmg->mm || !vmg->vmi)
		return -EINVAL;

	mmap_assert_write_locked(vmg->mm);

	vma = vmg->target;
	vmi = vmg->vmi;

	if (!vma)
		return -EINVAL;

	/*
	 * The first SWMMU implementation supports only expansion
	 * of the existing VMA at its end.
	 */
	if (vmg->start != vma->vm_start ||
	    vmg->end <= vma->vm_end)
		return -EINVAL;

	/*
	 * Do not merge or remove adjacent VMAs yet.
	 */
	if (vmg->next && vmg->next != vma)
		return -EOPNOTSUPP;

	vma_iter_config(vmi, vma->vm_start, vmg->end);
	if (vma_iter_prealloc(vmi, vma))
		return -ENOMEM;

	/*
	 * The old VMA is already attached. Replace its Maple Tree
	 * range with the expanded range.
	 */
	vma->vm_end = vmg->end;
	vma_iter_store_overwrite(vmi, vma);

	validate_mm(vmg->mm);
	return 0;
}
#endif /* !CONFIG_MMU */
