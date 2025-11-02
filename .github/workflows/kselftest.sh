#!/bin/sh

mount proc /proc -t proc
export PATH=/home:/sbin:/usr/sbin:/bin:/usr/bin


#make kselftests O=build-kselftest TARGETS=mm
/root/kselftest_install/run_kselftest.sh -l
/root/kselftest_install/run_kselftest.sh -s -t mm:nommu_mmap_test -t mm:nommu_mremap_test

RET=$?
cat /root/kselftest_install/output.log
echo $RET
echo $RET > /proc/exitcode
halt -f
