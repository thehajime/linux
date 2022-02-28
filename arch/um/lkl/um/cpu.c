// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/sched/stat.h>
#include <asm/host_ops.h>
#include <asm/cpu.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/sched.h>
#include <asm/syscalls.h>
#include <init.h>
#include <os.h>

/*
 * This structure is used to get access to the "LKL CPU" that allows us to run
 * Linux code. Because we have to deal with various synchronization requirements
 * between idle thread, system calls, interrupts, "reentrancy", CPU shutdown,
 * imbalance wake up (i.e. acquire the CPU from one thread and release it from
 * another), we can't use a simple synchronization mechanism such as (recursive)
 * mutex or semaphore. Instead, we use a mutex and a bunch of status data plus a
 * semaphore.
 */
static struct lkl_cpu {
	/* lock that protects the CPU status data */
	struct lkl_mutex *lock;
	/*
	 * Since we must free the cpu lock during shutdown we need a
	 * synchronization algorithm between lkl_cpu_shutdown() and the CPU
	 * access functions since lkl_cpu_get() gets called from thread
	 * destructor callback functions which may be scheduled after
	 * lkl_cpu_shutdown() has freed the cpu lock.
	 *
	 * An atomic counter is used to keep track of the number of running
	 * CPU access functions and allow the shutdown function to wait for
	 * them.
	 *
	 * The shutdown functions adds MAX_THREADS to this counter which allows
	 * the CPU access functions to check if the shutdown process has
	 * started.
	 *
	 * This algorithm assumes that we never have more the MAX_THREADS
	 * requesting CPU access.
	 */
	#define MAX_THREADS 1000000
	unsigned int shutdown_gate;
	bool irqs_pending;
	/* no of threads waiting the CPU */
	unsigned int sleepers;
	/* no of times the current thread got the CPU */
	unsigned int count;
	/* current thread that owns the CPU */
	lkl_thread_t owner;
	/* semaphore for threads waiting the CPU */
	struct lkl_sem *sem;
	/* semaphore used for shutdown */
	struct lkl_sem *shutdown_sem;
} cpu;

static void run_irqs(void)
{
	unblock_signals();
}

static void set_irq_pending(int sig)
{
	set_pending_signals(sig);
}

/*
 * internal routine to acquire LKL CPU's lock
 */
static int __cpu_try_get_lock(int n)
{
	lkl_thread_t self;

	if (__sync_fetch_and_add(&cpu.shutdown_gate, n) >= MAX_THREADS)
		return -LKL_CPU_IN_SHUTDOWN;

	lkl_mutex_lock(cpu.lock);

	if (cpu.shutdown_gate >= MAX_THREADS)
		return -LKL_CPU_MAX_THREAD;

	self = lkl_thread_self();

	/* if someone else is using the cpu, indicate as return 0 */
	if (cpu.owner && !lkl_thread_equal(cpu.owner, self))
		return LKL_CPU_IN_USE;

	/* set the owner of cpu */
	cpu.owner = self;
	cpu.count++;

	return LKL_CPU_LOCKED;
}

/*
 * internal routine to release LKL CPU's lock
 */
static void __cpu_try_get_unlock(int lock_ret, int n)
{
	/*
	 * release lock only if __cpu_try_get_lock() holds cpu.lock
	 * (returns >= -1)
	 */
	if (lock_ret >= -1)
		lkl_mutex_unlock(cpu.lock);
	__sync_fetch_and_sub(&cpu.shutdown_gate, n);
}

void lkl_cpu_change_owner(lkl_thread_t owner)
{
	lkl_mutex_lock(cpu.lock);
	if (cpu.count > 1)
		lkl_bug("bad count while changing owner\n");
	cpu.owner = owner;
	lkl_mutex_unlock(cpu.lock);
}

int lkl_cpu_get(void)
{
	int ret;

	ret = __cpu_try_get_lock(1);

	/*
	 * when somebody holds a lock, sleep until released,
	 * with obtaining a semaphore (cpu.sem)
	 */
	while (ret == LKL_CPU_IN_USE) {
		cpu.sleepers++;
		__cpu_try_get_unlock(ret, 0);
		lkl_sem_down(cpu.sem);
		ret = __cpu_try_get_lock(0);
	}

	__cpu_try_get_unlock(ret, 1);

	return ret;
}

