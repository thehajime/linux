/*
 * gcc test-signal-restore.c -o test-signal-restore-amd64
 * gcc -m32 -march=i686 -lm test-signal-restore.c -o test-signal-restore-i386
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

#define ST0_EXP_ADD 10

void *scratch_page;

void sighandler(int sig, siginfo_t *info, void *p)
{
	ucontext_t *uc = p;

#if 0
	printf("sighandler: extended_size: %d, xstate_size: %d\n",
	       ((struct _fpstate *)uc->uc_mcontext.fpregs)->sw_reserved.extended_size,
	        ((struct _fpstate *)uc->uc_mcontext.fpregs)->sw_reserved.xstate_size);
#endif
	uc->uc_mcontext.fpregs->_st[0].exponent += ST0_EXP_ADD;

	printf("%d: sh\n", gettid());
}

int test_fp(double num)
{
	long ret;
	double orig_num = num;

	printf("%d: pre-signal: %g\n", gettid(), num);
	/*
	 * This does kill(getpid(), SIGUSR1); with "num" being passed in AND
	 * out of the floating point stack. We can therefore modify num by
	 * changing st[0] when handling the signal.
	 */
#ifdef __i386__
	asm volatile (
		"int $0x80;"
		: "=t" (num), "=a" (ret)
		: "0" (num), "1" (__NR_kill), "b" (getpid()), "c" (SIGUSR1) : );
#else
	asm volatile (
		"syscall;"
		: "=t" (num), "=a" (ret)
		: "0" (num), "1" (__NR_kill), "D" (getpid()), "S" (SIGUSR1) : "r11", "rcx");
#endif
	sleep(1);
	printf("%d: post-signal: %g\n", gettid(), num);

	if (num != (pow(2, ST0_EXP_ADD) * orig_num)) {
		printf("%d: floating point register was not manipulated\n", gettid());
		return 1;
	}

	return 0;
}

enum source {
	S_FPREGS = 0,
	S_FPXREGS = 1,
	S_GETREGS_FPREGS = 2,
	S_GETREGS_XFPREGS = 3,
	S_GETREGS_XSTATE = 4,
};

static void *thread1(void *arg)
{
	int val = (intptr_t)arg;

	for (int i = 0; i < 2; i++ ){
		double num = 0.5 + val;
		double orig_num = num;

//	printf("%s: >> num: %f tid=%d\n", __func__, num, gettid());
		sleep(3);

		num = pow(2, ST0_EXP_ADD) * num;
		printf("%s: << orig_num: %f num: %f tid=%d\n", __func__, orig_num, num, gettid());

		if (pow(2, ST0_EXP_ADD) * orig_num != num) {
			abort();
			return (void *)-1;
		}
	}

	return NULL;
}

int test_fp_pthread(enum source source)
{
	pthread_t th[10];
	int ret = 0;
	long arg = 0;

	for (int i = 0; i < sizeof(th)/sizeof(pthread_t); i++) {
		if(pthread_create(&th[i], NULL, thread1, (void *)arg++)) {
			perror("pthread_create");
			return 1;
		}
	}
#if 0
	if (test_fp(0.9)) {
		perror("test_fp_pthread 2nd");
	}
#endif
	for (int i = 0; i < sizeof(th)/sizeof(pthread_t); i++) {
		void *retval;
		if(pthread_join(th[i], &retval)) {
			perror("pthread_join");
			return 1;
		}

		if ((intptr_t)retval == -1)
			ret = -1;

	}

	return ret;
}

