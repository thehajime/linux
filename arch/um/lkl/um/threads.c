// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <asm/host_ops.h>
#include <asm/cpu.h>
#include <asm/sched.h>

#include <os.h>

static int init_arch_thread(struct arch_thread *thread)
{
	thread->sched_sem = lkl_sem_alloc(0);
	if (!thread->sched_sem)
		return -ENOMEM;

	thread->dead = false;
	thread->tid = 0;

	return 0;
}

unsigned long *alloc_thread_stack_node(struct task_struct *task, int node)
{
	struct thread_info *ti;

	ti = kmalloc(sizeof(*ti), GFP_KERNEL | __GFP_ZERO);
	if (!ti)
		return NULL;

	ti->task = task;
	return (unsigned long *)ti;
}

/*
 * The only new tasks created are kernel threads that have a predefined starting
 * point thus no stack copy is required.
 */
void setup_thread_stack(struct task_struct *p, struct task_struct *org)
{
	struct thread_info *ti = task_thread_info(p);
	struct thread_info *org_ti = task_thread_info(org);

	ti->flags = org_ti->flags;
	ti->preempt_count = org_ti->preempt_count;
	ti->addr_limit = org_ti->addr_limit;
}

static void kill_thread(struct thread_info *ti)
{
	if (!test_ti_thread_flag(ti, TIF_HOST_THREAD)) {
		ti->task->thread.arch.dead = true;
		lkl_sem_up(ti->task->thread.arch.sched_sem);
		lkl_thread_join(ti->task->thread.arch.tid);
	}
	lkl_sem_free(ti->task->thread.arch.sched_sem);
}

void free_thread_stack(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);

	kill_thread(ti);
	kfree(ti);
}

struct thread_info *_current_thread_info = &init_thread_union.thread_info;
EXPORT_SYMBOL(_current_thread_info);

void switch_threads(jmp_buf *me, jmp_buf *you)
{
	/* NOP */
}

/*
 * schedule() expects the return of this function to be the task that we
 * switched away from. Returning prev is not going to work because we are
 * actually going to return the previous taks that was scheduled before the
 * task we are going to wake up, and not the current task, e.g.:
 *
 * swapper -> init: saved prev on swapper stack is swapper
 * init -> ksoftirqd0: saved prev on init stack is init
 * ksoftirqd0 -> swapper: returned prev is swapper
 */
static struct task_struct *abs_prev = &init_task;

void arch_switch_to(struct task_struct *prev,
		    struct task_struct *next)
{
	struct arch_thread *_prev = &prev->thread.arch;
	struct arch_thread *_next = &next->thread.arch;
	unsigned long _prev_flags = task_thread_info(prev)->flags;
	struct lkl_jmp_buf *_prev_jb;

	_current_thread_info = task_thread_info(next);
	next->thread.prev_sched = prev;
	abs_prev = prev;

	WARN_ON(!_next->tid);
	lkl_cpu_change_owner(_next->tid);

	if (test_bit(TIF_SCHED_JB, &_prev_flags)) {
		/* Atomic. Must be done before wakeup next */
		clear_ti_thread_flag(task_thread_info(prev), TIF_SCHED_JB);
		_prev_jb = &_prev->sched_jb;
	}

	lkl_sem_up(_next->sched_sem);
	if (test_bit(TIF_SCHED_JB, &_prev_flags))
		lkl_jmp_buf_longjmp(_prev_jb, 1);
	else
		lkl_sem_down(_prev->sched_sem);

	if (_prev->dead)
		lkl_thread_exit();

	/* __switch_to (arch/um) returns this value */
	current->thread.prev_sched = abs_prev;
}

int host_task_stub(void *unused)
{
	return 0;
}

void switch_to_host_task(struct task_struct *task)
{
	if (WARN_ON(!test_tsk_thread_flag(task, TIF_HOST_THREAD)))
		return;

	task->thread.arch.tid = lkl_thread_self();

	if (current == task)
		return;

	wake_up_process(task);
	thread_sched_jb();
	lkl_sem_down(task->thread.arch.sched_sem);
	schedule_tail(abs_prev);
}

struct thread_bootstrap_arg {
	struct thread_info *ti;
	int (*f)(void *a);
	void *arg;
};

static void *thread_bootstrap(void *_tba)
{
	struct thread_bootstrap_arg *tba = (struct thread_bootstrap_arg *)_tba;
	struct thread_info *ti = tba->ti;
	int (*f)(void *) = tba->f;
	void *arg = tba->arg;

	lkl_sem_down(ti->task->thread.arch.sched_sem);
	kfree(tba);
	if (ti->task->thread.prev_sched)
		schedule_tail(ti->task->thread.prev_sched);

	f(arg);
	do_exit(0);

	return NULL;
}

void new_thread(void *stack, jmp_buf *buf, void (*handler)(void))
{
	struct thread_info *ti = (struct thread_info *)stack;
	struct task_struct *p = ti->task;
	struct thread_bootstrap_arg *tba;
	int ret;

	unsigned long esp = (unsigned long)p->thread.request.u.thread.proc;
	unsigned long unused = (unsigned long)p->thread.request.u.thread.arg;

	ret = init_arch_thread(&p->thread.arch);
	if (ret < 0)
		panic("%s: init_arch_thread", __func__);

	if ((int (*)(void *))esp == host_task_stub) {
		set_ti_thread_flag(ti, TIF_HOST_THREAD);
		return;
	}

	tba = kmalloc(sizeof(*tba), GFP_KERNEL);
	if (!tba)
		return;

	tba->f = (int (*)(void *))esp;
	tba->arg = (void *)unused;
	tba->ti = ti;

	p->thread.arch.tid = lkl_thread_create(thread_bootstrap, tba);
	if (!p->thread.arch.tid) {
		kfree(tba);
		return;
	}
}

void show_stack(struct task_struct *task, unsigned long *esp)
{
}

static inline void pr_early(const char *str)
{
	lkl_print(str, strlen(str));
}

/**
 * This is called before the kernel initializes, so no kernel calls (including
 * printk) can't be made yet.
 */
void threads_init(void)
{
	int ret;
	struct thread_info *ti = &init_thread_union.thread_info;

	ti->task->thread = (struct thread_struct) INIT_THREAD;
	ret = init_arch_thread(&ti->task->thread.arch);
	if (ret < 0)
		pr_early("lkl: failed to allocate init schedule semaphore\n");

	ti->task->thread.arch.tid = lkl_thread_self();
}

void threads_cleanup(void)
{
	struct task_struct *p, *t;

	for_each_process_thread(p, t) {
		struct thread_info *ti = task_thread_info(t);

		if (t->pid != 1 && !test_ti_thread_flag(ti, TIF_HOST_THREAD))
			WARN(!(t->flags & PF_KTHREAD),
			     "non kernel thread task %s\n", t->comm);
		WARN(t->state == TASK_RUNNING,
		     "thread %s still running while halting\n", t->comm);

		kill_thread(ti);
	}

	lkl_sem_free(
		init_thread_union.thread_info.task->thread.arch.sched_sem);
}

void initial_thread_cb_skas(void (*proc)(void *), void *arg)
{
	pr_warn("unimplemented %s", __func__);
}

int arch_set_tls(struct task_struct *new, unsigned long tls)
{
	panic("unimplemented %s", __func__);
}
void clear_flushed_tls(struct task_struct *task)
{
	panic("unimplemented %s", __func__);
}
