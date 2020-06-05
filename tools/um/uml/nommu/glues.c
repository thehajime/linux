#include <generated/user_constants.h>
#include <signal.h>

void kernel_longjmp(void){};
void kernel_setjmp(void){};
struct pt_regs;
struct uml_pt_regs;
struct task_struct;
struct ksignal;
struct siginfo;

/* trap.c */
void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs)
{}
void bus_handler(int sig, struct siginfo *si, struct uml_pt_regs *regs)
{}
void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{}
void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{}

/* skas/process.c */
void halt_skas(void)
{}
int is_skas_winch(int pid, int fd, void *data)
{return 0;}
void reboot_skas(void){}

/* exec.c */
void flush_thread(void){}

/* tlb.c */
void flush_tlb_kernel_vm(void){}

/* x86/ptrace_64.c */
int is_syscall(unsigned long addr){return 0;}

/* x86/delay.c */
void __const_udelay(unsigned long xloops){}

/* x86/sysrq.c */
void show_regs(struct pt_regs *regs)
{}

/* x86/signal.c */
int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask){return 0;}

void arch_check_bugs(void){}


/* os-Linux/start_up.c */
int parse_iomem(char *str, int *add){return 0;}

/* os-Linux/skas/process.c */
int userspace_pid[UM_NR_CPUS];
void userspace(struct uml_pt_regs *regs, unsigned long *aux_fp_regs)
{}
