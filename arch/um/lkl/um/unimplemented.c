// SPDX-License-Identifier: GPL-2.0
#include <linux/signal.h>
#include <sysdep/ptrace.h>
#include <asm/ptrace.h>

/* physmem.c  */
unsigned long high_physmem;

/* x86/um/setjmp*.S  */
void kernel_longjmp(void)
{}
void kernel_setjmp(void)
{}

/* trap.c */
void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs)
{}
void bus_handler(int sig, struct siginfo *si, struct uml_pt_regs *regs)
{}
void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{}
void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{}

/* tlb.c */
void flush_tlb_kernel_vm(void)
{}
void force_flush_all(void)
{}

/* skas/process.c */
void halt_skas(void)
{}
int is_skas_winch(int pid, int fd, void *data)
{
	return 0;
}
void reboot_skas(void)
{}

int __init start_uml(void)
{
	return 0;
}

/* exec.c */
void flush_thread(void)
{}

/* x86/ptrace_64.c */
int is_syscall(unsigned long addr)
{
	return 0;
}


/* x86/sysrq.c */
void show_regs(struct pt_regs *regs)
{}

/* x86/signal.c */
int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask)
{
	return 0;
}

/* x86/bugs.c */
void arch_check_bugs(void)
{}
