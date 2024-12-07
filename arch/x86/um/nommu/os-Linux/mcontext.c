// SPDX-License-Identifier: GPL-2.0
#include <sys/ucontext.h>
#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#include <sysdep/ptrace.h>
#include <sysdep/mcontext.h>
#include <sysdep/syscalls.h>

void mc_set_sigsys_hook(mcontext_t *mc)
{
	mc->gregs[REG_RSP] -= sizeof(unsigned long);
	*((unsigned long *) (mc->gregs[REG_RSP])) = mc->gregs[REG_RIP];
	mc->gregs[REG_RCX] = mc->gregs[REG_RIP];
	mc->gregs[REG_RIP] = (unsigned long) __kernel_vsyscall;
}
