#!/bin/sh

mount proc /proc -t proc
mount -t tmpfs tmpfs /tmp
export PATH=/home:/sbin:/usr/sbin:/bin:/usr/bin


#make kselftests O=build-kselftest TARGETS=mm
#/root/kselftest_install/run_kselftest.sh -l
/root/kselftest_install/run_kselftest.sh -p -t mm:nommu_mmap_test -t mm:nommu_mremap_test

RET=$?

find /tmp/ -name nommu_\* -exec /root/tools/testing/kunit/kunit.py parse {} \;
#cat /root/kselftest_install/output.log | /root/tools/testing/kunit/kunit.py parse

echo $RET
echo $RET > /proc/exitcode
halt -f
