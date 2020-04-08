/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_UAPI_LKL_HOST_OPS_H
#define _ASM_UAPI_LKL_HOST_OPS_H

/**
 * lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 * @print - optional operation that receives console messages
 * @panic - called during a kernel panic
 *
 * @mem_alloc - allocate memory
 * @mem_free - free memory
 *
 */
struct lkl_host_operations {
	void (*print)(const char *str, int len);
	void (*panic)(void);

	void *(*mem_alloc)(unsigned long mem);
	void (*mem_free)(void *mem);
};

#endif
