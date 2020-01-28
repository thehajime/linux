/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_HOST_H
#define _LKL_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lkl/asm/host_ops.h>
#include <lkl.h>

extern struct lkl_host_operations lkl_host_ops;
extern char lkl_um_devs[4096];

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt: printf like format string
 */
int lkl_printf(const char *fmt, ...);

#ifdef LKL_HOST_CONFIG_POSIX
#include <sys/uio.h>
#else
struct iovec {
	void *iov_base;
	size_t iov_len;
};
#endif

struct lkl_netdev {
	struct lkl_dev_net_ops *ops;
	int id;
	uint8_t has_vnet_hdr: 1;
};

/**
 * struct lkl_dev_net_ops - network device host operations
 */
struct lkl_dev_net_ops {
	/**
	 * @tx: writes a L2 packet into the net device
	 *
	 * The data buffer can only hold 0 or 1 complete packets.
	 *
	 * @nd - pointer to the network device;
	 * @iov - pointer to the buffer vector;
	 * @cnt - # of vectors in iov.
	 *
	 * @returns number of bytes transmitted
	 */
	int (*tx)(struct lkl_netdev *nd, struct iovec *iov, int cnt);

	/**
	 * @rx: reads a packet from the net device.
	 *
	 * It must only read one complete packet if present.
	 *
	 * If the buffer is too small for the packet, the implementation may
	 * decide to drop it or trim it.
	 *
	 * @nd - pointer to the network device
	 * @iov - pointer to the buffer vector to store the packet
	 * @cnt - # of vectors in iov.
	 *
	 * @returns number of bytes read for success or < 0 if error
	 */
	int (*rx)(struct lkl_netdev *nd, struct iovec *iov, int cnt);

#define LKL_DEV_NET_POLL_RX		1
#define LKL_DEV_NET_POLL_TX		2
#define LKL_DEV_NET_POLL_HUP		4

	/**
	 * @poll: polls a net device
	 *
	 * Supports the following events: LKL_DEV_NET_POLL_RX
	 * (readable), LKL_DEV_NET_POLL_TX (writable) or
	 * LKL_DEV_NET_POLL_HUP (the close operations has been issued
	 * and we need to clean up). Blocks until one event is
	 * available.
	 *
	 * @nd - pointer to the network device
	 *
	 * @returns - LKL_DEV_NET_POLL_RX, LKL_DEV_NET_POLL_TX,
	 * LKL_DEV_NET_POLL_HUP or a negative value for errors
	 */
	int (*poll)(struct lkl_netdev *nd);

	/**
	 * @poll_hup: make poll wakeup and return LKL_DEV_NET_POLL_HUP
	 *
	 * @nd - pointer to the network device
	 */
	void (*poll_hup)(struct lkl_netdev *nd);

	/**
	 * @free: frees a network device
	 *
	 * Implementation must release its resources and free the network device
	 * structure.
	 *
	 * @nd - pointer to the network device
	 */
	void (*free)(struct lkl_netdev *nd);
};

#ifdef __cplusplus
}
#endif

#endif
