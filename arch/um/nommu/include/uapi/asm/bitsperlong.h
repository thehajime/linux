/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UM_NOMMU_UAPI_BITSPERLONG_H
#define __UM_NOMMU_UAPI_BITSPERLONG_H

#ifdef CONFIG_64BIT
#define __BITS_PER_LONG 64
#else
#define __BITS_PER_LONG 32
#endif

#endif
