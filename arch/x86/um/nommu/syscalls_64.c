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
#include <asm/prctl.h> /* XXX This should get the constants from libc */
#include <registers.h>
#include <os.h>
#include "syscalls.h"

void arch_switch_to(struct task_struct *to)
{
	/*
	 * In !CONFIG_MMU, it doesn't ptrace thus,
	 * The FS_BASE/GS_BASE registers are saved here.
	 */
	current_top_of_stack = task_top_of_stack(to);
	current_ptregs = (long)task_pt_regs(to);

	if ((to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] == 0) ||
	    (to->mm == NULL))
		return;

	/* this changes the FS on every context switch */
	arch_prctl(to, ARCH_SET_FS,
		   (void __user *) to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)]);
}

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}
