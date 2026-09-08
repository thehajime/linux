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

int vma_move_prepare(struct vma_remap_struct *vrm,
		     struct vm_area_struct *src)
{
	int ret;

	if (!vrm || !src || !vrm->backend || !vrm->backend->move_prepare)
		return -EINVAL;

	vrm->move_backend_active = true;
	vrm->move_backend_prepared = false;
	vrm->new_vma = NULL;
	vrm->new_vma_linked = false;

	ret = vrm->backend->move_prepare(vrm, src, &vrm->new_vma,
		&vrm->new_vma_linked, &vrm->backend_state);
	if (ret)
		return ret;

	if (!vrm->new_vma)
		return -EINVAL;

	vrm->move_backend_prepared = true;
	return 0;
}

void vma_move_commit(struct vma_remap_struct *vrm)
{
	if (!vrm || !vrm->move_backend_prepared)
		return;

	vrm->backend->move_commit(vrm, vrm->vma,
		vrm->new_vma, vrm->backend_state);

	vrm->backend_state = NULL;
	vrm->move_backend_prepared = false;
}

void vma_move_abort(struct vma_remap_struct *vrm)
{
	if (!vrm || !vrm->move_backend_prepared)
		return;

	vrm->backend->move_abort(vrm, vrm->vma,
		vrm->new_vma, vrm->backend_state);

	vrm->backend_state = NULL;
	vrm->move_backend_prepared = false;
}

int vma_move_at(struct vma_remap_struct *vrm, struct vm_area_struct *src,
		struct vm_area_struct *dst)
{
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	struct vma_munmap_struct vms;
	VMA_ITERATOR(dst_vmi, vrm->mm, dst->vm_start);
	int ret;

	if (!vrm || !vrm->mm || !src || !dst ||
	    !vrm->backend ||
	    !vrm->backend->remove_detached)
		return -EINVAL;

	if (src->vm_start != vrm->addr ||
	    src->vm_end != vrm->addr + vrm->old_len)
		return -EOPNOTSUPP;

	/*
	 * The first move implementation handles a complete source VMA.
	 * MREMAP_FIXED and overlapping destinations use other paths.
	 */
	if (dst->vm_start != vrm->new_addr ||
	    dst->vm_end != vrm->new_addr + vrm->new_len)
		return -EINVAL;

	mt_init_flags(&mt_detach,
		      vrm->mm->mm_mt.ma_flags & MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	VMA_ITERATOR(vmi, vrm->mm, src->vm_start);
	vrm->vmi = &vmi;

	vma_init_munmap(&vms, vrm->vmi, src,
			src->vm_start, src->vm_end,
			vrm->uf_unmap, false,
			vrm->backend);

	/*
	 * Prepare the backend while the source is still fully valid.
	 */
	ret = vma_move_prepare(vrm, src);
	if (ret)
		goto out_destroy;

	/*
	 * Gather the source into the detached tree. For this first
	 * version, the exact-range check above means no split is needed.
	 */
	ret = vma_gather_range(&vms, &mas_detach);
	if (ret)
		goto abort_backend;

	/*
	 * Clear the source range from the main VMA tree. The detached
	 * source remains available for rollback.
	 */
	ret = vma_iter_clear_gfp(
		vrm->vmi, src->vm_start, src->vm_end, GFP_KERNEL);
	if (ret)
		goto reattach;

	/*
	 * Prepare the destination insertion after clearing the source.
	 * This avoids using two stale Maple states.
	 */
	vma_iter_reset(&dst_vmi);
	vma_iter_config(&dst_vmi, dst->vm_start, dst->vm_end);

	ret = vma_iter_prealloc(&dst_vmi, dst);
	if (ret)
		goto reattach;

	/*
	 * No operation below this point may fail.
	 */
	vma_start_write(src);
	vma_start_write(dst);

	vma_iter_store_new(&dst_vmi, dst);

	/*
	 * One source VMA is replaced by one destination VMA.
	 */
	vrm->mm->map_count -= vms.vma_count;
	vrm->mm->map_count++;

	vma_move_commit(vrm);

	vma_remove_detached(&vms, &mas_detach, vrm->mm,
			    vrm->backend->remove_detached);

	__mt_destroy(&mt_detach);
	return 0;

reattach:
	vma_reattach_vmas(&mas_detach);
abort_backend:
	vma_move_abort(vrm);
out_destroy:
	__mt_destroy(&mt_detach);
	return ret;
}

unsigned long vma_move_mapping(struct vma_remap_struct *vrm,
			struct pagetable_move_control *pmc)
{
	if (!vrm || !pmc)
		return 0;

