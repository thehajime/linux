// SPDX-License-Identifier: GPL-2.0

struct uml_pt_regs;

int get_fp_registers(int pid, unsigned long *regs)
{
	return 0;
}

int save_i387_registers(int pid, unsigned long *fp_regs)
{
	return 0;
}

void arch_init_registers(int pid)
{
}

void get_regs_from_mc(struct uml_pt_regs *regs, void *mc)
{
}