int test_fp_ptrace(enum source source)
{
#if 0
	int pid, status, ret;

	pid = vfork();
	if (pid < 0)
		return 127;

	if (pid == 0) {
		/* child */
		ptrace(PTRACE_TRACEME, 0, 0, 0);
		kill(getpid(), SIGSTOP);
		
		if (test_fp(0.5))
			exit(1);

		exit(0);
	}

	/* Wait for child to stop itself */
	do {
		ret = waitpid(pid, &status, 0);
	} while (ret < 0 && errno == EINTR);
	if (!WIFSTOPPED(status))
		return 127;

	/* Continue until SIGUSR1 to self */
	ptrace(PTRACE_CONT, pid, NULL, 0);
	do {
		ret = waitpid(pid, &status, 0);
	} while (ret < 0 && errno == EINTR);
	if (!WIFSTOPPED(status))
		return 127;

	if (source == S_FPXREGS || source == S_GETREGS_XFPREGS) {
#ifdef __i386__
		struct user_fpxregs_struct *fpstate;
		struct iovec iov = {
			.iov_len = sizeof(*fpstate),
		};
		int ret;

		fpstate = scratch_page + 4096 - iov.iov_len;
		iov.iov_base = fpstate;

		if (source == S_GETREGS_XFPREGS)
			ret = ptrace(PTRACE_GETREGSET, pid, NT_PRXFPREG, &iov);
		else
			ret = ptrace(PTRACE_GETFPXREGS, pid, NULL, fpstate);

		if (ret) {
			kill(pid, SIGKILL);
			if (errno == EINVAL) {
				printf("Getting FPX regs not supported\n");
				return 0;
			} else {
				printf("Error getting FPX regs: %d\n", errno);
				return 127;
			}
		}
		((struct _fpxreg*)&fpstate->st_space[0])->exponent += ST0_EXP_ADD;

		if (source == S_GETREGS_XFPREGS)
			ret = ptrace(PTRACE_SETREGSET, pid, NT_PRXFPREG, &iov);
		else
			ret = ptrace(PTRACE_SETFPXREGS, pid, NULL, fpstate);
		if (ret)
			return -127;

#else
		printf("No FPXREGS on x86_64\n");
		kill(pid, SIGKILL);
		return 127;
#endif
	} else if (source == S_FPREGS || source == S_GETREGS_FPREGS) {
		struct _fpstate *fpstate;
		struct iovec iov = {
			.iov_len = sizeof(*fpstate),
		};

		fpstate = scratch_page; // + 4096 - sizeof(*fpstate);
		iov.iov_base = fpstate;

		if (source == S_GETREGS_FPREGS)
			ret = ptrace(PTRACE_GETREGSET, pid, NT_PRFPREG, &iov);
		else
			ret = ptrace(PTRACE_GETFPREGS, pid, NULL, fpstate);


		if (ret) {
			kill(pid, SIGKILL);
			if (errno == EINVAL) {
				printf("Getting FP regs not supported\n");
				return 0;
			} else {
				printf("Error getting FPX regs: %d\n", errno);
				return 127;
			}
		}
#ifdef __i386__
		((struct _fpreg*) &fpstate->_st[0])->exponent += ST0_EXP_ADD;
#else
		((struct _fpstate*) &fpstate)->_st[0].exponent += ST0_EXP_ADD;
#endif

		if (source == S_GETREGS_FPREGS)
			ret = ptrace(PTRACE_SETREGSET, pid, NT_PRFPREG, &iov);
		else
			ret = ptrace(PTRACE_SETFPREGS, pid, NULL, fpstate);

		if (ret)
			return 127;
	} else if (source == S_GETREGS_XSTATE) {
#ifdef __i386__
		struct user_fpxregs_struct *fpstate;
#else
		struct user_fpregs_struct *fpstate;
#endif
		struct iovec iov = {
			.iov_len = 4096,
		};

		fpstate = scratch_page + 4096 - iov.iov_len;
		iov.iov_base = fpstate;

		ret = ptrace(PTRACE_GETREGSET, pid, NT_X86_XSTATE, &iov);
		if (ret) {
			kill(pid, SIGKILL);
			if (errno == EINVAL) {
				printf("Getting XSTATE not supported\n");
				return 0;
			} else {
				printf("Error getting XSTATE size: %d\n", errno);
				return 127;
			}
		}

		printf("host xstate size: %ld\n", iov.iov_len);

		/* Second time with the exact length (to test the kernel) */
		fpstate = scratch_page + 4096 - iov.iov_len;
		iov.iov_base = fpstate;

		ret = ptrace(PTRACE_GETREGSET, pid, NT_X86_XSTATE, &iov);
		if (ret) {
			printf("Error getting XSTATE: %d\n", errno);
			return 127;
		}

		fpstate = scratch_page + 4096 - iov.iov_len;
		iov.iov_base = fpstate;

		ret = ptrace(PTRACE_GETREGSET, pid, NT_X86_XSTATE, &iov);
		if (ret) {
			kill(pid, SIGKILL);
			printf("Error getting XSTATE (with correct size): %d\n", errno);
			return 127;
		}

#ifdef __i386__
		((struct _fpxreg *)&fpstate->st_space[0])->exponent += ST0_EXP_ADD;
#else
		((struct _fpstate *)&fpstate)->_st[0].exponent += ST0_EXP_ADD;
#endif

		ret = ptrace(PTRACE_SETREGSET, pid, NT_X86_XSTATE, &iov);
		if (ret) {
			printf("Failed to set XSTATE: %d\n", errno);
			return 127;
		}

	} else {
		return 127;
	}

	/* Run until completion (without handling the signal) */
	ptrace(PTRACE_CONT, pid, NULL, 0);
	do {
		ret = waitpid(pid, &status, 0);
	} while (ret < 0 && errno == EINTR);

	if (!WIFEXITED(status))
		return 127;

	return WEXITSTATUS(status);
#endif
}

int main()
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO,
		.sa_handler = (void (*)(int))sighandler,
	};
	int ret;

	scratch_page = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	munmap(scratch_page + 4096, 4096);

	sigaction(SIGUSR1, &sa, NULL);

	if (test_fp(0.5))
		return 1;

	ret = test_fp_pthread(S_FPREGS);
	if (ret)
		return ret;

#if 0
	sa.sa_handler = SIG_DFL;
	sigaction(SIGUSR1, &sa, NULL);

	printf("\nmodify using ptrace PTRACE_SETFPREGS instead of sighandler:\n");
	ret = test_fp_ptrace(S_FPREGS);
	if (ret)
		return ret;

#ifdef __i386__
	printf("\nmodify using ptrace PTRACE_SETFPXREGS instead of sighandler:\n");
	ret = test_fp_ptrace(S_FPXREGS);
	if (ret)
		return ret;
#endif


	printf("\nmodify using ptrace PTRACE_SETREGSET, via NT_PRFPREG instead of sighandler:\n");
	ret = test_fp_ptrace(S_GETREGS_FPREGS);
	if (ret)
		return ret;

#ifdef __i386__
	printf("\nmodify using ptrace PTRACE_SETREGSET, via NT_XFPREGS instead of sighandler:\n");
	ret = test_fp_ptrace(S_GETREGS_XFPREGS);
	if (ret)
		return ret;
#endif

	printf("\nmodify using ptrace PTRACE_SETREGSET, via NT_X86_XSTATE instead of sighandler:\n");
	ret = test_fp_ptrace(S_GETREGS_XSTATE);
	if (ret)
		return ret;

#endif
	return 0;
}
