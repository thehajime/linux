// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/fsgsbase.h>
#include <asm/prctl.h> /* XXX This should get the constants from libc */
#include <registers.h>
#include <os.h>
#include "syscalls.h"

/*
 * The guest libc can change FS, which confuses the host libc.
 * In fact, changing FS directly is not supported (check
 * man arch_prctl). So, whenever we make a host syscall,
 * we should be changing FS to the original FS (not the
 * one set by the guest libc). This original FS is stored
 * in host_fs.
 */
long long host_fs = -1;

int os_x86_arch_prctl(int pid, int option, unsigned long *arg2)
{
	if (!host_has_fsgsbase)
		return os_arch_prctl(pid, option, arg2);

	switch (option) {
	case ARCH_SET_FS:
		wrfsbase((unsigned long)arg2);
		break;
	case ARCH_SET_GS:
		wrgsbase((unsigned long)arg2);
		break;
	case ARCH_GET_FS:
		*arg2 = rdfsbase();
		break;
	case ARCH_GET_GS:
		*arg2 = rdgsbase();
		break;
	default:
		pr_warn("%s: unsupported option: 0x%x", __func__, option);
		return -EINVAL;
	}

	return 0;
}

void arch_set_stack_to_current(void)
{
	current_top_of_stack = task_top_of_stack(current);
	current_ptregs = (long)task_pt_regs(current);
}

void arch_switch_to(struct task_struct *to)
{
	/*
	 * In !CONFIG_MMU, it doesn't ptrace thus,
	 * The FS_BASE registers are saved here.
	 */
	current_top_of_stack = task_top_of_stack(to);
	current_ptregs = (long)task_pt_regs(to);

	if ((to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] == 0) ||
	    (to->mm == NULL))
		return;

	/* this changes the FS on every context switch */
	os_x86_arch_prctl(0, ARCH_SET_FS,
		   (void __user *) to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)]);
}

static int __init um_nommu_setup_hostfs(void)
{
	/* initialize the host_fs value at boottime */
	os_x86_arch_prctl(0, ARCH_GET_FS, (void *)&host_fs);

	return 0;
}
arch_initcall(um_nommu_setup_hostfs);

void os_x86_set_hostfs(void)
{
	if (host_fs == -1)
		um_nommu_setup_hostfs();

	os_x86_arch_prctl(0, ARCH_SET_FS, (void *)host_fs);
}
