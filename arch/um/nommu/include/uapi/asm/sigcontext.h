/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UM_NOMMU_UAPI_SIGCONTEXT_H
#define __UM_NOMMU_UAPI_SIGCONTEXT_H

#include <asm/ptrace-generic.h>

struct sigcontext {
	struct pt_regs regs;
	unsigned long oldmask;
};

#endif
