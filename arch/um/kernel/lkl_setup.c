#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/personality.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/start_kernel.h>
#include <linux/syscalls.h>
#include <linux/tick.h>
#include <asm/host_ops.h>
#include <asm/irq.h>
#include <asm/unistd.h>
#include <os.h>

#include <linux/kernel.h>
#include <linux/console.h>

long lkl_syscall(long, long *);

static void *init_sem;
static int is_running;


struct lkl_host_operations *lkl_ops;

static void __init *lkl_run_kernel(void *arg)
{
	threads_init();
	start_kernel();

	return NULL;
}

int __init lkl_start_kernel(struct lkl_host_operations *ops,
			    const char *fmt, ...)
{
	int ret;

	lkl_ops = ops;

	init_sem = lkl_ops->sem_alloc(0);
	if (!init_sem)
		return -ENOMEM;

	change_sig(SIGIO, 0);
	set_handler(SIGUSR2);

	ret = lkl_ops->thread_create(lkl_run_kernel, NULL);
	if (!ret) {
		ret = -ENOMEM;
		goto out_free_init_sem;
	}

	lkl_ops->sem_down(init_sem);
	lkl_ops->sem_free(init_sem);

	is_running = 1;

	return 0;

out_free_init_sem:
	lkl_ops->sem_free(init_sem);

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

	threads_cleanup();
	/* Shutdown the clockevents source. */
	tick_suspend_local();

	return 0;
}

#ifndef CONFIG_GENERIC_CALIBRATE_DELAY
void calibrate_delay(void)
{
}
#endif

static int lkl_run_init(struct linux_binprm *bprm);

static struct linux_binfmt lkl_run_init_binfmt = {
	.module		= THIS_MODULE,
	.load_binary	= lkl_run_init,
};

int run_syscalls(void);
static int lkl_run_init(struct linux_binprm *bprm)
{
	int ret;

	if (strcmp("/init", bprm->filename) != 0)
		return -EINVAL;

	ret = flush_old_exec(bprm);
	if (ret)
		return ret;
	set_personality(PER_LINUX);
	setup_new_exec(bprm);
	install_exec_creds(bprm);

	set_binfmt(&lkl_run_init_binfmt);

	init_pid_ns.child_reaper = 0;

	lkl_ops->sem_up(init_sem);
	run_syscalls();
	lkl_ops->thread_exit();

	return 0;
}

/* skip mounting the "real" rootfs. ramfs is good enough. */
static int __init fs_setup(void)
{
	int fd;

	fd = ksys_open("/init", O_CREAT, 0700);
	WARN_ON(fd < 0);
	ksys_close(fd);

	register_binfmt(&lkl_run_init_binfmt);

	return 0;
}
late_initcall(fs_setup);
