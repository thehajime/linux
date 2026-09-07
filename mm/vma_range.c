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
#include "internal.h"

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
	dst->vm_swmmu_pt_range = NULL;
#endif
	return 0;
}

void vma_backend_split_adjust(struct vm_area_struct *vma,
			      unsigned long addr)
{
}

#endif /* !CONFIG_MMU */

void vma_remove_detached(struct vma_munmap_struct *vms,
			struct ma_state *mas_detach,
			struct mm_struct *mm,
			vma_remove_detached_fn remove)
{
	struct vm_area_struct *vma;

	if (!mm || !remove)
		return;

	mas_set(mas_detach, 0);

	mas_for_each(mas_detach, vma, ULONG_MAX)
		remove(mm, vma);
}

void vma_replace_init(struct vma_replace_struct *vrs,
		struct vma_munmap_struct *vms,
		struct vm_area_struct *insert,
		const struct vma_backend_ops *backend)
{
	memset(vrs, 0, sizeof(*vrs));

	vrs->vms = vms;
	vrs->insert = insert;
	vrs->backend = backend;
}

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

/*
 * vma_init_munmap() - Initializer wrapper for vma_munmap_struct
 * @vms: The vma munmap struct
 * @vmi: The vma iterator
 * @vma: The first vm_area_struct to munmap
 * @start: The aligned start address to munmap
 * @end: The aligned end address to munmap
 * @uf: The userfaultfd list_head
 * @unlock: Unlock after the operation.  Only unlocked on success
 */
void vma_init_munmap(struct vma_munmap_struct *vms,
		struct vma_iterator *vmi, struct vm_area_struct *vma,
		unsigned long start, unsigned long end, struct list_head *uf,
		bool unlock, const struct vma_backend_ops *backend)
{
	vms->vmi = vmi;
	vms->vma = vma;
	vms->backend = backend;
	if (vma) {
		vms->start = start;
		vms->end = end;
	} else {
		vms->start = vms->end = 0;
	}
	vms->unlock = unlock;
	vms->uf = uf;
	vms->vma_count = 0;
	vms->nr_pages = vms->locked_vm = vms->nr_accounted = 0;
	vms->exec_vm = vms->stack_vm = vms->data_vm = 0;
	vms->unmap_start = FIRST_USER_ADDRESS;
	vms->unmap_end = USER_PGTABLES_CEILING;
	vms->clear_ptes = false;
}

/*
 * vma_gather_range() - Put all VMAs within a range into a maple tree
 * for removal at a later date.  Handles splitting first and last if necessary
 * and marking the vmas as isolated.
 *
 * @vms: The vma munmap struct
 * @mas_detach: The maple state tracking the detached tree
 *
 * Return: 0 on success, error otherwise
 */
int vma_gather_range(struct vma_munmap_struct *vms, struct ma_state *mas_detach)
{
	struct vm_area_struct *next = NULL;
	int error;

	/*
	 * If we need to split any vma, do it now to save pain later.
	 * Does it split the first one?
	 */
	if (vms->start > vms->vma->vm_start) {

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 */
		if (vms->end < vms->vma->vm_end &&
		    vms->vma->vm_mm->map_count >= get_sysctl_max_map_count()) {
			error = -ENOMEM;
			goto map_count_exceeded;
		}

		/* Don't bother splitting the VMA if we can't unmap it anyway */
		if (vma_is_sealed(vms->vma)) {
			error = -EPERM;
			goto start_split_failed;
		}

		error = vma_split_backend(vms->vmi, vms->vma, vms->start, 1, vms->backend);
		if (error)
			goto start_split_failed;
	}
	vms->prev = vma_prev(vms->vmi);
	if (vms->prev)
		vms->unmap_start = vms->prev->vm_end;

	/*
	 * Detach a range of VMAs from the mm. Using next as a temp variable as
	 * it is always overwritten.
	 */
	for_each_vma_range(*(vms->vmi), next, vms->end) {
		long nrpages;

		if (vma_is_sealed(next)) {
			error = -EPERM;
			goto modify_vma_failed;
		}
		/* Does it split the end? */
		if (next->vm_end > vms->end) {
			error = vma_split_backend(vms->vmi, next, vms->end, 0, vms->backend);
			if (error)
				goto end_split_failed;
		}
		vma_start_write(next);
		mas_set(mas_detach, vms->vma_count++);
		error = mas_store_gfp(mas_detach, next, GFP_KERNEL);
		if (error)
			goto munmap_gather_failed;

		vma_mark_detached(next);
		nrpages = vma_pages(next);

		vms->nr_pages += nrpages;
		if (vma_test(next, VMA_LOCKED_BIT))
			vms->locked_vm += nrpages;

		if (vma_test(next, VMA_ACCOUNT_BIT))
			vms->nr_accounted += nrpages;

		if (is_exec_mapping(next->vm_flags))
			vms->exec_vm += nrpages;
		else if (is_stack_mapping(next->vm_flags))
			vms->stack_vm += nrpages;
		else if (is_data_mapping_vma_flags(&next->flags))
			vms->data_vm += nrpages;

		if (vms->uf) {
			/*
			 * If userfaultfd_unmap_prep returns an error the vmas
			 * will remain split, but userland will get a
			 * highly unexpected error anyway. This is no
			 * different than the case where the first of the two
			 * __split_vma fails, but we don't undo the first
			 * split, despite we could. This is unlikely enough
			 * failure that it's not worth optimizing it for.
			 */
			error = userfaultfd_unmap_prep(next, vms->start,
						       vms->end, vms->uf);
			if (error)
				goto userfaultfd_error;
		}
#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
		BUG_ON(next->vm_start < vms->start);
		BUG_ON(next->vm_start > vms->end);
#endif
	}

	vms->next = vma_next(vms->vmi);
	if (vms->next)
		vms->unmap_end = vms->next->vm_start;

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	/* Make sure no VMAs are about to be lost. */
	{
		MA_STATE(test, mas_detach->tree, 0, 0);
		struct vm_area_struct *vma_mas, *vma_test;
		int test_count = 0;

		vma_iter_set(vms->vmi, vms->start);
		rcu_read_lock();
		vma_test = mas_find(&test, vms->vma_count - 1);
		for_each_vma_range(*(vms->vmi), vma_mas, vms->end) {
			BUG_ON(vma_mas != vma_test);
			test_count++;
			vma_test = mas_next(&test, vms->vma_count - 1);
		}
		rcu_read_unlock();
		BUG_ON(vms->vma_count != test_count);
	}
#endif

	while (vma_iter_addr(vms->vmi) > vms->start)
		vma_iter_prev_range(vms->vmi);

	vms->clear_ptes = true;
	return 0;

userfaultfd_error:
munmap_gather_failed:
end_split_failed:
modify_vma_failed:
	vma_reattach_vmas(mas_detach);
start_split_failed:
map_count_exceeded:
	return error;
}

