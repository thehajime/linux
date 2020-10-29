#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
basedir=$(cd $script_dir/..; pwd)
base_objdir=$(cd ${OUTPUT}/; pwd)

TEST_SUCCESS=0
TEST_FAILURE=1
TEST_SKIP=113
TEST_TODO=114
TEST_BAILOUT=115

print_log()
{
    echo " log: |"
    while read line; do
        echo "  $line"
    done < $1
}

export_vars()
{
    if [ -z "$var_file" ]; then
        return
    fi

    for i in $@; do
        echo "$i=${!i}" >> $var_file
    done
}

lkl_test_run()
{
    log_file=$(mktemp)
    export var_file=$(mktemp)

    tid=$1 && shift && tname=$@

    echo "* $tid $tname"

    start=$(date '+%s%9N')
    # run in a separate shell to avoid -e terminating us
    $@ 2>&1 | strings >$log_file
    exit=${PIPESTATUS[0]}
    stop=$(date '+%s%9N')

    case $exit in
    $TEST_SUCCESS)
        echo "ok $tid $tname"
        ;;
    $TEST_SKIP)
        echo "ok $tid $tname # SKIP"
        ;;
    $TEST_BAILOUT)
        echo "not ok $tid $tname"
        echo "Bail out!"
        ;;
    $TEST_FAILURE|*)
        echo "not ok $tid $tname"
        ;;
    esac

    delta=$(((stop-start)/1000))

    echo " ---"
    echo " time_us: $delta"
    print_log $log_file
    echo -e " ..."

    rm $log_file
    . $var_file
    rm $var_file

    return $exit
}

lkl_test_plan()
{
    echo "1..$1 # $2"
    export suite_name="${2// /\-}"
}

lkl_test_exec()
{
    local SUDO=""
    local WRAPPER=""

    if [ "$1" = "sudo" ]; then
        SUDO=sudo
        shift
    fi

    local file=$1
    shift

    if [ -n "$LKL_HOST_CONFIG_NT" ]; then
        file=$file.exe
    fi

    file=${OUTPUT}/$(basename $file)

    if file $file | grep ARM; then
        WRAPPER="qemu-arm-static"
    elif file $file | grep "FreeBSD" ; then
        ssh_copy "$file" $BSD_WDIR
        if [ -n "$SUDO" ]; then
            SUDO=""
        fi
        WRAPPER="$MYSSH $SU"
        # ssh will mess up with pipes ('|') so, escape the pipe char.
        args="${@//\|/\\\|}"
        set - $BSD_WDIR/$(basename $file) $args
        file=""
    elif [ -n "$GDB" ]; then
        WRAPPER="gdb"
        args="$@"
        set - -ex "run $args" -ex quit $file
        file=""
    elif [ -n "$VALGRIND" ]; then
        WRAPPER="valgrind --suppressions=$script_dir/valgrind.supp \
                  --leak-check=full --show-leak-kinds=all --xml=yes \
                  --xml-file=valgrind-$suite_name.xml"
    fi

    $SUDO $WRAPPER $file "$@"
}

lkl_test_cmd()
{
    local WRAPPER=""

    if [ -z "$QUIET" ]; then
        SHOPTS="-x"
    fi

    if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
        WRAPPER="$MYSSH $SU"
    fi

    echo "$@" | $WRAPPER sh $SHOPTS
}

# XXX: $MYSSH and $MYSCP are defined in a circleci docker image.
# see the definitions in lkl/lkl-docker:circleci/freebsd11/Dockerfile
ssh_push()
{
    while [ -n "$1" ]; do
        if [[ "$1" = *.sh ]]; then
            type="script"
        else
            type="file"
        fi

        dir=$(dirname $1)
        $MYSSH mkdir -p $BSD_WDIR/$dir

        $MYSCP -P 7722 -r $basedir/$1 root@localhost:$BSD_WDIR/$dir
        if [ "$type" = "script" ]; then
            $MYSSH chmod a+x $BSD_WDIR/$1
        fi

        shift
    done
}

ssh_copy()
{
    $MYSCP -P 7722 -r $1 root@localhost:$2
}

lkl_test_bsd_cleanup()
{
    $MYSSH rm -rf $BSD_WDIR
}

if [ -n "$LKL_HOST_CONFIG_BSD" ]; then
    trap lkl_test_bsd_cleanup EXIT
    export BSD_WDIR=/root/lkl
    $MYSSH mkdir -p $BSD_WDIR
fi
