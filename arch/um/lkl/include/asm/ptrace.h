/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_PTRACE_H
#define __UM_LIBMODE_PTRACE_H

#include <linux/errno.h>

static int reg_dummy __attribute__((unused));

#define PT_REGS_ORIG_SYSCALL(r) (reg_dummy)
#define PT_REGS_SYSCALL_RET(r) (reg_dummy)
#define PT_REGS_SET_SYSCALL_RETURN(r, res) (reg_dummy = (res))
#define REGS_SP(r) (reg_dummy)

#define user_mode(regs) 0
#define kernel_mode(regs) 1
#define profile_pc(regs) 0
#define user_stack_pointer(regs) 0

extern void new_thread_handler(void);

#endif
