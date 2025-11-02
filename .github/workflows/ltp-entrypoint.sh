#!/bin/sh

/root/vmlinux vec0:transport=raw,ifname=eth0,depth=128,gro=1 \
	root=/dev/root rootflags=/  rootfstype=hostfs rw \
	mem=4G loglevel=8 cmd_option console=tty init=/ltp-exec.sh $*
