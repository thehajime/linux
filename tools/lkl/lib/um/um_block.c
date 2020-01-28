// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lkl_host.h>


static int registered_blk_dev_idx;

int lkl_disk_um_add(struct lkl_disk *disk, const char *blkparams)
{
	/* concat strings */
	snprintf(lkl_um_devs + strlen(lkl_um_devs), sizeof(lkl_um_devs),
		 " ubd%d=%s", registered_blk_dev_idx, blkparams);

	return registered_blk_dev_idx++;
}
