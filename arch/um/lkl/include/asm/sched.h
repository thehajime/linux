/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_SCHED_H
#define __UM_LIBMODE_SCHED_H

#include <linux/sched.h>
#include <uapi/asm/host_ops.h>

static inline void thread_sched_jb(void)
{
	if (test_ti_thread_flag(current_thread_info(), TIF_HOST_THREAD)) {
		set_ti_thread_flag(current_thread_info(), TIF_SCHED_JB);
		set_current_state(TASK_UNINTERRUPTIBLE);
		lkl_jmp_buf_set(&current_thread_info()->task->thread.arch.sched_jb,
				     schedule);
	} else {
		lkl_bug("%s can be used only for host task", __func__);
	}
}

void switch_to_host_task(struct task_struct *);
int host_task_stub(void *unused);

#endif
