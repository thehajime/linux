// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <kern_util.h>
#include <asm/syscall.h>
#include <asm/prctl.h>
#include <os.h>
#include "syscalls.h"

__visible void do_syscall_64(struct pt_regs *regs)
{
	int syscall;

	syscall = PT_SYSCALL_NR(regs->regs.gp);
	UPT_SYSCALL_NR(&regs->regs) = syscall;

	/* set fs register to the original host one */
	os_x86_arch_prctl(0, ARCH_SET_FS, (void *)host_fs);

	if (likely(syscall < NR_syscalls)) {
		unsigned long ret;

		ret = (*sys_call_table[syscall])(UPT_SYSCALL_ARG1(&regs->regs),
						 UPT_SYSCALL_ARG2(&regs->regs),
						 UPT_SYSCALL_ARG3(&regs->regs),
						 UPT_SYSCALL_ARG4(&regs->regs),
						 UPT_SYSCALL_ARG5(&regs->regs),
						 UPT_SYSCALL_ARG6(&regs->regs));
		PT_REGS_SET_SYSCALL_RETURN(regs, ret);
	}

	PT_REGS_SYSCALL_RET(regs) = regs->regs.gp[HOST_AX];

	/* handle tasks and signals at the end */
	interrupt_end();

	/* restore back fs register to userspace configured one */
	os_x86_arch_prctl(0, ARCH_SET_FS,
		      (void *)(current->thread.regs.regs.gp[FS_BASE
						     / sizeof(unsigned long)]));

}
