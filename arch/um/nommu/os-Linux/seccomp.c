// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <init.h>
#include <as-layout.h>
#include <os.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

int __init os_setup_seccomp(void)
{
	int err;
	unsigned long __userspace_start = uml_reserved,
		__userspace_end = high_physmem;

	struct sock_filter filter[] = {
		/* if (IP_high > __userspace_end) allow; */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP(BPF_JMP + BPF_JGT + BPF_K, __userspace_end >> 32,
			 /*true-skip=*/0, /*false-skip=*/1),
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

		/* if (IP_high == __userspace_end && IP_low >= __userspace_end) allow; */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __userspace_end >> 32,
			 /*true-skip=*/0, /*false-skip=*/3),
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer)),
		BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, __userspace_end,
			 /*true-skip=*/0, /*false-skip=*/1),
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

		/* if (IP_high < __userspace_start) allow; */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, __userspace_start >> 32,
			 /*true-skip=*/1, /*false-skip=*/0),
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

		/* if (IP_high == __userspace_start && IP_low < __userspace_start) allow; */
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __userspace_start >> 32,
			 /*true-skip=*/0, /*false-skip=*/3),
		BPF_STMT(BPF_LD + BPF_W + BPF_ABS,
			 offsetof(struct seccomp_data, instruction_pointer)),
		BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, __userspace_start,
			 /*true-skip=*/1, /*false-skip=*/0),
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

		/* other address; trap  */
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP),
	};
	struct sock_fprog prog = {
		.len = ARRAY_SIZE(filter),
		.filter = filter,
	};

	err = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (err)
		os_warn("PR_SET_NO_NEW_PRIVS (err=%d, ernro=%d)\n",
		       err, errno);

	err = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER,
		      SECCOMP_FILTER_FLAG_TSYNC, &prog);
	if (err) {
		os_warn("SECCOMP_SET_MODE_FILTER (err=%d, ernro=%d)\n",
		       err, errno);
		exit(1);
	}

	set_handler(SIGSYS);

	os_info("seccomp: setup filter syscalls in the range: 0x%lx-0x%lx\n",
		__userspace_start, __userspace_end);

	return 0;
}

