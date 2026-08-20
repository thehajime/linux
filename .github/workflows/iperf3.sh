#!/bin/sh

NIC=vec0
mount proc /proc -t proc
echo "nameserver 8.8.8.8" > /etc/resolv.conf
/sbin/ifconfig lo 127.0.0.1 up
/sbin/ifconfig $NIC 172.17.0.2 up
/sbin/ip route add default via 172.17.0.1 dev $NIC

/sbin/sysctl -w net.ipv4.tcp_wmem="40960 873800 1677721600"
/sbin/sysctl -w net.ipv4.tcp_rmem="40960 873800 1677721600"

sleep 5
IPERF=/root/iperf3.static
IPERF=iperf3

echo "===iperf3 forward==="
$IPERF -c 172.17.0.1 -fm
echo "===iperf3 reverse==="
$IPERF -c 172.17.0.1 -R -fm

sh /root/netperf-bench.sh

/sbin/halt -f
