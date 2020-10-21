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
#include <asm/syscalls.h>
#include <asm/syscalls_32.h>
#include <asm/cpu.h>
#include <asm/sched.h>
#include <kern_util.h>
#include <os.h>

typedef long (*syscall_handler_t)(long arg1, ...);

#undef __SYSCALL
#define __SYSCALL(nr, sym)[nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] =  (syscall_handler_t)sys_ni_syscall,
#undef __UM_LIBMODE_UAPI_UNISTD_H
#include <asm/unistd.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#endif
};


static long run_syscall(long no, long *params)
{
	long ret;

	if (no < 0 || no >= __NR_syscalls)
		return -ENOSYS;

	ret = syscall_table[no](params[0], params[1], params[2], params[3],
				params[4], params[5]);

	task_work_run();

	return ret;
}


#define CLONE_FLAGS (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD |	\
		     CLONE_SIGHAND | SIGCHLD)

static int host_task_id;
static struct task_struct *host0;

static int new_host_task(struct task_struct **task)
{
	pid_t pid;

	switch_to_host_task(host0);

	pid = kernel_thread(host_task_stub, NULL, CLONE_FLAGS);
	if (pid < 0)
		return pid;

	rcu_read_lock();
	*task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	host_task_id++;

	snprintf((*task)->comm, sizeof((*task)->comm), "host%d", host_task_id);

	return 0;
}
static void exit_task(void)
{
	do_exit(0);
}

static void del_host_task(void *arg)
{
	struct task_struct *task = (struct task_struct *)arg;
	struct thread_info *ti = task_thread_info(task);

	if (lkl_cpu_get() < 0)
		return;

	switch_to_host_task(task);
	host_task_id--;
	set_ti_thread_flag(ti, TIF_SCHED_JB);
	lkl_jmp_buf_set(&ti->task->thread.arch.sched_jb, exit_task);
}

static struct lkl_tls_key *task_key;

long lkl_syscall(long no, long *params)
{
	struct task_struct *task = host0;
	long ret;

	ret = lkl_cpu_get();
	if (ret < 0)
		return ret;

	task = lkl_tls_get(task_key);
	if (!task) {
		ret = new_host_task(&task);
		if (ret)
			goto out;
		lkl_tls_set(task_key, task);
	}

	switch_to_host_task(task);

	ret = run_syscall(no, params);

	if (no == __NR_reboot) {
		thread_sched_jb();
		return ret;
	}

out:
	lkl_cpu_put();

	return ret;
}

static struct task_struct *idle_host_task;

/* called from idle, don't failed, don't block */
void wakeup_idle_host_task(void)
{
	if (!need_resched() && idle_host_task)
		wake_up_process(idle_host_task);
}

static int idle_host_task_loop(void *unused)
{
	struct thread_info *ti = task_thread_info(current);

	snprintf(current->comm, sizeof(current->comm), "idle_host_task");
	set_thread_flag(TIF_HOST_THREAD);
	idle_host_task = current;

	for (;;) {
		lkl_cpu_put();
		lkl_sem_down(ti->task->thread.arch.sched_sem);
		if (idle_host_task == NULL) {
			lkl_thread_exit();
			return 0;
		}
		schedule_tail(ti->task->thread.prev_sched);
	}
}

int syscalls_init(void)
{
	snprintf(current->comm, sizeof(current->comm), "host0");
	set_thread_flag(TIF_HOST_THREAD);
	host0 = current;

	task_key = lkl_tls_alloc(del_host_task);
	if (!task_key)
		return -1;

	if (kernel_thread(idle_host_task_loop, NULL, CLONE_FLAGS) < 0) {
		lkl_tls_free(task_key);
		return -1;
	}

	return 0;
}

void syscalls_cleanup(void)
{
	if (idle_host_task) {
		struct thread_info *ti = task_thread_info(idle_host_task);

		idle_host_task = NULL;
		lkl_sem_up(ti->task->thread.arch.sched_sem);
		lkl_thread_join(ti->task->thread.arch.tid);
	}

	lkl_tls_free(task_key);
}
