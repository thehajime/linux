#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

source $script_dir/test.sh
source $script_dir/net-setup.sh

cleanup_backend()
{
    set -e

    case "$1" in
    "loopback")
        ;;
    "um")
        ;;
    esac
}

get_test_ip()
{
    # DHCP test parameters
    TEST_HOST=8.8.8.8
    HOST_IF=$(lkl_test_cmd ip route get $TEST_HOST | head -n1 |cut -d ' ' -f5)
    HOST_GW=$(lkl_test_cmd ip route get $TEST_HOST | head -n1 | cut -d ' ' -f3)
    if lkl_test_cmd ping -c1 -w1 $HOST_GW; then
        TEST_IP_REMOTE=$HOST_GW
    elif lkl_test_cmd ping -c1 -w1 $TEST_HOST; then
        TEST_IP_REMOTE=$TEST_HOST
    else
        echo "could not find remote test ip"
        return $TEST_SKIP
    fi

    export_vars HOST_IF TEST_IP_REMOTE
}

setup_backend()
{
    set -e

    if [ "$LKL_HOST_CONFIG_POSIX" != "y" ] &&
       [ "$1" != "loopback" ]; then
        echo "not a posix environment"
        return $TEST_SKIP
    fi

    case "$1" in
    "loopback")
        ;;
    "um")
	# only intel arch is capable with um-net backent
	if [ -z "$LKL_HOST_CONFIG_UML_DEV" ]; then
            return $TEST_SKIP
	fi
	# slirp's helper process doesn't work with valgrind
	if [ -n "$VALGRIND" ]; then
            return $TEST_SKIP
	fi
        ;;
    *)
        echo "don't know how to setup backend $1"
        return $TEST_FAILED
        ;;
    esac
}

run_tests()
{
    case "$1" in
    "loopback")
        lkl_test_exec $script_dir/net-test --dst 127.0.0.1
        ;;
    "um")
        lkl_test_exec $script_dir/net-test --backend um \
                      --ifname $TEST_UM_SLIRP_PARMS \
                      --ip 10.0.2.15 --netmask-len 8 \
                      --dst 10.0.2.2
        ;;
    esac
}

if [ "$1" = "-b" ]; then
    shift
    backend=$1
    shift
fi

if [ -z "$backend" ]; then
    backend="loopback"
fi

lkl_test_plan 1 "net $backend"
lkl_test_run 1 setup_backend $backend

if [ $? = $TEST_SKIP ]; then
    exit 0
fi

trap "cleanup_backend $backend" EXIT

run_tests $backend

trap : EXIT
lkl_test_plan 1 "net $backend"
lkl_test_run 1 cleanup_backend $backend

