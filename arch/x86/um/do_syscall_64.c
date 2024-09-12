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
	}
	else
		return os_arch_prctl(pid, option, arg2);

	return 0;
}

/*
 * Make a copy of the stack, so the child does whatever it
 * wants with it.  This copy is restored before exiting from
 * this function.
 *
 * (IIUC) the vfork(2) doesn't need to copy stack, and share all
 * memory including stack between parent and child. so this function
 * isn't needed, but some programs (in our case hush, a shell for
 * nommu env of busybox) corrupts stack between vfork(2)=>execve(2).
 * the hush implementation looks fine (calling immediate execve(2)
 * after vfork(2)), but corrupted.
 *
 * so, this workaround exists.
 */
static void *vfork_save_stack(void)
{
	unsigned char *stack_copy;

	stack_copy = kzalloc(PAGE_SIZE << THREAD_SIZE_ORDER,
			     GFP_KERNEL);
	if (!stack_copy)
		return NULL;

	memcpy(stack_copy,
	       (void *)current->thread.regs.regs.gp[HOST_SP],
	       PAGE_SIZE << THREAD_SIZE_ORDER);

	return stack_copy;
}

static void vfork_restore_stack(void *stack_copy)
{
	WARN_ON_ONCE(!stack_copy);
	memcpy((void *)current->thread.regs.regs.gp[HOST_SP],
	       stack_copy, PAGE_SIZE << THREAD_SIZE_ORDER);
}

__visible void do_syscall_64(struct pt_regs *regs)
{
	int syscall;
	unsigned char *stack_copy = NULL;

	syscall = PT_SYSCALL_NR(regs->regs.gp);
	UPT_SYSCALL_NR(&regs->regs) = syscall;

	pr_debug("syscall(%d) (current=%lx) (fn=%lx)\n",
		 syscall, (unsigned long)current,
		 (unsigned long)sys_call_table[syscall]);

	if (syscall == __NR_vfork)
		stack_copy = vfork_save_stack();

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
	if (syscall == __NR_vfork && regs->regs.gp[HOST_AX] > 0)
		vfork_restore_stack(stack_copy);
}

#endif