	if (!vrm->backend || !vrm->backend->move_mapping)
		return pmc->len_in;

	return vrm->backend->move_mapping(vrm, pmc);
}

void vma_move_rollback(struct vma_remap_struct *vrm,
		unsigned long moved_len)
{
	if (!vrm || !vrm->backend || !vrm->backend->move_rollback)
		return;

	vrm->backend->move_rollback(vrm, moved_len);
}

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
		const struct vma_mapping_ops *backend)
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
		bool unlock, const struct vma_mapping_ops *backend)
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
		if (!vmg->backend ||
			!vmg->backend->expand_prepare)
			return -EOPNOTSUPP;

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
		      const struct vma_mapping_ops *backend)
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

void vma_move_replace_init(struct vma_move_replace_struct *vmrs,
			   struct vma_remap_struct *remap,
			   struct vm_area_struct *src,
			   struct vm_area_struct *dst,
			   struct vma_munmap_struct *target_vms,
			   struct ma_state *target_detach,
			   const struct vma_mapping_ops *backend)
{
	memset(vmrs, 0, sizeof(*vmrs));

	vmrs->mm = remap->mm;
	vmrs->remap = remap;
	vmrs->src = src;
	vmrs->dst = dst;
	vmrs->target_vms = target_vms;
	vmrs->target_detach = target_detach;
	vmrs->backend = backend;
}

int vma_move_link_destination(struct vma_remap_struct *vrm)
{
	struct vm_area_struct *vma;
	int ret;

	if (!vrm || !vrm->mm || !vrm->new_vma)
		return -EINVAL;

	if (vrm->new_vma_linked)
		return 0;

	vma = vrm->new_vma;
	VMA_ITERATOR(vmi, vrm->mm, vma->vm_start);

	if (vma_iter_load(&vmi))
		return -EEXIST;

	vma_iter_config(&vmi, vma->vm_start, vma->vm_end);

	ret = vma_iter_prealloc(&vmi, vma);
	if (ret)
		return ret;

	vma_start_write(vma);
	vma_iter_store_new(&vmi, vma);
	vrm->mm->map_count++;
	vrm->new_vma_linked = true;

	return 0;
}

/* Was this VMA ever forked from a parent, i.e. maybe contains CoW mappings? */
static bool vma_is_fork_child(struct vm_area_struct *vma)
{
	/*
	 * The list_is_singular() test is to avoid merging VMA cloned from
	 * parents. This can improve scalability caused by the anon_vma root
	 * lock.
	 */
	return vma && vma->anon_vma && !list_is_singular(&vma->anon_vma_chain);
}

static inline bool is_mergeable_vma(struct vma_merge_struct *vmg, bool merge_next)
{
	struct vm_area_struct *vma = merge_next ? vmg->next : vmg->prev;
	vma_flags_t diff;

#ifdef CONFIG_MMU
	if (!mpol_equal(vmg->policy, vma_policy(vma)))
		return false;
#endif
	diff = vma_flags_diff_pair(&vma->flags, &vmg->vma_flags);
	vma_flags_clear_mask(&diff, VMA_IGNORE_MERGE_FLAGS);

	if (!vma_flags_empty(&diff))
		return false;
	if (vma->vm_file != vmg->file)
		return false;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vmg->uffd_ctx))
		return false;
	if (!anon_vma_name_eq(anon_vma_name(vma), vmg->anon_name))
		return false;
	return true;
}


