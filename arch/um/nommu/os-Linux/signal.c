// SPDX-License-Identifier: GPL-2.0

#include <signal.h>
#include <kern_util.h>
#include <os.h>
#include <sysdep/mcontext.h>
#include <sys/ucontext.h>

void arch_sigsys_handler(int sig, struct siginfo *si, void *ptr)
{
	mcontext_t *mc = (mcontext_t *) ptr;

	/* hook syscall via SIGSYS */
	mc_set_sigsys_hook(mc);
}
