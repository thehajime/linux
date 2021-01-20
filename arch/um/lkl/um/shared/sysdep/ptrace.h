/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_UM_PTRACE_H
#define __ARCH_UM_PTRACE_H

#include <generated/user_constants.h>
#include <linux/errno.h>

struct task_struct;

#define UPT_SYSCALL_NR(r) ((r)->syscall)
#define UPT_RESTART_SYSCALL(r) ((r)->syscall--) /* XXX */

#define UPT_SP(r) 0
#define UPT_IP(r) 0
#define EMPTY_UML_PT_REGS { }

#define MAX_REG_OFFSET (UM_FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

/* unused */
struct uml_pt_regs {
	unsigned long gp[1];
	unsigned long fp[1];
	long faultinfo;
	long syscall;
	int is_user;
};

extern void arch_init_registers(int pid);

static inline long arch_ptrace(struct task_struct *child,
			       long request, unsigned long addr,
			       unsigned long data)
{
	return -EINVAL;
}

static inline void ptrace_disable(struct task_struct *child) {}
static inline void user_enable_single_step(struct task_struct *child) {}
static inline void user_disable_single_step(struct task_struct *child) {}

#endif /* __ARCH_UM_PTRACE_H */