/*
 * vma_reattach_vmas() - Undo any munmap work and free resources
 * @mas_detach: The maple state with the detached maple tree
 *
 * Reattach any detached vmas and free up the maple tree used to track the vmas.
 */
void vma_reattach_vmas(struct ma_state *mas_detach)
{
	struct vm_area_struct *vma;

	mas_set(mas_detach, 0);
	mas_for_each(mas_detach, vma, ULONG_MAX)
		vma_mark_attached(vma);

	__mt_destroy(mas_detach->tree);
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
	bool swmmu_prepared = false;
	int ret;

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

	/* prep */
	if (vma->vm_swmmu_pt_range) {
		ret = vmg->backend->expand_prepare(vma, vmg->end,
						&vmg->backend_state);
		if (ret)
			return ret;

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
		vmg->backend->expand_commit(vmg->target, vmg->backend_state);

	validate_mm(vmg->mm);
	nommu_swmmu_validate(vmg->mm);

	return 0;
abort_swmmu:
	if (swmmu_prepared)
		vmg->backend->expand_abort(vmg->target, &vmg->backend_state);

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

int vma_replace_prepare(struct vma_replace_struct *vrs,
			struct ma_state *mas_detach)
{
	struct vma_munmap_struct *vms;
	int ret;

	if (!vrs || !vrs->vms || !vrs->insert || !mas_detach)
		return -EINVAL;

	vms = vrs->vms;

	if (vrs->backend && vrs->backend->replace_prepare) {
		ret = vrs->backend->replace_prepare(vrs);
		if (ret)
			return ret;
	}

	ret = vma_gather_range(vms, mas_detach);
	if (ret)
		goto abort_backend;

	vma_iter_reset(vms->vmi);
	vma_iter_config(vms->vmi, vms->start, vms->end);

	ret = vma_iter_prealloc(vms->vmi, vrs->insert);
	if (ret) {
		vma_reattach_vmas(mas_detach);
		goto abort_backend;
	}

	return 0;

abort_backend:
	if (vrs->backend && vrs->backend->replace_abort)
		vrs->backend->replace_abort(vrs);

	return ret;
}

void vma_replace_commit(struct vma_replace_struct *vrs,
			struct ma_state *mas_detach,
			struct mm_struct *mm,
			vma_remove_detached_fn remove)
{
	struct vma_munmap_struct *vms;

	if (!vrs || !vrs->vms || !vrs->insert ||
	    !mas_detach || !mm || !remove)
		return;

	vms = vrs->vms;

	/*
	 * vma_replace_prepare() preallocated this store.
	 */
	vma_iter_store_new(vms->vmi, vrs->insert);

	mm->map_count -= vms->vma_count;
	mm->map_count++;

	if (vrs->backend && vrs->backend->replace_commit)
		vrs->backend->replace_commit(vrs);

	vma_remove_detached(vms, mas_detach, mm, remove);

	vrs->insert = NULL;
	vrs->backend_state = NULL;
}

void vma_replace_abort(struct vma_replace_struct *vrs,
		       struct ma_state *mas_detach)
{
	if (!vrs || !vrs->vms || !mas_detach)
		return;

	vma_reattach_vmas(mas_detach);

	if (vrs->backend && vrs->backend->replace_abort)
		vrs->backend->replace_abort(vrs);

	if (vrs->insert) {
		vm_area_free(vrs->insert);
		vrs->insert = NULL;
	}

	vrs->backend_state = NULL;
}
