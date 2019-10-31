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

extern char lkl_virtio_devs[4096];

#ifdef LKL_HOST_CONFIG_POSIX
#include <sys/uio.h>
#else
struct iovec {
	void *iov_base;
	size_t iov_len;
};
#endif

extern struct lkl_dev_blk_ops lkl_dev_blk_ops;

/**
 * struct lkl_blk_req - block device request
 *
 * @type: type of request
 * @prio: priority of request - currently unused
 * @sector: offset in units 512 bytes for read / write requests
 * @buf: an array of buffers to be used for read / write requests
 * @count: the number of buffers
 */
struct lkl_blk_req {
#define LKL_DEV_BLK_TYPE_READ		0
#define LKL_DEV_BLK_TYPE_WRITE		1
#define LKL_DEV_BLK_TYPE_FLUSH		4
#define LKL_DEV_BLK_TYPE_FLUSH_OUT	5
	unsigned int type;
	unsigned int prio;
	unsigned long long sector;
	struct iovec *buf;
	int count;
};

/**
 * struct lkl_dev_blk_ops - block device host operations
 */
struct lkl_dev_blk_ops {
	/**
	 * @get_capacity: returns the disk capacity in bytes
	 *
	 * @disk - the disk for which the capacity is requested;
	 * @res - pointer to receive the capacity, in bytes;
	 * @returns - 0 in case of success, negative value in case of error
	 */
	int (*get_capacity)(struct lkl_disk disk, unsigned long long *res);
#define LKL_DEV_BLK_STATUS_OK		0
#define LKL_DEV_BLK_STATUS_IOERR	1
#define LKL_DEV_BLK_STATUS_UNSUP	2
	/**
	 * @request: issue a block request
	 *
	 * @disk - the disk the request is issued to;
	 * @req - a request described by &struct lkl_blk_req
	 */
	int (*request)(struct lkl_disk disk, struct lkl_blk_req *req);
};


#ifdef __cplusplus
}
#endif

#endif
