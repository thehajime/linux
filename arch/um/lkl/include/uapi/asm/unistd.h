/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_UNISTD_H
#define __UM_LIBMODE_UAPI_UNISTD_H

#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_TIME32_SYSCALLS

#include <asm/bitsperlong.h>

#if __BITS_PER_LONG == 64
#define __ARCH_WANT_SYS_NEWFSTATAT
#else
#define __ARCH_WANT_STAT64
#endif

#include <asm-generic/unistd.h>


#endif
