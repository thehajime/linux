#!/bin/sh

PSIZES="64 128 256 512 1024 1500 65507"
TESTNAMES="TCP_STREAM TCP_MAERTS"
DEST_ADDR=${1:-"172.17.0.1"}
NETPERF=${2:-/root/netperf}

/sbin/sysctl -w net.ipv4.tcp_wmem="40960 873800 1677721600"
/sbin/sysctl -w net.ipv4.tcp_rmem="40960 873800 1677721600"

for size in $PSIZES
do
    for test in $TESTNAMES
    do
	echo "== netperf ($size-$test)  =="
	$NETPERF -H $DEST_ADDR -t $test -- -o THROUGHPUT,THROUGHPUT_UNITS,LOCAL_SEND_SIZE,COMMAND_LINE -m "$size,$size"

    done
done

$NETPERF -H $DEST_ADDR -t TCP_RR -- -o THROUGHPUT,THROUGHPUT_UNITS,LOCAL_SEND_SIZE,COMMAND_LINE
