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
#include <asm/thread_info.h>

long long os_nsecs(void);

/*
 * The guest libc can change FS, which confuses the host libc.
 * In fact, changing FS directly is not supported (check
 * man arch_prctl). So, whenever we make a host syscall,
 * we should be changing FS to the original FS (not the
 * one set by the guest libc). This original FS is stored
 * in host_fs.
 */
long long host_fs = -1;

long arch_prctl(struct task_struct *task, int option,
		unsigned long __user *arg2)
{
	long ret = -EINVAL;
	struct uml_pt_regs *regs = &current->thread.regs.regs;

	switch (option) {
	case ARCH_SET_FS:
#ifdef CONFIG_MMU
		current->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
#else
		if (host_fs == -1) {
			os_arch_prctl(0, ARCH_GET_FS, (void *)&host_fs);
		}
		regs->gp[HOST_FS] = arg2;
#endif
		ret = 0;
		break;
	case ARCH_SET_GS:
#ifdef CONFIG_MMU
		current->thread.regs.regs.gp[GS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
#else
		regs->gp[HOST_GS] = arg2;
#endif
		ret = 0;
		break;
	case ARCH_GET_FS:
		ret = put_user(current->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)], arg2);
		break;
	case ARCH_GET_GS:
		ret = put_user(current->thread.regs.regs.gp[GS_BASE / sizeof(unsigned long)], arg2);
		break;
	}

	return ret;
}

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return arch_prctl(current, option, (unsigned long __user *) arg2);
}

void arch_switch_to(struct task_struct *to)
{
	/*
	 * Nothing needs to be done on x86_64.
	 * The FS_BASE/GS_BASE registers are saved in the ptrace register set.
	 */
#ifndef CONFIG_MMU
	current_top_of_stack = task_top_of_stack(to);
	current_ptregs = task_pt_regs(to);

	if (to->mm == NULL)
		return;

	/* XXX: FIXME */
	// rkj: this changes the FS on every context switch
//	arch_prctl(to, ARCH_SET_FS, (void __user *) to->thread.arch.fs);
#endif
}

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}
