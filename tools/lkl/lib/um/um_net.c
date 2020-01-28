// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lkl_host.h>

static int registered_net_dev_idx;

struct lkl_netdev *lkl_um_netdev_create(const char *ifparams)
{
	struct lkl_netdev *nd;

	nd = lkl_host_ops.mem_alloc(sizeof(struct lkl_netdev));
	if (!nd)
		return NULL;

	memset(nd, 0, sizeof(struct lkl_netdev));

	nd->id = registered_net_dev_idx++;
	/* concat strings */
	snprintf(lkl_um_devs + strlen(lkl_um_devs), sizeof(lkl_um_devs),
		 " eth%d=%s", nd->id, ifparams);

	return nd;
}
