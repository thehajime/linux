// SPDX-License-Identifier: GPL-2.0
/*
 * Test FP register handling in userspace mcontext.
 *
 * Copyright (C) 2025 Intel Corporation
 */

#include <math.h>
#include <signal.h>
#include <types.h>
#include <unistd.h>
#include <string.h>
#include <sys.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>

#include "../../../tools/testing/selftests/kselftest.h"

#define ST0_EXP_ADD 10

static void sighandler(int sig, siginfo_t *info, void *p)
{
	struct ucontext *uc = p;
	struct _fpstate *fpstate = (void *)uc->uc_mcontext.fpstate;

	ksft_print_msg("sighandler: extended_size: %d, xstate_size: %d\n",
		       fpstate->sw_reserved.extended_size,
		       fpstate->sw_reserved.xstate_size);

#ifdef __i386__
	fpstate->_st[0].exponent += ST0_EXP_ADD;
	fpstate->_xmm[1].element[0] |= 0x01010101;
	fpstate->_xmm[1].element[1] |= 0x01010101;
	fpstate->_xmm[1].element[2] |= 0x01010101;
	fpstate->_xmm[1].element[3] |= 0x01010101;
#else
	/* Hacky way of modifying the exponent without breaking aliasing */
	fpstate->st_space[2] += ST0_EXP_ADD;
	fpstate->xmm_space[4] |= 0x01010101;
	fpstate->xmm_space[5] |= 0x01010101;
	fpstate->xmm_space[6] |= 0x01010101;
	fpstate->xmm_space[7] |= 0x01010101;
#endif
}

static int test_mcontext(int xmm_should_change)
{
	double num = 0.5;
	uint32_t sse[4] = {0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 };
	long ret;
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

int main(void)
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO,
		.sa_handler = (void (*)(int))sighandler,
	};

	ksft_print_header();
	ksft_set_plan(1);

	if (sys_sigaction(SIGUSR1, &sa, NULL) < 0)
		ksft_exit_fail_msg("Failed to register sigaction: %d\n", errno);

	if (!test_mcontext(1))
		ksft_test_result_pass("mcontext\n");
	else
		ksft_test_result_fail("mcontext failed!\n");

	ksft_finished();
}