static bool is_mergeable_anon_vma(struct vma_merge_struct *vmg, bool merge_next)
{
	struct vm_area_struct *tgt = merge_next ? vmg->next : vmg->prev;
	struct vm_area_struct *src = vmg->middle; /* existing merge case. */
	struct anon_vma *tgt_anon = tgt->anon_vma;
	struct anon_vma *src_anon = vmg->anon_vma;

	/*
	 * We _can_ have !src, vmg->anon_vma via copy_vma(). In this instance we
	 * will remove the existing VMA's anon_vma's so there's no scalability
	 * concerns.
	 */
	VM_WARN_ON(src && src_anon != src->anon_vma);

	/* Case 1 - we will dup_anon_vma() from src into tgt. */
	if (!tgt_anon && src_anon) {
		struct vm_area_struct *copied_from = vmg->copied_from;

		if (vma_is_fork_child(src))
			return false;
		if (vma_is_fork_child(copied_from))
			return false;

		return true;
	}
	/* Case 2 - we will simply use tgt's anon_vma. */
	if (tgt_anon && !src_anon)
		return !vma_is_fork_child(tgt);
	/* Case 3 - the anon_vma's are already shared. */
	return src_anon == tgt_anon;
}

/*
 * Does this merge require that adjacent VMAs must have adjacent anonymous page
 * offsets in addition to having adjacent vma->vm_pgoff?
 *
 * This is only required for MAP_PRIVATE-file backed mappings as the page offset
 * for pure anonymous VMAs is equal to the anonymous page offset.
 *
 * Read-only shared mappings (with VMA_SHARED_BIT cleared) are always unfaulted
 * so automatically have correct anonymous page offset (as it is always updated
 * on remap).
 *
 * 'Special' mappings in the sense of VDSO, VVAR etc. have !file but would in
 * any case not be candidates for merge nor be mergeable.
 */
static bool needs_adjacent_anon_pgoff(const struct vma_merge_struct *vmg)
{
	return vmg->file && vma_flags_is_cow_mapping(&vmg->vma_flags);
}

/* We can only remove VMAs when merging if they do not have a close hook. */
static bool can_merge_remove_vma(struct vm_area_struct *vma)
{
	return !vma->vm_ops || !vma->vm_ops->close;
}

/*
 * Return true if we can merge this (vma_flags,anon_vma,file,vm_pgoff)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 *
 * We assume the vma may be removed as part of the merge.
 */
static bool can_vma_merge_before(struct vma_merge_struct *vmg)
{
	if (!is_mergeable_vma(vmg, /* merge_next = */ true))
		return false;
	if (!is_mergeable_anon_vma(vmg, /* merge_next = */ true))
		return false;
	if (vmg_end_pgoff(vmg) != vma_start_pgoff(vmg->next))
		return false;
	if (needs_adjacent_anon_pgoff(vmg) &&
	    vmg_end_anon_pgoff(vmg) != vma_start_anon_pgoff(vmg->next))
		return false;
	return true;
}

/*
 * Return true if we can merge this (vma_flags,anon_vma,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We assume that vma is not removed as part of the merge.
 */
static bool can_vma_merge_after(struct vma_merge_struct *vmg)
{
	if (!is_mergeable_vma(vmg, /* merge_next = */ false))
		return false;
	if (!is_mergeable_anon_vma(vmg, /* merge_next = */ false))
		return false;
	if (vma_end_pgoff(vmg->prev) != vmg_start_pgoff(vmg))
		return false;
	if (needs_adjacent_anon_pgoff(vmg) &&
	    vma_end_anon_pgoff(vmg->prev) != vmg_start_anon_pgoff(vmg))
		return false;
	return true;
}

/*
 * Can the proposed VMA be merged with the left (previous) VMA taking into
 * account the start position of the proposed range.
 */
static bool can_vma_merge_left(struct vma_merge_struct *vmg)

{
	return vmg->prev && vmg->prev->vm_end == vmg->start &&
		can_vma_merge_after(vmg);
}

/*
 * Can the proposed VMA be merged with the right (next) VMA taking into
 * account the end position of the proposed range.
 *
 * In addition, if we can merge with the left VMA, ensure that left and right
 * anon_vma's are also compatible.
 */
