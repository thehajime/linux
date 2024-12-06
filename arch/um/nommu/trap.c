// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>
#include <asm/current.h>
#include <asm/tlbflush.h>
#include <arch.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>

/*
 * Note this is constrained to return 0, -EFAULT, -EACCES, -ENOMEM by
 * segv().
 */
int handle_page_fault(unsigned long address, unsigned long ip,
		      int is_write, int is_user, int *code_out)
{
	/* !MMU has no pagefault */
	return -EFAULT;
}

static void show_segv_info(struct uml_pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct faultinfo *fi = UPT_FAULTINFO(regs);

	if (!unhandled_signal(tsk, SIGSEGV))
		return;

	pr_warn_ratelimited("%s%s[%d]: segfault at %lx ip %p sp %p error %x",
			    task_pid_nr(tsk) > 1 ? KERN_INFO : KERN_EMERG,
			    tsk->comm, task_pid_nr(tsk), FAULT_ADDRESS(*fi),
			    (void *)UPT_IP(regs), (void *)UPT_SP(regs),
			    fi->error_code);
}

static void bad_segv(struct faultinfo fi, unsigned long ip)
{
	current->thread.arch.faultinfo = fi;
	force_sig_fault(SIGSEGV, SEGV_ACCERR, (void __user *) FAULT_ADDRESS(fi));
}

void fatal_sigsegv(void)
{
	force_fatal_sig(SIGSEGV);
	do_signal(&current->thread.regs);
	/*
	 * This is to tell gcc that we're not returning - do_signal
	 * can, in general, return, but in this case, it's not, since
	 * we just got a fatal SIGSEGV queued.
	 */
	os_dump_core();
}

/**
 * segv_handler() - the SIGSEGV handler
 * @sig:	the signal number
 * @unused_si:	the signal info struct; unused in this handler
 * @regs:	the ptrace register information
 *
 * The handler first extracts the faultinfo from the UML ptrace regs struct.
 * If the userfault did not happen in an UML userspace process, bad_segv is called.
 * Otherwise the signal did happen in a cloned userspace process, handle it.
 */
void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{
	struct faultinfo *fi = UPT_FAULTINFO(regs);

	/* !MMU specific part; detection of userspace */
	/* mark is_user=1 when the IP is from userspace code. */
	if (UPT_IP(regs) > uml_reserved && UPT_IP(regs) < high_physmem)
		regs->is_user = 1;

	if (UPT_IS_USER(regs) && !SEGV_IS_FIXABLE(fi)) {
		show_segv_info(regs);
		bad_segv(*fi, UPT_IP(regs));
		return;
	}
	segv(*fi, UPT_IP(regs), UPT_IS_USER(regs), regs);
}

/*
 * We give a *copy* of the faultinfo in the regs to segv.
 * This must be done, since nesting SEGVs could overwrite
 * the info in the regs. A pointer to the info then would
 * give us bad data!
 */
unsigned long segv(struct faultinfo fi, unsigned long ip, int is_user,
		   struct uml_pt_regs *regs)
{
	int si_code;
	int err;
	int is_write = FAULT_WRITE(fi);
	unsigned long address = FAULT_ADDRESS(fi);

	if (!is_user && regs)
		current->thread.segv_regs = container_of(regs, struct pt_regs, regs);

	if (current->mm == NULL) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Segfault with no mm");
	} else if (!is_user && address > PAGE_SIZE && address < TASK_SIZE) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Kernel tried to access user memory at addr 0x%lx, ip 0x%lx",
		       address, ip);
	}

	if (SEGV_IS_FIXABLE(&fi))
		err = handle_page_fault(address, ip, is_write, is_user,
					&si_code);
	else {
		err = -EFAULT;
		/*
		 * A thread accessed NULL, we get a fault, but CR2 is invalid.
		 * This code is used in __do_copy_from_user() of TT mode.
		 * XXX tt mode is gone, so maybe this isn't needed any more
		 */
		address = 0;
	}

	if (!err)
		goto out;
	else if (!is_user && arch_fixup(ip, regs))
		goto out;

	if (!is_user) {
		show_regs(container_of(regs, struct pt_regs, regs));
		panic("Kernel mode fault at addr 0x%lx, ip 0x%lx",
		      address, ip);
	}

	show_segv_info(regs);

	if (err == -EACCES) {
		current->thread.arch.faultinfo = fi;
		force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
	} else {
		WARN_ON_ONCE(err != -EFAULT);
		current->thread.arch.faultinfo = fi;
		force_sig_fault(SIGSEGV, si_code, (void __user *) address);
	}

out:
	if (regs)
		current->thread.segv_regs = NULL;

	return 0;
}

void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs)
{
	int code, err;

	if (!UPT_IS_USER(regs)) {
		if (sig == SIGBUS)
			pr_err("Bus error - the host /dev/shm or /tmp mount likely just ran out of space\n");
		panic("Kernel mode signal %d", sig);
	}

	arch_examine_signal(sig, regs);

	/* Is the signal layout for the signal known?
	 * Signal data must be scrubbed to prevent information leaks.
	 */
	code = si->si_code;
	err = si->si_errno;
	if ((err == 0) && (siginfo_layout(sig, code) == SIL_FAULT)) {
		struct faultinfo *fi = UPT_FAULTINFO(regs);

		current->thread.arch.faultinfo = *fi;
		force_sig_fault(sig, code, (void __user *)FAULT_ADDRESS(*fi));
	} else {
		pr_err("Attempted to relay unknown signal %d (si_code = %d) with errno %d\n",
		       sig, code, err);
		force_sig(sig);
	}
}

void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{
	do_IRQ(WINCH_IRQ, regs);
}
