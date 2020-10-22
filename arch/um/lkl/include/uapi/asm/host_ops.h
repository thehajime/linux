/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_HOST_OPS_H
#define __UM_LIBMODE_UAPI_HOST_OPS_H

/**
 * struct lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 *
 */
struct lkl_host_operations {
};

/**
 * lkl_bug - call lkl_panic with a message
 *
 * @fmt: message format with parameters
 *
 */
void lkl_bug(const char *fmt, ...);

#endif
