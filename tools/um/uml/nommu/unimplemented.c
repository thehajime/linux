// SPDX-License-Identifier: GPL-2.0
#include <generated/user_constants.h>

struct uml_pt_regs;

/* os-Linux/skas/process.c */
int userspace_pid[UM_NR_CPUS];
void userspace(struct uml_pt_regs *regs, unsigned long *aux_fp_regs)
{}
