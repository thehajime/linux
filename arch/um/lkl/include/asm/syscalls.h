/* SPDX-License-Identifier: GPL-2.0 */

#include <uapi/linux/audit.h>

int syscalls_init(void);
void syscalls_cleanup(void);
long lkl_syscall(long no, long *params);
void wakeup_idle_host_task(void);

#define sys_mmap sys_ni_syscall
#define sys_mmap2 sys_mmap_pgoff
#define sys_rt_sigreturn sys_ni_syscall
#define sys_arch_prctl sys_ni_syscall
#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall
#define sys_clone sys_ni_syscall

int run_syscalls(void);

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_X86_64;
}
