/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_CPU_H
#define __UM_LIBMODE_CPU_H

int lkl_cpu_get(void);
void lkl_cpu_put(void);
int lkl_cpu_init(void);
void lkl_cpu_wait_shutdown(void);
void lkl_cpu_change_owner(lkl_thread_t owner);

#endif