static bool can_vma_merge_right(struct vma_merge_struct *vmg,
				bool can_merge_left)
{
	struct vm_area_struct *next = vmg->next;
	struct vm_area_struct *prev;

	if (!next || vmg->end != next->vm_start || !can_vma_merge_before(vmg))
		return false;

	if (!can_merge_left)
		return true;

	/*
	 * If we can merge with prev (left) and next (right), indicating that
	 * each VMA's anon_vma is compatible with the proposed anon_vma, this
	 * does not mean prev and next are compatible with EACH OTHER.
	 *
	 * We therefore check this in addition to mergeability to either side.
	 */
	prev = vmg->prev;
	return !prev->anon_vma || !next->anon_vma ||
		prev->anon_vma == next->anon_vma;
}

/*
 * vma_merge_new_range - Attempt to merge a new VMA into address space
 *
 * @vmg: Describes the VMA we are adding, in the range @vmg->start to @vmg->end
 *       (exclusive), which we try to merge with any adjacent VMAs if possible.
 *
 * We are about to add a VMA to the address space starting at @vmg->start and
 * ending at @vmg->end. There are three different possible scenarios:
 *
 * 1. There is a VMA with identical properties immediately adjacent to the
 *    proposed new VMA [@vmg->start, @vmg->end) either before or after it -
 *    EXPAND that VMA:
 *
 * Proposed:       |-----|  or  |-----|
 * Existing:  |----|                  |----|
 *
 * 2. There are VMAs with identical properties immediately adjacent to the
 *    proposed new VMA [@vmg->start, @vmg->end) both before AND after it -
 *    EXPAND the former and REMOVE the latter:
 *
 * Proposed:       |-----|
 * Existing:  |----|     |----|
 *
 * 3. There are no VMAs immediately adjacent to the proposed new VMA or those
 *    VMAs do not have identical attributes - NO MERGE POSSIBLE.
 *
 * In instances where we can merge, this function returns the expanded VMA which
 * will have its range adjusted accordingly and the underlying maple tree also
 * adjusted.
 *
 * Returns: In instances where no merge was possible, NULL. Otherwise, a pointer
 *          to the VMA we expanded.
 *
 * This function adjusts @vmg to provide @vmg->next if not already specified,
 * and adjusts [@vmg->start, @vmg->end) to span the expanded range.
 *
 * ASSUMPTIONS:
 * - The caller must hold a WRITE lock on the mm_struct->mmap_lock.
 * - The caller must have determined that [@vmg->start, @vmg->end) is empty,
     other than VMAs that will be unmapped should the operation succeed.
 * - The caller must have specified the previous vma in @vmg->prev.
 * - The caller must have specified the next vma in @vmg->next.
 * - The caller must have positioned the vmi at or before the gap.
 */
struct vm_area_struct *vma_merge_new_range(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *prev = vmg->prev;
	struct vm_area_struct *next = vmg->next;
	unsigned long end = vmg->end;
	bool can_merge_left, can_merge_right;

	mmap_assert_write_locked(vmg->mm);
	VM_WARN_ON_VMG(vmg->middle, vmg);
	VM_WARN_ON_VMG(vmg->target, vmg);
	/* vmi must point at or before the gap. */
	VM_WARN_ON_VMG(vma_iter_addr(vmg->vmi) > end, vmg);

	vmg->state = VMA_MERGE_NOMERGE;

	/* Special VMAs are unmergeable, also if no prev/next. */
	if (vma_flags_test_any_mask(&vmg->vma_flags, VMA_SPECIAL_FLAGS) ||
	    (!prev && !next))
		return NULL;

	can_merge_left = can_vma_merge_left(vmg);
	can_merge_right = !vmg->just_expand && can_vma_merge_right(vmg, can_merge_left);

	/* If we can merge with the next VMA, adjust vmg accordingly. */
	if (can_merge_right) {
		vmg->end = next->vm_end;
		vmg->target = next;
	}

	/* If we can merge with the previous VMA, adjust vmg accordingly. */
	if (can_merge_left) {
		vmg->start = prev->vm_start;
		vmg->target = prev;
		vmg->pgoff = vma_start_pgoff(prev);
		vmg->anon_pgoff = vma_start_anon_pgoff(prev);

		/*
		 * If this merge would result in removal of the next VMA but we
		 * are not permitted to do so, reduce the operation to merging
		 * prev and vma.
		 */
		if (can_merge_right && !can_merge_remove_vma(next))
			vmg->end = end;

		/* In expand-only case we are already positioned at prev. */
		if (!vmg->just_expand) {
			/* Equivalent to going to the previous range. */
			vma_prev(vmg->vmi);
		}
	}

	/*
	 * Now try to expand adjacent VMA(s). This takes care of removing the
	 * following VMA if we have VMAs on both sides.
	 */
	if (vmg->target && !vma_expand(vmg)) {
		khugepaged_enter_vma(vmg->target, vmg->vm_flags);
		vmg->state = VMA_MERGE_SUCCESS;
		return vmg->target;
	}

	return NULL;
}

