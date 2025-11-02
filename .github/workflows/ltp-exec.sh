#!/bin/sh

set -x
mount proc /proc -t proc
mkdir -p /dev/shm
mount -t tmpfs -o rw,nosuid,nodev tmpfs /dev/shm
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

export PATH=/home:/sbin:/usr/sbin:/bin:/usr/bin

modprobe loop

echo "nameserver 8.8.8.8" > /etc/resolv.conf
/sbin/ifconfig lo 127.0.0.1 up
/sbin/ip addr add 172.18.0.2/16 dev vec0
/sbin/ip link set up dev vec0
/sbin/ip route add default via 172.18.0.1 dev vec0


## XXX
dd if=/dev/zero of=/tmp/test.img bs=1M count=10
losetup /dev/loop0 /tmp/test.img

sleep 5

/opt/ltp/kirk -f syscalls --run-pattern "" \
	      --skip-file ltp-skip -o /result.json

#--workers 16

ip addr show
ip route show

cp -rpf /tmp/kirk.root/ /github/home/

halt -f
