/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_LKL_SWAB_H
#define _ASM_LKL_SWAB_H

#ifndef __arch_swab32
#define __arch_swab32(x) ___constant_swab32(x)
#endif

#include <asm-generic/swab.h>

#endif /* _ASM_LKL_SWAB_H */
