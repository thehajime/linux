/* SPDX-License-Identifier: GPL-2.0 */

#define sys_mmap sys_ni_syscall
#define sys_rt_sigreturn sys_ni_syscall
#define sys_arch_prctl sys_ni_syscall
#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall
#define sys_clone sys_ni_syscall

int run_syscalls(void);
