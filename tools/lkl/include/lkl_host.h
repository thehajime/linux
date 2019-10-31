/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_HOST_H
#define _LKL_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lkl/asm/host_ops.h>
#include <lkl.h>

extern struct lkl_host_operations lkl_host_ops;

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt: printf like format string
 */
int lkl_printf(const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif
