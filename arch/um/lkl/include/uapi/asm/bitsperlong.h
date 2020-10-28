/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_BITSPERLONG_H
#define __UM_LIBMODE_UAPI_BITSPERLONG_H

/* need to add new arch defines here, as we cannot use CONFIG_64BIT here
 * to avoid CONFIG leaks to userspace
 */
#if defined(__x86_64__)
#define __BITS_PER_LONG 64
#else
#define __BITS_PER_LONG 32
#endif

#include <asm-generic/bitsperlong.h>

#endif
