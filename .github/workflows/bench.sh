#!/bin/sh

mount proc /proc -t proc
export PATH=/home:/sbin:/usr/sbin:/bin:/usr/bin


cd /lmbench2/bin/x86_64-linux-gnulibc1
sh lmbench_run.sh

/root/do_getpid -c 10000

# test umh (usermode helper) to exec `modprobe`
ifconfig eth10

make -C /root tests
RET=$?
echo $RET
echo $RET > /proc/exitcode
halt -f
