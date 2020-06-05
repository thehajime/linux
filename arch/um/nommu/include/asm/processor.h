#ifndef _ASM_LKL_PROCESSOR_H
#define _ASM_LKL_PROCESSOR_H

#include <asm/host_ops.h>

struct arch_thread {
	struct lkl_sem *sched_sem;
	bool dead;
	lkl_thread_t tid;
	struct task_struct *prev_sched;
	unsigned long stackend;
};

#include <asm/ptrace-generic.h>
#include <asm/processor-generic.h>

#define INIT_ARCH_THREAD {}
#define task_pt_regs(tsk) (struct pt_regs *)(NULL)

static inline void cpu_relax(void)
{
	unsigned long flags;

	/* since this is usually called in a tight loop waiting for some
	 * external condition (e.g. jiffies) lets run interrupts now to allow
	 * the external condition to propagate */
	local_irq_save(flags);
	local_irq_restore(flags);
}

#define KSTK_EIP(tsk)	(0)
#define KSTK_ESP(tsk)	(0)

static inline void trap_init(void)
{
}

#endif
