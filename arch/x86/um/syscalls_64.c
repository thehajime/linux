/*
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <asm/prctl.h> /* XXX This should get the constants from libc */
#include <os.h>

long arch_prctl(struct task_struct *task, int option,
		unsigned long __user *arg2)
{
	unsigned long *ptr = arg2, tmp;
	long ret;
	int pid = task->mm->context.id.u.pid;

	/*
	 * With ARCH_SET_FS (and ARCH_SET_GS is treated similarly to
	 * be safe), we need to call arch_prctl on the host because
	 * setting %fs may result in something else happening (like a
	 * GDT or thread.fs being set instead).  So, we let the host
	 * fiddle the registers and thread struct and restore the
	 * registers afterwards.
	 *
	 * So, the saved registers are stored to the process (this
	 * needed because a stub may have been the last thing to run),
	 * arch_prctl is run on the host, then the registers are read
	 * back.
	 */
	switch (option) {
	case ARCH_SET_FS:
	case ARCH_SET_GS:
		ret = restore_registers(pid, &current->thread.regs.regs);
		if (ret)
			return ret;
		break;
	case ARCH_GET_FS:
	case ARCH_GET_GS:
		/*
		 * With these two, we read to a local pointer and
		 * put_user it to the userspace pointer that we were
		 * given.  If addr isn't valid (because it hasn't been
		 * faulted in or is just bogus), we want put_user to
		 * fault it in (or return -EFAULT) instead of having
		 * the host return -EFAULT.
		 */
		ptr = &tmp;
	}

	ret = os_arch_prctl(pid, option, ptr);
	if (ret)
		return ret;

	switch (option) {
	case ARCH_SET_FS:
		current->thread.arch.fs = (unsigned long) ptr;
		ret = save_registers(pid, &current->thread.regs.regs);
		break;
	case ARCH_SET_GS:
		ret = save_registers(pid, &current->thread.regs.regs);
		break;
	case ARCH_GET_FS:
		ret = put_user(tmp, arg2);
		break;
	case ARCH_GET_GS:
		ret = put_user(tmp, arg2);
		break;
	}

	return ret;
}

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return arch_prctl(current, option, (unsigned long __user *) arg2);
}

void arch_switch_to(struct task_struct *to)
{
	if ((to->thread.arch.fs == 0) || (to->mm == NULL))
		return;

	arch_prctl(to, ARCH_SET_FS, (void __user *) to->thread.arch.fs);
}

SYSCALL_DEFINE3(virtio_mmio_device_add, long, base, long, size, unsigned int,
		irq)
{
	struct platform_device *pdev;
	int ret;

	struct resource res[] = {
		[0] = {
		       .start = base,
		       .end = base + size - 1,
		       .flags = IORESOURCE_MEM,
		       },
		[1] = {
		       .start = irq,
		       .end = irq,
		       .flags = IORESOURCE_IRQ,
		       },
	};

	pdev = platform_device_alloc("virtio-mmio", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		dev_err(&pdev->dev,
			"%s: Unable to device alloc for virtio-mmio\n",
			__func__);
		return -ENOMEM;
	}

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev,
			"%s: Unable to add resources for %s%d\n", __func__,
			pdev->name, pdev->id);
		goto exit_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Unable to add %s%d\n", __func__,
			pdev->name, pdev->id);
		goto exit_release_pdev;
	}

	return pdev->id;

exit_release_pdev:
	platform_device_del(pdev);
exit_device_put:
	platform_device_put(pdev);

	return ret;
}
