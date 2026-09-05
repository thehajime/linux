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
#include <linux/pagemap.h>
#include <linux/nommu_swmmu.h>

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
			 struct vm_area_struct *vma,
			 struct vm_area_struct *insert)
{
	memset(vp, 0, sizeof(*vp));
	vp->vma = vma;
	vp->insert = insert;
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
	if (!vp->insert)
		return;

	/*
	 * Storing the inserted range replaces the corresponding part
	 * of the old VMA Maple entry and leaves the remainder associated
	 * with vp->vma.
	 */
	vma_iter_store_new(vmi, vp->insert);
	mm->map_count++;
}

int vma_backend_dup(struct vm_area_struct *src,
		    struct vm_area_struct *dst)
{
#ifdef CONFIG_NOMMU_SWMMU
	dst->vm_swmmu_data = NULL;
#endif
	return 0;
}

void vma_backend_split_adjust(struct vm_area_struct *vma,
			      unsigned long addr)
{
}

#endif /* !CONFIG_MMU */

static void
debug_dump_vma_range(const char *where,
		     struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	VMA_ITERATOR(vmi, mm, vma->vm_start);
	struct vm_area_struct *tree_vma;

	tree_vma = vma_iter_load(&vmi);

	pr_info("SWMMU VMA %s: vma=%px fields=[%#lx,%#lx) "
		"tree=%px iterator=[%#lx,%#lx)\n",
		where,
		vma,
		vma->vm_start,
		vma->vm_end,
		tree_vma,
		vma_iter_addr(&vmi),
		vma_iter_end(&vmi));
}



int vma_range_count_overlaps(struct mm_struct *mm, unsigned long start,
			unsigned long end, struct vm_area_struct **single)
{
	VMA_ITERATOR(vmi, mm, start);
	struct vm_area_struct *vma;
	int count = 0;

	if (!mm || !single || start >= end)
		return -EINVAL;

	mmap_assert_locked(mm);
	*single = NULL;

	vma = vma_find(&vmi, end);
	while (vma && vma->vm_start < end) {
		if (vma->vm_end > start &&
		    vma->vm_start < end) {
			if (count == 0)
				*single = vma;

			count++;

			/*
			 * Callers currently only distinguish zero,
			 * one, or multiple overlaps.
			 */
			if (count > 1)
				return 2;
		}
		vma = vma_next(&vmi);
	}
	return count;
}


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
	unsigned long old_end = vma->vm_end;

	mmap_assert_write_locked(vma->vm_mm);

	if (end <= start || end > vma->vm_end)
		return -EINVAL;

	VM_WARN_ON_ONCE(end > vma->vm_end);

	vma_iter_reset(vmi);
	vma_iter_config(vmi, end, old_end);
	if (vma_iter_prealloc(vmi, NULL))
		return -ENOMEM;

	vma_start_write(vma);

	vma_backend_prepare(&vp, vma, NULL);
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
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_vma_tx tx = {};
	bool swmmu_prepared = false;
	int ret;
	bool space_locked = false;

	if (!vmg || !vmg->mm || !vmg->vmi)
		return -EINVAL;

	mmap_assert_write_locked(vmg->mm);

	vma = vmg->target;
	vmi = vmg->vmi;

	if (!vma)
		return -EINVAL;

	/* FIXME: Current implementation supports only expansion at the end. */
	if (vmg->start != vma->vm_start ||
	    vmg->end <= vma->vm_end)
		return -EINVAL;

	/* FIXME: Adjacent-VMA removal/merge will be implemented later. */
	if (vmg->next && vmg->next != vma &&
		vmg->end > vmg->next->vm_start)
		return -ENOMEM;

	space = vma->vm_mm->swmmu_space;

	/* prep */
	if (vma->vm_swmmu_data) {
		if (!space)
			return -EINVAL;

		down_write(&space->lock);
		space_locked = true;

		ret = nommu_swmmu_expand_prepare(space, vma, vmg->end,
					   &tx);
		if (ret)
			goto unlock_space;

		swmmu_prepared = true;
	}

	/*
	 * Preallocate the Maple Tree update before changing the VMA.
	 */
	vma_iter_reset(vmi);
	vma_iter_config(vmi, vma->vm_start, vmg->end);

	ret = vma_iter_prealloc(vmi, vma);
	if (ret)
		goto abort_swmmu;

	vma->vm_end = vmg->end;
	vma_iter_store_overwrite(vmi, vma);

	if (swmmu_prepared)
		nommu_swmmu_vma_expand_commit(vmg->target, &tx);

	if (space_locked)
		up_write(&space->lock);

	validate_mm(vmg->mm);
	nommu_swmmu_validate(vmg->mm);

	return 0;
abort_swmmu:
	if (swmmu_prepared)
		nommu_swmmu_vma_expand_abort(space, &tx);
unlock_space:
	if (space_locked)
		up_write(&space->lock);

	return ret;
}
#endif /* !CONFIG_MMU */

int vma_split_backend(struct vma_iterator *vmi,
		      struct vm_area_struct *vma,
		      unsigned long addr,
		      int new_below,
		      const struct vma_backend_ops *backend)
{
	struct vma_prepare vp;
	struct vm_area_struct *new;
	void *backend_state = NULL;
	int ret;

	if (!vma || addr <= vma->vm_start || addr >= vma->vm_end)
		return -EINVAL;

	if (vma->vm_ops && vma->vm_ops->may_split) {
		ret = vma->vm_ops->may_split(vma, addr);
		if (ret)
			return ret;
	}

	new = vm_area_dup(vma);
	if (!new)
		return -ENOMEM;

	if (new_below) {
		new->vm_end = addr;
	} else {
		new->vm_start = addr;
		vma_add_pgoff(new, linear_page_delta(vma, addr));
	}

	ret = vma_backend_dup(vma, new);
	if (ret)
		goto err_free_vma;

	if (new->vm_file)
		get_file(new->vm_file);

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	if (backend && backend->split_prepare) {
		ret = backend->split_prepare(vma, new, addr,
					     new_below, &backend_state);
		if (ret)
			goto err_file;
	}

	vma_iter_reset(vmi);
	vma_iter_config(vmi, new->vm_start, new->vm_end);
	ret = vma_iter_prealloc(vmi, new);
	if (ret)
		goto err_backend;

	vma_start_write(vma);
	vma_start_write(new);

	vma_backend_prepare(&vp, vma, new);
	vma_backend_split_adjust(vma, addr);

	if (new_below) {
		/*
		 * new: [old_start, addr)
		 * vma: [addr, old_end)
		 */
		vma->vm_start = addr;
		vma_add_pgoff(vma, linear_page_delta(new, addr));
	} else {
		/*
		 * vma: [old_start, addr)
		 * new: [addr, old_end)
		 */
		vma->vm_end = addr;
	}

	/*
	 * For MMU this calls vma_complete().
	 * For NOMMU it must store vp.insert and increment map_count.
	 */
	vma_backend_complete(&vp, vmi, vma->vm_mm);

	if (backend && backend->split_commit)
		backend->split_commit(vma, new, addr,
				      new_below, backend_state);

	validate_mm(vma->vm_mm);

	if (new_below)
		vma_next(vmi);
	else
		vma_prev(vmi);

	return 0;

err_backend:
	if (backend && backend->split_abort)
		backend->split_abort(vma, new, backend_state);
err_file:
	if (new->vm_file)
		fput(new->vm_file);
err_free_vma:
	vm_area_free(new);
	return ret;
}
