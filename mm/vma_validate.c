// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/maple_tree.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/security.h>

#include "vma.h"

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
void validate_mm(struct mm_struct *mm)
{
	int bug = 0;
	int i = 0;
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	mt_validate(&mm->mm_mt);
	for_each_vma(vmi, vma) {
#ifdef CONFIG_DEBUG_VM_RB
		struct anon_vma *anon_vma = vma->anon_vma;
		struct anon_vma_chain *avc;
#endif
		unsigned long vmi_start, vmi_end;
		bool warn = 0;

		vmi_start = vma_iter_addr(&vmi);
		vmi_end = vma_iter_end(&vmi);
		if (VM_WARN_ON_ONCE_MM(vma->vm_end != vmi_end, mm))
			warn = 1;

		if (VM_WARN_ON_ONCE_MM(vma->vm_start != vmi_start, mm))
			warn = 1;

		if (warn) {
			pr_emerg("issue in %s\n", current->comm);
			dump_stack();
			dump_vma(vma);
			pr_emerg("tree range: %px start %lx end %lx\n", vma,
				 vmi_start, vmi_end - 1);
			vma_iter_dump_tree(&vmi);
		}

#ifdef CONFIG_DEBUG_VM_RB
		if (anon_vma) {
			anon_vma_lock_read(anon_vma);
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				anon_rmap_tree_verify(avc);
			anon_vma_unlock_read(anon_vma);
		}
#endif
		/* Check for a infinite loop */
		if (++i > mm->map_count + 10) {
			i = -1;
			break;
		}
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d vma iterator %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}
#endif /* CONFIG_DEBUG_VM_MAPLE_TREE */
