/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_PROCESSOR_H
#define __UM_LIBMODE_PROCESSOR_H

#include <asm/host_ops.h>

struct alt_instr;

struct arch_thread {
	struct lkl_sem *sched_sem;
	bool dead;
	lkl_thread_t tid;
	struct lkl_jmp_buf sched_jb;
	unsigned long stackend;
};

#include <asm/ptrace-generic.h>
#include <asm/processor-generic.h>

#define INIT_ARCH_THREAD {}
#define task_pt_regs(tsk) ((struct pt_regs *)(NULL))

static inline void cpu_relax(void)
{
	unsigned long flags;

	/* since this is usually called in a tight loop waiting for some
	 * external condition (e.g. jiffies) lets run interrupts now to allow
	 * the external condition to propagate
	 */
	local_irq_save(flags);
	local_irq_restore(flags);
}

#define KSTK_EIP(tsk)	(0)
#define KSTK_ESP(tsk)	(0)

static inline void trap_init(void)
{
}

static inline void arch_copy_thread(struct arch_thread *from,
				    struct arch_thread *to)
{
	panic("unimplemented %s: fork isn't supported yet", __func__);
}

#endif
