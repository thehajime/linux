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

/**
 * lkl_mem_alloc - allocate memory
 *
 * @mem: the size of memory requested
 *
 */
void *lkl_mem_alloc(unsigned long mem);

/**
 * lkl_mem_free - free memory
 *
 * @mem: the address of memory to be freed
 *
 */
void lkl_mem_free(void *mem);

#endif
