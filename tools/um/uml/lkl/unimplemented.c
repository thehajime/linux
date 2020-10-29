// SPDX-License-Identifier: GPL-2.0
#include <generated/user_constants.h>

struct uml_pt_regs;

/* os-Linux/skas/process.c */
int userspace_pid[UM_NR_CPUS];
void userspace(struct uml_pt_regs *regs, unsigned long *aux_fp_regs)
{}


/* x86/os-Linux/task_size.c */
unsigned long os_get_top_address(void)
{
	return 0;
}

/* start-up.c */
void os_early_checks(void)
{
}
