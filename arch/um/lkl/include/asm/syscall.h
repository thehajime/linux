/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_SYSCALL_H
#define __UM_LIBMODE_SYSCALL_H

#include <uapi/linux/audit.h>

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_X86_64;
}

#endif /* __UM_LIBMODE_SYSCALL_H */
