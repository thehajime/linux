// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
#undef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
#include <linux/syscalls.h>
#define CONFIG_ARCH_HAS_SYSCALL_WRAPPER
#endif

#include <linux/stat.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/task_work.h>

#include <asm/host_ops.h>
#include <kern_util.h>
#include <os.h>
#include "syscalls.h"

typedef long (*syscall_handler_t)(long arg1, ...);

#undef __SYSCALL
#define __SYSCALL(nr, sym)[nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] =  (syscall_handler_t)sys_ni_syscall,
#undef __UM_NOMMU_UAPI_UNISTD_H
#include <asm/unistd.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#endif
};


struct syscall {
	long no, *params, ret;
	void *sem;
};

static struct syscall_thread_data {
	wait_queue_head_t wqh;
	struct syscall *s;
	void *mutex, *completion;
} syscall_thread_data;

static struct syscall *dequeue_syscall(struct syscall_thread_data *data)
{
	return (struct syscall *)__sync_fetch_and_and((long *)&data->s, 0);
}

static long run_syscall(struct syscall *s)
{
	int ret;

	if (s->no < 0 || s->no >= __NR_syscalls || !syscall_table[s->no])
		ret = -ENOSYS;
	else
		ret = syscall_table[s->no](s->params[0], s->params[1],
					   s->params[2], s->params[3],
					   s->params[4], s->params[5]);
	s->ret = ret;

	task_work_run();

	return ret;
}

int run_syscalls(void)
{
	struct syscall_thread_data *data = &syscall_thread_data;
	struct syscall *s;

	current->flags &= ~PF_KTHREAD;

	snprintf(current->comm, sizeof(current->comm), "init");

	while (1) {
		wait_event(data->wqh, (s = dequeue_syscall(data)) != NULL);

		run_syscall(s);
		lkl_ops->sem_up(s->sem);
	}

	s->ret = 0;
	lkl_ops->sem_up(data->completion);

	return 0;
}

static irqreturn_t syscall_irq_handler(int irq, void *dev_id)
{
	wake_up(&syscall_thread_data.wqh);
	return IRQ_HANDLED;
}

static struct irqaction syscall_irqaction  = {
	.handler = syscall_irq_handler,
	.flags = IRQF_NOBALANCING,
	.dev_id = &syscall_irqaction,
	.name = "syscall"
};

void lkl_syscall_real_handler(int sig, struct siginfo *unused_si,
			      struct uml_pt_regs *regs)
{
	unsigned long flags;

	local_irq_save(flags);
	do_IRQ(LKL_SYSCALL_IRQ, regs);
	local_irq_restore(flags);
}

static int syscall_irq;

int lkl_trigger_irq(int irq)
{
	int ret = 0;

	if (irq >= NR_IRQS)
		return -EINVAL;

	os_sig_usr2(os_getpid());

	return ret;
}

long lkl_syscall(long no, long *params)
{
	struct syscall_thread_data *data = &syscall_thread_data;
	struct syscall s;

	s.no = no;
	s.params = params;
	s.sem = data->completion;

	lkl_ops->sem_down(data->mutex);
	data->s = &s;
	lkl_trigger_irq(syscall_irq);

	lkl_ops->sem_down(data->completion);
	lkl_ops->sem_up(data->mutex);

	return s.ret;
}


int __init syscall_init(void)
{
	struct syscall_thread_data *data = &syscall_thread_data;

	init_waitqueue_head(&data->wqh);
	data->mutex = lkl_ops->sem_alloc(1);
	data->completion = lkl_ops->sem_alloc(0);
	WARN_ON(!data->mutex || !data->completion);

	syscall_irq = LKL_SYSCALL_IRQ; //lkl_get_free_irq("syscall");
	setup_irq(syscall_irq, &syscall_irqaction);

	pr_info("lkl: syscall interface initialized (irq%d)\n", syscall_irq);
	return 0;
}
late_initcall(syscall_init);
