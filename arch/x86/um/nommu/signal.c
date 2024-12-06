// SPDX-License-Identifier: GPL-2.0

#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <asm/sigframe.h>

#include <sysdep/signal.h>

int arch_setup_signal_stack_si(struct rt_sigframe __user **frame,
			       struct ksignal *ksig)
{
	int err = 0;

	/*
	 * we need to push handler address at top of stack, as
	 * __kernel_vsyscall, called after this returns with ret with
	 * stack contents, thus push the handler here.
	 */
	*frame = (struct rt_sigframe __user *) ((unsigned long) *frame -
					       sizeof(unsigned long));
	err |= __put_user((unsigned long)ksig->ka.sa.sa_handler,
			  (unsigned long *)*frame);

	return err;
}

struct rt_sigframe *arch_setup_rt_sigreturn(struct rt_sigframe *frame)
{
	/**
	 * we enter here with:
	 *
	 * __restore_rt:
	 *     mov $15, %rax
	 *     syscall      ; => __kernel_vsyscall (hooked)
	 *
	 * (code is from musl libc)
	 * so, stack needs to be popped of "call"ed address before
	 * looking at rt_sigframe.
	 */
	frame = (struct rt_sigframe __user *)((unsigned long)frame + sizeof(long));

	return frame;
}
