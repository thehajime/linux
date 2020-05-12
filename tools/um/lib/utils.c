// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <lkl_host.h>

static const char * const lkl_err_strings[] = {
	"Success",
	"Operation not permitted",
	"No such file or directory",
	"No such process",
	"Interrupted system call",
	"I/O error",
	"No such device or address",
	"Argument list too long",
	"Exec format error",
	"Bad file number",
	"No child processes",
	"Try again",
	"Out of memory",
	"Permission denied",
	"Bad address",
	"Block device required",
	"Device or resource busy",
	"File exists",
	"Cross-device link",
	"No such device",
	"Not a directory",
	"Is a directory",
	"Invalid argument",
	"File table overflow",
	"Too many open files",
	"Not a typewriter",
	"Text file busy",
	"File too large",
	"No space left on device",
	"Illegal seek",
	"Read-only file system",
	"Too many links",
	"Broken pipe",
	"Math argument out of domain of func",
	"Math result not representable",
	"Resource deadlock would occur",
	"File name too long",
	"No record locks available",
	"Invalid system call number",
	"Directory not empty",
	"Too many symbolic links encountered",
	"Bad error code", /* EWOULDBLOCK is EAGAIN */
	"No message of desired type",
	"Identifier removed",
	"Channel number out of range",
	"Level 2 not synchronized",
	"Level 3 halted",
	"Level 3 reset",
	"Link number out of range",
	"Protocol driver not attached",
	"No CSI structure available",
	"Level 2 halted",
	"Invalid exchange",
	"Invalid request descriptor",
	"Exchange full",
	"No anode",
	"Invalid request code",
	"Invalid slot",
	"Bad error code", /* EDEADLOCK is EDEADLK */
	"Bad font file format",
	"Device not a stream",
	"No data available",
	"Timer expired",
	"Out of streams resources",
	"Machine is not on the network",
	"Package not installed",
	"Object is remote",
	"Link has been severed",
	"Advertise error",
	"Srmount error",
	"Communication error on send",
	"Protocol error",
	"Multihop attempted",
	"RFS specific error",
	"Not a data message",
	"Value too large for defined data type",
	"Name not unique on network",
	"File descriptor in bad state",
	"Remote address changed",
	"Can not access a needed shared library",
	"Accessing a corrupted shared library",
	".lib section in a.out corrupted",
	"Attempting to link in too many shared libraries",
	"Cannot exec a shared library directly",
	"Illegal byte sequence",
	"Interrupted system call should be restarted",
	"Streams pipe error",
	"Too many users",
	"Socket operation on non-socket",
	"Destination address required",
	"Message too long",
	"Protocol wrong type for socket",
	"Protocol not available",
	"Protocol not supported",
	"Socket type not supported",
	"Operation not supported on transport endpoint",
	"Protocol family not supported",
	"Address family not supported by protocol",
	"Address already in use",
	"Cannot assign requested address",
	"Network is down",
	"Network is unreachable",
	"Network dropped connection because of reset",
	"Software caused connection abort",
	"Connection reset by peer",
	"No buffer space available",
	"Transport endpoint is already connected",
	"Transport endpoint is not connected",
	"Cannot send after transport endpoint shutdown",
	"Too many references: cannot splice",
	"Connection timed out",
	"Connection refused",
	"Host is down",
	"No route to host",
	"Operation already in progress",
	"Operation now in progress",
	"Stale file handle",
	"Structure needs cleaning",
	"Not a XENIX named type file",
	"No XENIX semaphores available",
	"Is a named type file",
	"Remote I/O error",
	"Quota exceeded",
	"No medium found",
	"Wrong medium type",
	"Operation Canceled",
	"Required key not available",
	"Key has expired",
	"Key has been revoked",
	"Key was rejected by service",
	"Owner died",
	"State not recoverable",
	"Operation not possible due to RF-kill",
	"Memory page has hardware error",
};

const char *lkl_strerror(int err)
{
	if (err < 0)
		err = -err;

	if ((size_t)err >= sizeof(lkl_err_strings) / sizeof(const char *))
		return "Bad error code";

	return lkl_err_strings[err];
}

void lkl_perror(char *msg, int err)
{
	const char *err_msg = lkl_strerror(err);
	/* We need to use 'real' printf because lkl_host_ops.print can
	 * be turned off when debugging is off.
	 */
	lkl_printf("%s: %s\n", msg, err_msg);
}

static int lkl_vprintf(const char *fmt, va_list args)
{
	int n;
	char *buffer;
	va_list copy;

	if (!lkl_host_ops.print)
		return 0;

	va_copy(copy, args);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);

	buffer = lkl_host_ops.mem_alloc(n + 1);
	if (!buffer)
		return -1;

	vsnprintf(buffer, n + 1, fmt, args);

	lkl_host_ops.print(buffer, n);
	lkl_host_ops.mem_free(buffer);

	return n;
}

int lkl_printf(const char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = lkl_vprintf(fmt, args);
	va_end(args);

	return n;
}

void lkl_bug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	lkl_vprintf(fmt, args);
	va_end(args);

	lkl_host_ops.panic();
}

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
void __delay(unsigned long loops){}
void __ndelay(unsigned long loops){}
void __udelay(unsigned long loops){}

/* x86/sysrq.c */
void show_regs(struct pt_regs *regs)
{}

/* x86/signal.c */
int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *mask){return 0;}

void arch_check_bugs(void){}

/* x86/syscalls.c */
void arch_switch_to(struct task_struct *to)
{}

/* os-Linux/start_up.c */
int parse_iomem(char *str, int *add){return 0;}

/* os-Linux/skas/process.c */
#define UM_NR_CPUS 1
int userspace_pid[UM_NR_CPUS];
void userspace(struct uml_pt_regs *regs, unsigned long *aux_fp_regs)
{}
void switch_threads(void *me, void *you)
{}
