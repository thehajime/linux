#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

clear_wdir()
{
    rm -rf ${wdir}
}

set_cfgjson()
{
    cfgjson=${wdir}/hijack-test$1.conf

    cat > ${cfgjson}

    export_vars cfgjson
}

run_hijack_cfg()
{
    lkl_test_cmd LKL_HIJACK_CONFIG_FILE=$cfgjson $hijack $@
}

run_hijack()
{
    lkl_test_cmd $hijack $@
}

test_ping()
{
    set -e

    run_hijack ${ping} -c 1 127.0.0.1
}

test_ping6()
{
    set -e

    run_hijack ${ping6} -c 1 ::1
}

test_mount_and_dump()
{
    set -e

    set_cfgjson << EOF
    {
        "mount":"proc,sysfs",
        "dump":"/sysfs/class/net/lo/mtu,/sysfs/class/net/lo/dev_id",
        "debug": "1"
    }
EOF

    ans=$(run_hijack_cfg $(lkl_test_cmd which true))
    echo "$ans"
    echo "$ans" | grep "^65536" # lo's MTU
    echo "$ans" | grep "0x0" # lo's dev_id
}

test_boot_cmdline()
{
    set -e

    set_cfgjson << EOF
    {
        "debug":"1",
        "boot_cmdline":"loglevel=1"
    }
EOF

    ans=$(run_hijack_cfg $(lkl_test_cmd which true))
    echo "$ans"
    [ $(echo "$ans" | wc -l) = 1 ]
}


source ${script_dir}/test.sh
source ${script_dir}/net-setup.sh

if [[ ! -e ${base_objdir}/lib/hijack/liblkl-hijack.so ]]; then
    lkl_test_plan 0 "hijack tests"
    echo "missing liblkl-hijack.so"
    exit 0
fi

# Make a temporary directory to run tests in, since we'll be copying
# things there.
wdir=$(mktemp -d)
cp `which ping` ${wdir}
cp `which ping6` ${wdir}
ping=${wdir}/ping
ping6=${wdir}/ping6
hijack=$basedir/bin/lkl-hijack.sh

export LD_LIBRARY_PATH=${base_objdir}/lib/hijack/:$LD_LIBRARY_PATH

# And make sure we clean up when we're done
trap "clear_wdir &>/dev/null" EXIT

lkl_test_plan 5 "hijack basic tests"
lkl_test_run 1 run_hijack ip addr
lkl_test_run 2 run_hijack ip route
lkl_test_run 3 test_ping
lkl_test_run 4 test_ping6
lkl_test_run 5 test_mount_and_dump
lkl_test_run 6 test_boot_cmdline
