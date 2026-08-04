// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 *
 * This vDSO turns all calls into a syscall so that UML can trap them.
 */


/* Disable profiling for userspace code */
#define DISABLE_BRANCH_PROFILING

#include <vdso/gettime.h>
#include <linux/time.h>
#include <asm/unistd.h>

/* XXX: FIXME, always trap SIGSYS on nommu, cannot use zpoline path as
 * we don't know how to retrieve um_zpoline_enabled from vdso object ???
 */
#define __VDSO_SYSCALL1(sysnr, ret, a0) {		\
		do {					\
			asm("syscall"			\
			    : "=a" (ret)		\
			    : "0" (sysnr), "D" (a0)	\
			    : "rcx", "r11", "memory");	\
		} while (0);				\
	}
#define __VDSO_SYSCALL2(sysnr, ret, a0, a1) {			\
		do {						\
			asm("syscall"				\
			    : "=a" (ret)			\
			    : "0" (sysnr), "D" (a0), "S" (a1)	\
			    : "rcx", "r11", "memory");		\
		} while (0);					\
	}

int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	long ret;

	__VDSO_SYSCALL2(__NR_clock_gettime, ret, clock, ts);
	return ret;
}
int clock_gettime(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	long ret;

	__VDSO_SYSCALL2(__NR_gettimeofday, ret, tv, tz);
	return ret;
}
int gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

__kernel_old_time_t __vdso_time(__kernel_old_time_t *t)
{
	long secs;

	__VDSO_SYSCALL1(__NR_time, secs, t);
	return secs;
}
__kernel_old_time_t time(__kernel_old_time_t *t) __attribute__((weak, alias("__vdso_time")));
