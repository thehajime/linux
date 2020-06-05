#ifndef _ASM_UAPI_LKL_SIGCONTEXT_H
#define _ASM_UAPI_LKL_SIGCONTEXT_H

#include <asm/ptrace-generic.h>

struct sigcontext {
	struct pt_regs regs;
	unsigned long oldmask;
};

#endif
