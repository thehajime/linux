#if 1
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <stdlib.h>

static void segv_handler (int cause, siginfo_t * info, void *uap)
{
	ucontext_t *context = uap;
	//For test. Never ever call stdio functions in a signal handler otherwise*/
	printf ("SIGSEGV handled (mcontext=0x%lx)\n", (unsigned long)context->uc_mcontext.gregs);

	printf ("SIGSEGV raised at address 0x%lx\n", (unsigned long)context->uc_mcontext.gregs[REG_RIP]);
	/*On my particular system, compiled with gcc -O2, the offending instruction
	  generated for "*f = 16;" is 6 bytes. Lets try to set the instruction
	  pointer to the next instruction (general register 14 is EIP, on linux x86) */
	context->uc_mcontext.gregs[14] += 14;
	//alternativly, try to jump to a "safe place"
	//context->uc_mcontext.gregs[14] = (unsigned int)safe_func;
	exit(cause);
}

/* XXX: this code doesn't work as 2nd fprintf() causes SEGV
 * (probably) due to alignment issue of xmm0/rsp register
 *
 * https://stackoverflow.com/questions/5397041/getting-the-saved-instruction-pointer-address-from-a-signal-handler
 */
static void signal_segv(int signum, siginfo_t *info, void *ptr)
{
	static const char *si_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};
	int i, f = 0;
	ucontext_t *ucontext = (ucontext_t*)ptr;
	void **bp = 0;
	void *ip = 0;

	fprintf(stderr, "Segmentation Fault!\n");
	fprintf(stderr, "info.si_signo = %d\n", signum);
	fprintf(stderr, "info.si_errno = %d\n", info->si_errno);
	fprintf(stderr, "info.si_code  = %d (%s)\n", info->si_code, si_codes[info->si_code]);
	fprintf(stderr, "info.si_addr  = %p\n", info->si_addr);
	for(i = 0; i < NGREG; i++)
		fprintf(stderr, "reg[%02d]       = 0x%016llx\n", i, ucontext->uc_mcontext.gregs[i]);

	ucontext->uc_mcontext.gregs[14] += 14;
	exit(signum);
}


__asm__ (
".section .text.nolibc_memmove_memcpy\n"
".weak nolibc_memmove\n"
".weak nolibc_memcpy\n"
"nolibc_memmove:\n"
"nolibc_memcpy:\n"
	"movq %rdx, %rcx\n\t"
	"movq %rdi, %rax\n\t"
	"movq %rdi, %rdx\n\t"
	"subq %rsi, %rdx\n\t"
	"cmpq %rcx, %rdx\n\t"
	"jb   1f\n\t"
	"rep movsb\n\t"
	"retq\n"
"1:" /* backward copy */
	"leaq -1(%rdi, %rcx, 1), %rdi\n\t"
	"leaq -1(%rsi, %rcx, 1), %rsi\n\t"
	"std\n\t"
	"rep movsb\n\t"
	"cld\n\t"
	"retq\n"

".section .text.nolibc_memset\n"
".weak memset\n"
"memset:\n"
	"xchgl %eax, %esi\n\t"
	"movq  %rdx, %rcx\n\t"
	"pushq %rdi\n\t"
	"rep stosb\n\t"
	"popq  %rax\n\t"
	"retq\n"
);

void *nolibc_memcpy(void *dest, const void *src, size_t n);

/* from arch/x86/boot/compressed/string.c */
static void *____memcpy(void *dest, const void *src, size_t n)
{
	long d0, d1, d2;
        asm volatile(
                "rep movsq\n\t"
                "movq %4,%%rcx\n\t"
                "rep movsb"
                : "=&c" (d0), "=&D" (d1), "=&S" (d2)
                : "0" (n >> 3), "g" (n & 7), "1" (dest), "2" (src)
                : "memory");

        return dest;
}

static inline void do_host_sigsegv(void)
{
	char *ptr = NULL;
//	____memcpy(ptr, 0, 8);
	nolibc_memcpy(ptr, 0, 4);
//	*ptr = '1';
}

