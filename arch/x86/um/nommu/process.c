// SPDX-License-Identifier: GPL-2.0

#include <asm/unistd.h>
#include <sysdep/syscalls.h>
#include <os.h>

void arch_fixup_stack(struct uml_pt_regs *regs)
{
	/*
	 * child of vfork(2) comes here.
	 * clone(2) also enters here but doesn't need to advance the %rsp.
	 *
	 * This fork can only come from libc's vfork, which
	 * does this:
	 *	pop %%rdx;
	 *	mov $58,%eax
	 *	syscall   ; // hook => __kernel_vsyscall
	 *	push %%rdx;
	 * %rcx stores the return address which is stored
	 * at pt_regs[HOST_IP] at the moment.  As child returns
	 * via userspace() with a jmp instruction (while parent
	 * does via ret instruction in __kernel_vsyscall), we
	 * need to pop (advance) the pushed address by "call"
	 * though, so this is what this next line does.
	 *
	 * As a result of vfork return in child, stack contents
	 * is overwritten by child (by pushq in vfork), which
	 * makes the parent puzzled after child returns.
	 *
	 * thus the contents should be restored before vfork/parent
	 * returns.  this is done in do_syscall_64().
	 */
	if (regs->gp[HOST_ORIG_AX] == __NR_vfork)
		regs->gp[REGS_SP_INDEX] += 8;
}
