// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/elf.h>
#include <linux/init.h>

static unsigned int __read_mostly vdso_enabled = 1;
unsigned long um_vdso_addr;

extern unsigned long task_size;
extern char vdso_start[], vdso_end[];

static struct page **vdsop;

static int __init init_vdso(void)
{
	struct page *um_vdso;

	BUG_ON(vdso_end - vdso_start > PAGE_SIZE);

#ifdef CONFIG_MMU
	um_vdso_addr = task_size - PAGE_SIZE;
#endif

	vdsop = kmalloc(sizeof(struct page *), GFP_KERNEL);
	if (!vdsop)
		goto oom;

	um_vdso = alloc_page(GFP_KERNEL);
	if (!um_vdso) {
		kfree(vdsop);

		goto oom;
	}

	copy_page(page_address(um_vdso), vdso_start);
	*vdsop = um_vdso;

#ifndef CONFIG_MMU
	/* this is fine with NOMMU as everything is accessible */
	um_vdso_addr = (unsigned long)page_address(um_vdso);
	pr_debug("vdso_start=%lx um_vdso_addr=%lx pg_um_vdso=%lx",
	       (unsigned long)vdso_start, um_vdso_addr,
	       (unsigned long)page_address(um_vdso));
#endif

	return 0;

oom:
	pr_err("Cannot allocate vdso");
	vdso_enabled = 0;

	return -ENOMEM;
}
subsys_initcall(init_vdso);

#ifdef CONFIG_MMU
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int err;
	struct mm_struct *mm = current->mm;

	if (!vdso_enabled)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	err = install_special_mapping(mm, um_vdso_addr, PAGE_SIZE,
		VM_READ|VM_EXEC|
		VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
		vdsop);

	mmap_write_unlock(mm);

	return err;
}
#endif