int main (int argc, char *argv[])
{
	char *ptr = NULL;
	struct sigaction sa;
	int *f = NULL;

	if (argc != 3) {
		printf("%s [nullptr or raise] [handler or not]\n", argv[0]);
		return 0;
	}

	if (atoi(argv[2]) >= 1) {
		printf("register handler\n");
		if (atoi(argv[2]) == 1)
			sa.sa_sigaction = segv_handler;
		else
			sa.sa_sigaction = signal_segv;
		sigemptyset (&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		if (sigaction (SIGSEGV, &sa, 0)) {
			perror ("sigaction");
			return -1;
		}
	}

	if (atoi(argv[1]) == 0)
		do_host_sigsegv();
	else if (atoi(argv[1]) == 1) {
		printf("raising signal\n");
		raise(SIGSEGV);
	}
	else if (atoi(argv[1]) == 2) {
		int i = 1, j;
		j = i / 0;
		memcpy(ptr, 0, 8);
	}
	else {
		printf("should: 0 <= argv[1] <= 2\n");
	}
	return 0;
}
#else
// SPDX-License-Identifier: GPL-2.0
/*
 * Test FP register handling in userspace mcontext.
 *
 * Copyright (C) 2025 Intel Corporation
 */

/* Is there a better way to *not* include bits/sigcontext.h? */
#include <features.h>
#undef __USE_MISC
//#include <asm/sigcontext.h>

#include <elf.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <asm/unistd.h>
#include <pthread.h>

//#include "../../../tools/testing/selftests/kselftest.h"
#define ksft_print_msg printf
#define ksft_print_header() do {} while(0)
#define ksft_exit_fail_msg printf
#define ksft_test_result_pass printf
#define ksft_test_result_fail printf
#define ksft_set_plan
#define ksft_finished() do {} while(0)

#define ST0_EXP_ADD 10

static void sighandler(int sig, siginfo_t *info, void *p)
{
	ucontext_t *uc = p;
	struct _fpstate *fpstate = (void *)uc->uc_mcontext.fpregs;

	ksft_print_msg("sighandler(%d): extended_size: %d, xstate_size: %d\n", sig,
		       fpstate->padding[13],
		       fpstate->padding[16]);

#ifdef __i386__
	fpstate->_st[0].exponent += ST0_EXP_ADD;
	fpstate->_xmm[1].element[0] |= 0x01010101;
	fpstate->_xmm[1].element[1] |= 0x01010101;
	fpstate->_xmm[1].element[2] |= 0x01010101;
	fpstate->_xmm[1].element[3] |= 0x01010101;
#else
	/* Hacky way of modifying the exponent without breaking aliasing */
	fpstate->_st[0].exponent += ST0_EXP_ADD;
	fpstate->_xmm[1].element[0] |= 0x01010101;
	fpstate->_xmm[1].element[1] |= 0x01010101;
	fpstate->_xmm[1].element[2] |= 0x01010101;
	fpstate->_xmm[1].element[3] |= 0x01010101;
#endif

	if (sig == SIGSEGV || sig == SIGFPE)
		exit(sig);
}

static void do_self_sigusr1(uint32_t **sse, double *num)
{
	long ret;

	/*
	 * This does kill(getpid(), SIGUSR1); with "num" being passed in AND
	 * out of the floating point stack. We can therefore modify num by
	 * changing st[0] when handling the signal.
	 */
#ifdef __i386__
	asm volatile (
		"movups %1, %%xmm1;"
		"int $0x80;"
		"movups %%xmm1, %1;"
		: "=t" (*num), "=m" (*sse), "=a" (ret)
		: "0" (*num), "2" (__NR_kill), "b" (getpid()), "c" (SIGUSR1) :
		"xmm1", "memory");
#else
	asm volatile (
		"movups %1, %%xmm1;"
		"syscall;"
		"movups %%xmm1, %1;"
		: "=t" (*num), "=m"(*sse), "=a" (ret)
		: "0" (*num), "2" (__NR_kill), "D" (getpid()), "S" (SIGUSR1)
		: "r11", "rcx", "xmm1", "memory");
#endif
}


static inline void do_host_sigfpe(uint32_t **sse, double *num)
{
	char *ptr = NULL;
	int i = 1, j;
	j = i / 0;
}

static int test_mcontext(int xmm_should_change, void(*func)(uint32_t **, double *))
{
	double num = 0.5;
	uint32_t sse[4] = {0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 };
	int xmm_manipulated;

	ksft_print_msg("pre-signal:  %d / 100, %08x %08x %08x %08x\n", (int) (100*num), sse[0], sse[1], sse[2], sse[3]);

	func((uint32_t **)&sse, &num);

	if (sse[0] == 0x11223344 || sse[1] == 0x55667788 || sse[2] == 0x99aabbcc || sse[3] == 0xddeeff00)
		xmm_manipulated = 0;
	else if (sse[0] == 0x11233345 || sse[1] == 0x55677789 || sse[2] == 0x99abbbcd || sse[3] == 0xddefff01)
		xmm_manipulated = 1;
	else
		xmm_manipulated = 2;

	ksft_print_msg("post-signal: %d / 100, %08x %08x %08x %08x (should change: %d, changed: %d)\n",
		       (int) (100 * num), sse[0], sse[1], sse[2], sse[3], xmm_should_change, xmm_manipulated);

	if (num != (1 << (ST0_EXP_ADD - 1))) {
		ksft_print_msg("floating point register was not manipulated\n");
		return 1;
	}

	if (xmm_manipulated != xmm_should_change) {
		ksft_print_msg("xmm/sse had unexpected value!\n");
		return 1;
	}

	return 0;
}

static int test_mcontext2(int xmm_should_change)
{
	long ret;
	double num = 0.5;
	uint32_t sse[4] = {0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 };
	int xmm_manipulated;

	ksft_print_msg("pre-signal:  %d / 100, %08x %08x %08x %08x\n", (int) (100*num), sse[0], sse[1], sse[2], sse[3]);

	/*
	 * This does kill(getpid(), SIGUSR1); with "num" being passed in AND
	 * out of the floating point stack. We can therefore modify num by
	 * changing st[0] when handling the signal.
	 */
#ifdef __i386__
	asm volatile (
		"movups %1, %%xmm1;"
		"int $0x80;"
		"movups %%xmm1, %1;"
		: "=t" (num), "=m" (sse), "=a" (ret)
		: "0" (num), "2" (__NR_kill), "b" (getpid()), "c" (SIGUSR1) :
		"xmm1", "memory");
#else
	asm volatile (
		"movups %1, %%xmm1;"
		"syscall;"
		"movups %%xmm1, %1;"
		: "=t" (num), "=m"(sse), "=a" (ret)
		: "0" (num), "2" (__NR_kill), "D" (getpid()), "S" (SIGUSR1)
		: "r11", "rcx", "xmm1", "memory");
#endif

	if (sse[0] == 0x11223344 || sse[1] == 0x55667788 || sse[2] == 0x99aabbcc || sse[3] == 0xddeeff00)
		xmm_manipulated = 0;
	else if (sse[0] == 0x11233345 || sse[1] == 0x55677789 || sse[2] == 0x99abbbcd || sse[3] == 0xddefff01)
		xmm_manipulated = 1;
	else
		xmm_manipulated = 2;

	ksft_print_msg("post-signal: %d / 100, %08x %08x %08x %08x (should change: %d, changed: %d)\n",
		       (int) (100 * num), sse[0], sse[1], sse[2], sse[3], xmm_should_change, xmm_manipulated);

	if (num != (1 << (ST0_EXP_ADD - 1))) {
		ksft_print_msg("floating point register was not manipulated\n");
		return 1;
	}

	if (xmm_manipulated != xmm_should_change) {
		ksft_print_msg("xmm/sse had unexpected value!\n");
		return 1;
	}

	return 0;
}

static void my_sa_restorer(void)
{
	syscall(__NR_rt_sigreturn);
}

#define _NSIG 65
#define SIGPT_SET						   \
	((sigset_t *)(const unsigned long [_NSIG/8/sizeof(long)]){ \
	[sizeof(long)==4] = 3UL<<(32*(sizeof(long)>4)) })

struct k_sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned mask[2];
};