/*
 * Expand vma by delta bytes, potentially merging with an immediately adjacent
 * VMA with identical properties.
 */
struct vm_area_struct *vma_merge_extend(struct vma_iterator *vmi,
					struct vm_area_struct *vma,
					unsigned long delta,
					const struct vma_mapping_ops *backend)
{
	VMG_VMA_STATE(vmg, vmi, vma, vma, vma->vm_end, vma->vm_end + delta);

	vmg.backend = backend;
	vmg.next = vma_iter_next_rewind(vmi, NULL);
	vmg.middle = NULL; /* We use the VMA to populate VMG fields only. */

	return vma_merge_new_range(&vmg);
}

#ifndef CONFIG_MMU

int do_vmi_munmap(struct vma_iterator *vmi, struct mm_struct *mm,
		  unsigned long start, size_t len,
		  struct list_head *uf, bool unlock)
{
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	struct vma_munmap_struct vms;
	struct vm_area_struct *vma;
	const struct vma_mapping_ops *backend;
	int ret;

	if (!vmi || !mm || !len)
		return -EINVAL;

	if (start > ULONG_MAX - len)
		return -EINVAL;

	len = PAGE_ALIGN(len);
	if (!len || start > ULONG_MAX - len)
		return -EINVAL;

	vma = vma_find(vmi, start + len);
	if (!vma)
		return 0;

	backend = vma_mapping_ops_for_mm(mm);
	if (!backend || !backend->remove_detached)
		return -EOPNOTSUPP;

	mt_init_flags(&mt_detach,
		      vmi->mas.tree->ma_flags &
		      MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	vma_init_munmap(&vms, vmi, vma,
			start, start + len,
			uf, unlock, backend);

	ret = vma_gather_range(&vms, &mas_detach);
	if (ret)
		goto out_destroy;

	ret = vma_iter_clear_gfp(vmi, start, start + len,
				 GFP_KERNEL);
	if (ret) {
		vma_reattach_vmas(&mas_detach);
		goto out_destroy;
	}

	/*
	 * The original mm->mm_mt range is now clear. The detached
	 * VMAs remain in mas_detach until backend cleanup completes.
	 */
	mm->map_count -= vms.vma_count;

	vma_remove_detached(&vms, &mas_detach, mm,
			    backend->remove_detached);


out_destroy:
	__mt_destroy(&mt_detach);

	validate_mm(mm);

#ifdef CONFIG_NOMMU_SWMMU
	nommu_swmmu_validate(mm);
#endif

	if (unlock) {
		mmap_write_downgrade(mm);
		mmap_read_unlock(mm);
	}
	return ret;
}

#endif /* !CONFIG_MMU */

unsigned long vma_get_unmapped_area(struct vma_remap_struct *vrm)
{
	if (!vrm || !vrm->backend ||
	    !vrm->backend->get_unmapped_area)
		return -EINVAL;

	return vrm->backend->get_unmapped_area(vrm);
}
