/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_CPU_H
#define __UM_LIBMODE_CPU_H

enum {
	LKL_CPU_IN_SHUTDOWN = -2,	/* during shutdown process */
	LKL_CPU_MAX_THREAD = -1,	/* exceeded the number of max threads */
	LKL_CPU_IN_USE = 0,		/* someone else is using the cpu */
	LKL_CPU_LOCKED,		/*  successfully locked cpu */
}

int lkl_cpu_get(void);
void lkl_cpu_put(void);
int lkl_cpu_init(void);
void lkl_cpu_wait_shutdown(void);
void lkl_cpu_change_owner(lkl_thread_t owner);

#endif
