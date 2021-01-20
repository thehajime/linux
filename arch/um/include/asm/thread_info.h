/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_THREAD_INFO_H
#define __UM_THREAD_INFO_H

#define THREAD_SIZE_ORDER CONFIG_KERNEL_STACK_ORDER
#define THREAD_SIZE ((1 << CONFIG_KERNEL_STACK_ORDER) * PAGE_SIZE)

#ifndef __ASSEMBLY__

#include <asm/types.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <sysdep/ptrace_user.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;  /* 0 => preemptable,
						   <0 => BUG */
	struct thread_info	*real_thread;    /* Points to non-IRQ stack */
	unsigned long aux_fp_regs[FP_SIZE];	/* auxiliary fp_regs to save/restore
						   them out-of-band */
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.flags =		0,		\
	.cpu =		0,			\
	.preempt_count = INIT_PREEMPT_COUNT,	\
	.real_thread = NULL,			\
}

#ifndef CONFIG_UMMODE_LIB
/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	unsigned long mask = THREAD_SIZE - 1;
	void *p;

	asm volatile ("" : "=r" (p) : "0" (&ti));
	ti = (struct thread_info *) (((unsigned long)p) & ~mask);
	return ti;
}

#else /* CONFIG_UMMODE_LIB */

#define __HAVE_THREAD_FUNCTIONS
#define task_thread_info(task)	((struct thread_info *)(task)->stack)
#define task_stack_page(task)	((task)->stack)
#define end_of_stack(p) (&task_thread_info(p)->aux_fp_regs[FP_SIZE-1])

void threads_init(void);
void threads_cleanup(void);

unsigned long *alloc_thread_stack_node(struct task_struct *p, int node);
void setup_thread_stack(struct task_struct *p, struct task_struct *org);
void free_thread_stack(struct task_struct *tsk);

extern struct thread_info *_current_thread_info;
static inline struct thread_info *current_thread_info(void)
{
	return _current_thread_info;
}
#endif /* CONFIG_UMMODE_LIB */

#endif

#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_NOTIFY_SIGNAL	3	/* signal notifications exist */
#define TIF_RESTART_BLOCK	4
#define TIF_MEMDIE		5	/* is terminating due to OOM killer */
#define TIF_SYSCALL_AUDIT	6
#define TIF_RESTORE_SIGMASK	7
#define TIF_NOTIFY_RESUME	8
#define TIF_SECCOMP		9	/* secure computing */
#define TIF_SCHED_JB		10	/* thread is configured with jump buffer */
#define TIF_HOST_THREAD	11	/* thread is a host task */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)

#endif
