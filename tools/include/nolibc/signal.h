/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * signal function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_SIGNAL_H
#define _NOLIBC_SIGNAL_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"
#include <linux/signal.h>

/* This one is not marked static as it's needed by libgcc for divide by zero */
int raise(int signal);
__attribute__((weak,unused,section(".text.nolibc_raise")))
int raise(int signal)
{
	return _sys_kill(_sys_getpid(), signal);
}

/*
 * sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
 */

#ifdef SA_RESTORER
__attribute__((naked))
static void my_sa_restorer(void)
{
	__nolibc_syscall0(__NR_rt_sigreturn);
}
#endif

static __attribute__((unused))
int sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	struct sigaction real_act = *act;
#ifdef SA_RESTORER
	if (!(real_act.sa_flags & SA_RESTORER)) {
		real_act.sa_flags |= SA_RESTORER;
		real_act.sa_restorer = my_sa_restorer;
	}
#endif

	return __nolibc_syscall4(__NR_rt_sigaction, signum, &real_act, oldact,
			   sizeof(act->sa_mask));
}

static __attribute__((unused))
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	return __sysret(sys_sigaction(signum, act, oldact));
}

#endif /* _NOLIBC_SIGNAL_H */
