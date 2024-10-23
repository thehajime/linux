// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <asm/fsgsbase.h>
#include <asm/prctl.h>
#include <kern_util.h>
#include <sysdep/syscalls.h>
#include <os.h>

#ifndef CONFIG_MMU

static int os_x86_arch_prctl(int pid, int option, unsigned long *arg2)
{
	if (os_has_fsgsbase()) {
		switch (option) {
		case ARCH_SET_FS:
			wrfsbase(*arg2);
			break;
		case ARCH_SET_GS:
			wrgsbase(*arg2);
			break;
		case ARCH_GET_FS:
			*arg2 = rdfsbase();
			break;
		case ARCH_GET_GS:
			*arg2 = rdgsbase();
			break;
		}
		return 0;
	} else
		return os_arch_prctl(pid, option, arg2);

	return 0;
}

__visible void do_syscall_64(struct pt_regs *regs)
{
	int syscall;

	syscall = PT_SYSCALL_NR(regs->regs.gp);
	UPT_SYSCALL_NR(&regs->regs) = syscall;

	pr_debug("syscall(%d) (current=%lx) (fn=%lx)\n",
		 syscall, (unsigned long)current,
		 (unsigned long)sys_call_table[syscall]);

	/* set fs register to the original host one */
	os_x86_arch_prctl(0, ARCH_SET_FS, (void *)host_fs);

	if (likely(syscall < NR_syscalls)) {
		PT_REGS_SET_SYSCALL_RETURN(regs,
				EXECUTE_SYSCALL(syscall, regs));
	}

	pr_debug("syscall(%d) --> %lx\n", syscall,
		regs->regs.gp[HOST_AX]);

	PT_REGS_SYSCALL_RET(regs) = regs->regs.gp[HOST_AX];

	/* restore back fs register to userspace configured one */
	os_x86_arch_prctl(0, ARCH_SET_FS,
		      (void *)(current->thread.regs.regs.gp[FS_BASE
						     / sizeof(unsigned long)]));

	/* force do_signal() --> is_syscall() */
	set_thread_flag(TIF_SIGPENDING);
	interrupt_end();

	/* execve succeeded */
	if (syscall == __NR_execve && regs->regs.gp[HOST_AX] == 0) {
		userspace(&current->thread.regs.regs,
			current_thread_info()->aux_fp_regs);
	}
}

#endif
