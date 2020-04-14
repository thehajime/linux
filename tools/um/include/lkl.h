/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_H
#define _LKL_H

#ifdef __cplusplus
extern "C" {
#endif

#define _LKL_LIBC_COMPAT_H

#ifdef __cplusplus
#define class __lkl__class
#endif

/*
 * Avoid collisions between Android which defines __unused and
 * linux/icmp.h which uses __unused as a structure field.
 */
#pragma push_macro("__unused")
#undef __unused

#include <lkl/asm/syscalls.h>

#pragma pop_macro("__unused")

#ifdef __cplusplus
#undef class
#endif

/**
 * lkl_strerror - returns a string describing the given error code
 *
 * @err - error code
 * @returns - string for the given error code
 */
const char *lkl_strerror(int err);

/**
 * lkl_perror - prints a string describing the given error code
 *
 * @msg - prefix for the error message
 * @err - error code
 */
void lkl_perror(char *msg, int err);



#ifdef __cplusplus
}
#endif

#endif
