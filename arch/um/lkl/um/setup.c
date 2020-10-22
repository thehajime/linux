// SPDX-License-Identifier: GPL-2.0
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/personality.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/start_kernel.h>
#include <linux/fdtable.h>
#include <linux/tick.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <asm/host_ops.h>
#include <asm/irq.h>
#include <asm/unistd.h>
#include <asm/syscalls.h>
#include <asm/cpu.h>
#include <os.h>
#include <as-layout.h>


static void *init_sem;
static int is_running;


struct lkl_host_operations *lkl_ops;

long lkl_panic_blink(int state)
{
	lkl_panic();
	return 0;
}

static void __init *lkl_run_kernel(void *arg)
{

	panic_blink = lkl_panic_blink;

	/* signal should be received at this thread (main and idle threads) */
	init_new_thread_signals();
	threads_init();
	lkl_cpu_get();
	start_kernel();

	return NULL;
}

static char _cmd_line[COMMAND_LINE_SIZE];
int __init lkl_start_kernel(struct lkl_host_operations *ops,
			    const char *fmt, ...)
{
	va_list ap;
	int ret;

	lkl_ops = ops;

	va_start(ap, fmt);
	ret = vsnprintf(_cmd_line, COMMAND_LINE_SIZE, fmt, ap);
	va_end(ap);

	if (ops->um_devices)
		strscpy(_cmd_line + ret, ops->um_devices,
			COMMAND_LINE_SIZE - ret);

	uml_set_args(_cmd_line);

	init_sem = lkl_sem_alloc(0);
	if (!init_sem)
		return -ENOMEM;

	ret = lkl_cpu_init();
	if (ret)
		goto out_free_init_sem;

	change_sig(SIGALRM, 0);
	change_sig(SIGIO, 0);

	ret = lkl_thread_create(lkl_run_kernel, NULL);
	if (!ret) {
		ret = -ENOMEM;
		goto out_free_init_sem;
	}

	lkl_sem_down(init_sem);
	lkl_sem_free(init_sem);
	current_thread_info()->task->thread.arch.tid = lkl_thread_self();
	lkl_cpu_change_owner(current_thread_info()->task->thread.arch.tid);

	lkl_cpu_put();
	is_running = 1;

	return 0;

out_free_init_sem:
	lkl_sem_free(init_sem);

	return ret;
}

int lkl_is_running(void)
{
	return is_running;
}


long lkl_sys_halt(void)
{
	long err;
	long params[6] = {LINUX_REBOOT_MAGIC1,
		LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART, };

	err = lkl_syscall(__NR_reboot, params);
	if (err < 0)
		return err;

	is_running = false;

	lkl_cpu_wait_shutdown();

	syscalls_cleanup();
	threads_cleanup();
	/* Shutdown the clockevents source. */
	tick_suspend_local();
	free_mem();
	lkl_thread_join(current_thread_info()->task->thread.arch.tid);

	return 0;
}


static int lkl_run_init(struct linux_binprm *bprm);

static struct linux_binfmt lkl_run_init_binfmt = {
	.module		= THIS_MODULE,
	.load_binary	= lkl_run_init,
};

static int lkl_run_init(struct linux_binprm *bprm)
{
	int ret;

	if (strcmp("/init", bprm->filename) != 0)
		return -EINVAL;

	ret = begin_new_exec(bprm);
	if (ret)
		return ret;
	set_personality(PER_LINUX);
	setup_new_exec(bprm);

	set_binfmt(&lkl_run_init_binfmt);

	init_pid_ns.child_reaper = 0;

	syscalls_init();

	lkl_sem_up(init_sem);
	lkl_thread_exit();

	return 0;
}


/* skip mounting the "real" rootfs. ramfs is good enough. */
static int __init fs_setup(void)
{
	int fd, flags = 0;

	if (force_o_largefile())
		flags |= O_LARGEFILE;
	fd = do_sys_open(AT_FDCWD, "/init", O_CREAT | flags, 0700);
	WARN_ON(fd < 0);
	close_fd(fd);

	register_binfmt(&lkl_run_init_binfmt);

	return 0;
}
late_initcall(fs_setup);
