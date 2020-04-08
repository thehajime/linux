/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_UAPI_LKL_HOST_OPS_H
#define _ASM_UAPI_LKL_HOST_OPS_H

/**
 * lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 *
 */
struct lkl_host_operations {
	int (*register_irq_fd)(int irq, int fd, int type, void *dev_id);
	void (*unregister_irq_fd)(int irq, int fd);
};

#endif