static int my_sigaction(int signum, struct sigaction *act, struct sigaction *oldact)
{
	struct k_sigaction ksa, oksa;

	ksa.handler = act->sa_handler;
	ksa.flags = act->sa_flags;
	ksa.flags |= SA_RESTORER;
	ksa.restorer = my_sa_restorer;
	memcpy(&ksa.mask, &act->sa_mask, _NSIG/8);

	return syscall(__NR_rt_sigaction, signum, &ksa, oldact ? & oksa : 0, _NSIG/8);
}

int main(int argc, char *argv[])
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO,
		.sa_handler = (void (*)(int))sighandler,
		.sa_mask = 0,
	};

	ksft_print_header();
	ksft_set_plan(1);

	if (atoi(argv[2]) >= 1) {
		printf("register handler\n");
		if (my_sigaction(SIGUSR1, &sa, NULL) < 0) {
			perror ("sigaction");
			return -1;
		}
		if (my_sigaction(SIGSEGV, &sa, NULL) < 0) {
			perror ("sigaction");
			return -1;
		}
		if (my_sigaction(SIGFPE, &sa, NULL) < 0) {
			perror ("sigaction");
			return -1;
		}
	}

	if (atoi(argv[1]) == 0)
		test_mcontext(1, do_host_sigsegv);
	else if (atoi(argv[1]) == 1) {
		printf("raising signal\n");
		test_mcontext2(1);
		//raise(SIGSEGV);
	}
	else if (atoi(argv[1]) == 2) {
		test_mcontext(1, do_host_sigfpe);
	}
	else {
		printf("should: 0 <= argv[1] <= 2\n");
	}

	ksft_finished();
}

#endif
