/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_ASM_SYSCALL_H
#define __UM_ASM_SYSCALL_H

#include <asm/syscall-generic.h>
#include <uapi/linux/audit.h>

typedef asmlinkage long (*sys_call_ptr_t)(unsigned long, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long, unsigned long);

extern const sys_call_ptr_t sys_call_table[];

static inline int syscall_get_arch(struct task_struct *task)
{
#ifdef CONFIG_X86_32
	return AUDIT_ARCH_I386;
#else
	return AUDIT_ARCH_X86_64;
#endif
}

#ifndef CONFIG_MMU
extern void do_syscall_64(struct pt_regs *regs);
extern long __kernel_vsyscall(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
			      int64_t a4, int64_t a5, int64_t a6);
#endif

#endif /* __UM_ASM_SYSCALL_H */
