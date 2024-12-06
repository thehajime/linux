/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_NOMMU_SYSCALLS_H
#define __UM_NOMMU_SYSCALLS_H


#define task_top_of_stack(task) \
({									\
	unsigned long __ptr = (unsigned long)task->stack;	\
	__ptr += THREAD_SIZE;			\
	__ptr;					\
})

extern long current_top_of_stack;
extern long current_ptregs;

#endif
