#!/bin/sh

set -x
mount proc /proc -t proc
mount -t tmpfs tmpfs /tmp
export PATH=/home:/sbin:/usr/sbin:/bin:/usr/bin

/root/kselftest_install/run_kselftest.sh -p

RET=$?

find /tmp/ -name nommu_\* -exec /root/tools/testing/kunit/kunit.py parse {} \;
#cat /root/kselftest_install/output.log | /root/tools/testing/kunit/kunit.py parse

echo $RET
echo $RET > /proc/exitcode
halt -f
