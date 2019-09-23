
USER_CFLAGS += -I$(srctree)/tools/lkl/include \
		-Wno-strict-prototypes -Wno-undef \
		-Wframe-larger-than=20480 -O0 -g

USER_OBJS += iomem.o jmp_buf.o virtio.o virtio_net.o \
	 virtio_net_fd.o virtio_net_tap.o utils.o posix-host.o \
	 virtio_blk.o ../../perf/pmu-events/jsmn.o

#obj-y += fs.o
obj-y += iomem.o
#obj-y += net.o
obj-y += jmp_buf.o
obj-y += posix-host.o
#obj-$(LKL_HOST_CONFIG_NT) += nt-host.o
obj-y += utils.o
obj-y += virtio_blk.o
obj-y += virtio.o
#obj-y += dbg.o
#obj-y += dbg_handler.o
obj-y += virtio_net.o
obj-y += virtio_net_fd.o
obj-y += virtio_net_tap.o
#obj-$(LKL_HOST_CONFIG_VIRTIO_NET) += virtio_net_raw.o
#obj-$(LKL_HOST_CONFIG_VIRTIO_NET_MACVTAP) += virtio_net_macvtap.o
#obj-$(LKL_HOST_CONFIG_VIRTIO_NET_DPDK) += virtio_net_dpdk.o
#obj-$(LKL_HOST_CONFIG_VIRTIO_NET_VDE) += virtio_net_vde.o
#obj-$(LKL_HOST_CONFIG_VIRTIO_NET) += virtio_net_pipe.o
obj-y += ../../perf/pmu-events/jsmn.o
#obj-y += config.o

include arch/um/scripts/Makefile.rules
