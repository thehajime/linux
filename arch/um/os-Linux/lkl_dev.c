// SPDX-License-Identifier: GPL-2.0

#include <stdlib.h>
#include <string.h>
#include <init.h>
#include <os.h>
#include <kern_util.h>
#include <errno.h>
#include <fcntl.h>

#include <lkl.h>
#include <lkl_host.h>

extern struct lkl_host_operations lkl_host_ops;
struct lkl_host_operations *lkl_ops = &lkl_host_ops;

static struct lkl_netdev *nd;
static struct lkl_disk disk;

int __init uml_netdev_prepare(char *iftype, char *ifparams, char *ifoffload)
{
	int offload = 0;

	if (ifoffload)
		offload = strtol(ifoffload, NULL, 0);

	if ((strcmp(iftype, "tap") == 0)) {
		nd = lkl_netdev_tap_create(ifparams, offload);
#ifdef notyet
	} else if ((strcmp(iftype, "macvtap") == 0)) {
		nd = lkl_netdev_macvtap_create(ifparams, offload);
#endif
	} else {
		if (offload) {
			lkl_printf("WARN: %s isn't supported on %s\n",
				   "LKL_HIJACK_OFFLOAD",
				   iftype);
			lkl_printf(
				"WARN: Disabling offload features.\n");
		}
		offload = 0;
	}
#ifdef notyet
	if (strcmp(iftype, "raw") == 0)
		nd = lkl_netdev_raw_create(ifparams);
#endif

	return 0;
}


int __init uml_netdev_add(void)
{
	if (nd)
		lkl_netdev_add(nd, NULL);

	return 0;
}
__initcall(uml_netdev_add);

static int __init lkl_eth_setup(char *str, int *niu)
{
	char *end, *iftype, *ifparams, *ifoffload;
	int devid, err = -EINVAL;

	/* veth */
	devid = strtoul(str, &end, 0);
	if (end == str) {
		os_warn("Bad device number\n");
		return err;
	}

	/* = */
	str = end;
	if (*str != '=') {
		os_warn("Expected '=' after device number\n");
		return err;
	}
	str++;

	/* <iftype> */
	iftype = str;

	/* <ifparams> */
	ifparams = strchr(str, ',');
	if (ifparams == NULL) {
		os_warn("failed to parse ifparams\n");
		return -1;
	}
	*ifparams = '\0';
	ifparams++;

	str = ifparams;
	/* <offload> */
	ifoffload = strchr(str, ',');
	*ifoffload = '\0';
	ifoffload++;

	os_info("str=%s, iftype=%s, ifparams=%s, offload=%s\n",
		str, iftype, ifparams, ifoffload);

	/* preparation */
	uml_netdev_prepare(iftype, ifparams, ifoffload);

	return 1;
}

__uml_setup("veth", lkl_eth_setup,
"veth[0-9]+=<iftype>,<ifparams>,<offload>\n"
"    Configure a network device.\n\n"
);

int __init uml_blkdev_add(void)
{
	int disk_id = 0;

	if (disk.fd)
		disk_id = lkl_disk_add(&disk);

	if (disk_id < 0)
		return -1;

	return 0;
}
__initcall(uml_blkdev_add);

static int __init lkl_ubd_setup(char *str, int *niu)
{
	char *end, *fname;
	int devid, err = -EINVAL;

	/* veth */
	devid = strtoul(str, &end, 0);
	if (end == str) {
		os_warn("Bad device number\n");
		return err;
	}

	/* = */
	str = end;
	if (*str != '=') {
		os_warn("Expected '=' after device number\n");
		return err;
	}
	str++;

	/* <filename> */
	fname = str;

	os_info("fname=%s\n", fname);
	/* create */
	disk.fd = open(fname, O_RDWR);
	if (disk.fd < 0)
		return -1;

	disk.ops = NULL;

	return 1;
}
__uml_setup("vubd", lkl_ubd_setup,
"vubd<n>=<filename>\n"
"    Configure a block device.\n\n"
);


/* stub functions */
int lkl_is_running(void)
{
	return 1;
}

void lkl_put_irq(int i, const char *user)
{
}

/* XXX */
static int free_irqs[2] = {5, 13};
int lkl_get_free_irq(const char *user)
{
	static int irq_idx;
	return free_irqs[irq_idx++];
}

int lkl_trigger_irq(int irq)
{
	do_IRQ(irq, NULL);
	return 0;
}
