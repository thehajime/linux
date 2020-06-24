/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_UM_MCONTEXT_H
#define __ARCH_UM_MCONTEXT_H

extern void get_regs_from_mc(struct uml_pt_regs *regs, mcontext_t *mc);

#define GET_FAULTINFO_FROM_MC(fi, mc) (fi = fi)

#endif /* __ARCH_UM_MCONTEXT_H */
