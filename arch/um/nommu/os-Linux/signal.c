// SPDX-License-Identifier: GPL-2.0

#include <signal.h>
#include <kern_util.h>
#include <os.h>
#include <sysdep/mcontext.h>
#include <sys/ucontext.h>
#include <as-layout.h>

void sigsys_handler(int sig, struct siginfo *si,
		    struct uml_pt_regs *regs, void *ptr)
{
	mcontext_t *mc = (mcontext_t *) ptr;

	/* hook syscall via SIGSYS */
	set_mc_sigsys_hook(mc);
}

void nommu_relay_signal(void *ptr)
{
	mcontext_t *mc = (mcontext_t *) ptr;

	set_mc_relay_signal(mc);
}
