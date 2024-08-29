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
#include <asm/mman.h>

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
#ifdef CONFIG_MMU

	switch (option) {
	case ARCH_SET_FS:
		current->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
		ret = 0;
		break;
	case ARCH_SET_GS:
		current->thread.regs.regs.gp[GS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
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
#else

	unsigned long *ptr = arg2, tmp;
	int pid = task->mm->context.id.u.pid;

	switch (option) {
	case ARCH_SET_FS:
		if (host_fs == -1)
			os_arch_prctl(0, ARCH_GET_FS, (void *)&host_fs);
		ret = 0;
		break;
	case ARCH_SET_GS:
		ret = 0;
		break;
	case ARCH_GET_FS:
	case ARCH_GET_GS:
		ptr = &tmp;
		break;
	}

	ret = os_arch_prctl(pid, option, ptr);
	if (ret)
		return ret;

	switch (option) {
	case ARCH_SET_FS:
		current->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
		break;
	case ARCH_SET_GS:
		current->thread.regs.regs.gp[GS_BASE / sizeof(unsigned long)] =
			(unsigned long) arg2;
		break;
	case ARCH_GET_FS:
		ret = put_user(current->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)], arg2);
		break;
	case ARCH_GET_GS:
		ret = put_user(current->thread.regs.regs.gp[GS_BASE / sizeof(unsigned long)], arg2);
		break;
	}

	return ret;
#endif
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
	current_ptregs = (long)task_pt_regs(to);

	if ((to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] == 0)
	    || (to->mm == NULL))
		return;

	// rkj: this changes the FS on every context switch
	arch_prctl(to, ARCH_SET_FS,
		   (void __user *) to->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)]);
#endif
}

/*
 * XXX: copied from musl dynlink.c
 * musl has a switch, DL_NOMMU_SUPPORT, to support nommu kernel, but it's
 * a compilation-time switch, which isn't enabled on x86_64 platform.  so
 * emulate the behavior of DL_NOMMU_SUPPORT here, until musl support it over
 * a runtime-switch.
 */

static void *mmap_fixed(void *p, size_t n, int prot, int flags, int fd, off_t off)
{
	char *q;
	ssize_t r;

	/* Fallbacks for MAP_FIXED failure on NOMMU kernels. */
	if (flags & MAP_ANONYMOUS) {
		memset(p, 0, n);
		return p;
	}
	if (sys_lseek(fd, off, SEEK_SET) < 0) return (void *)-1;
	for (q=p; n; q+=r, off+=r, n-=r) {
		r = ksys_read(fd, q, n);
		if (r < 0 && r != -EINTR) return (void *)-1;
		if (!r) {
			memset(q, 0, n);
			break;
		}
	}
	return p;
}

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;

#ifndef CONFIG_MMU
	if (flags & MAP_FIXED) {
		void *ret;
		ret = mmap_fixed((void *)addr, len, prot, flags, fd, off);
		if (ret == (void *)-1)
			return -EINVAL;
		return (unsigned long)ret;
	}
#endif
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}
#if 0
SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
               unsigned long, prot)
{
	os_protect_memory((void *)start, len, prot & PROT_READ, prot & PROT_WRITE,
		prot & PROT_EXEC);
	return 0;
}
#endif
