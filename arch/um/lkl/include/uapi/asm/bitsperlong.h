/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_UAPI_LKL_BITSPERLONG_H
#define _ASM_UAPI_LKL_BITSPERLONG_H

#ifdef CONFIG_64BIT
#define __BITS_PER_LONG 64
#else
#define __BITS_PER_LONG 32
#endif

#define __ARCH_WANT_STAT64

#endif /* _ASM_UAPI_LKL_BITSPERLONG_H */