void lkl_cpu_put(void)
{
	lkl_mutex_lock(cpu.lock);

	if (!cpu.count || !cpu.owner ||
	    !lkl_thread_equal(cpu.owner, lkl_thread_self()))
		lkl_bug("%s: unbalanced put\n", __func__);

	/*
	 * we're going to trigger irq handlers if there are any pending
	 * interrupts, and not irq_disabled.
	 */
	while (cpu.irqs_pending && !irqs_disabled()) {
		cpu.irqs_pending = false;
		lkl_mutex_unlock(cpu.lock);
		run_irqs();
		lkl_mutex_lock(cpu.lock);
	}

	/*
	 * switch to userspace code if current is host task (TIF_HOST_THREAD),
	 * AND, there are other running tasks.
	 */
	if (test_ti_thread_flag(current_thread_info(), TIF_HOST_THREAD) &&
	    !single_task_running() && cpu.count == 1) {
		if (in_interrupt())
			lkl_bug("%s: in interrupt\n", __func__);
		lkl_mutex_unlock(cpu.lock);
		thread_sched_jb();
		return;
	}

	/*
	 * if there are any other tasks holding cpu lock, return after
	 * decreasing cpu.count
	 */
	if (--cpu.count > 0) {
		lkl_mutex_unlock(cpu.lock);
		return;
	}

	/* release semaphore if slept */
	if (cpu.sleepers) {
		cpu.sleepers--;
		lkl_sem_up(cpu.sem);
	}

	cpu.owner = 0;

	lkl_mutex_unlock(cpu.lock);
}

int lkl_cpu_try_run_irq(int irq)
{
	int ret;

	ret = __cpu_try_get_lock(1);
	if (ret == LKL_CPU_IN_USE) {
		set_irq_pending(irq);
		cpu.irqs_pending = true;
	}
	__cpu_try_get_unlock(ret, 1);

	return ret;
}

int lkl_irq_enter(int sig)
{
	return lkl_cpu_try_run_irq(sig);
}

void lkl_irq_exit(void)
{
	return lkl_cpu_put();
}

static void lkl_cpu_shutdown(void)
{
	__sync_fetch_and_add(&cpu.shutdown_gate, MAX_THREADS);
}
__uml_exitcall(lkl_cpu_shutdown);

void lkl_cpu_wait_shutdown(void)
{
	lkl_sem_down(cpu.shutdown_sem);
	lkl_sem_free(cpu.shutdown_sem);
}

static void lkl_cpu_cleanup(bool shutdown)
{
	while (__sync_fetch_and_add(&cpu.shutdown_gate, 0) > MAX_THREADS)
		;

	/*
	 * if caller indicates shutdown, notify the semaphore to release
	 * the block (lkl_cpu_wait_shutdown()).
	 */
	if (shutdown)
		lkl_sem_up(cpu.shutdown_sem);
	/* if lkl_cpu_wait_shutdown() is not called, free shutdown_sem here */
	else if (cpu.shutdown_sem)
		lkl_sem_free(cpu.shutdown_sem);

	if (cpu.sem)
		lkl_sem_free(cpu.sem);
	if (cpu.lock)
		lkl_mutex_free(cpu.lock);
}

void subarch_cpu_idle(void)
{
	if (cpu.shutdown_gate >= MAX_THREADS) {
		lkl_mutex_lock(cpu.lock);
		while (cpu.sleepers--)
			lkl_sem_up(cpu.sem);
		lkl_mutex_unlock(cpu.lock);

		lkl_cpu_cleanup(true);
		lkl_thread_exit();
	}

	/* switch to idle_host_task */
	wakeup_idle_host_task();
}

int lkl_cpu_init(void)
{
	cpu.lock = lkl_mutex_alloc(0);
	cpu.sem = lkl_sem_alloc(0);
	cpu.shutdown_sem = lkl_sem_alloc(0);

	if (!cpu.lock || !cpu.sem || !cpu.shutdown_sem) {
		lkl_cpu_cleanup(false);
		return -ENOMEM;
	}

	return 0;
}
