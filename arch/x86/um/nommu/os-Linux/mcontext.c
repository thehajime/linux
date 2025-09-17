// SPDX-License-Identifier: GPL-2.0
#include <sys/ucontext.h>
#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#include <sysdep/ptrace.h>
#include <sysdep/mcontext.h>
#include <os.h>
#include "../syscalls.h"

extern long __kernel_vsyscall(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
			      int64_t a4, int64_t a5, int64_t a6);

void set_mc_relay_signal(mcontext_t *mc)
{
	/* configure stack and userspace returning routine as
	 * instruction pointer
	 */
	mc->gregs[REG_RSP] = (unsigned long) current_top_of_stack;
	mc->gregs[REG_RIP] = (unsigned long) userspace;
}

void set_mc_sigsys_hook(mcontext_t *mc)
{
	mc->gregs[REG_RCX] = mc->gregs[REG_RIP];
	mc->gregs[REG_RIP] = (unsigned long) __kernel_vsyscall;
}
