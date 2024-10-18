
PATH=/usr/lib/lmbench/bin/x86_64-linux-gnu/:/lmbench2/bin/x86_64-linux-gnulibc1:$PATH
mkdir -p /var/tmp/lmbench
cp `which hello` /var/tmp/lmbench/hello
cp `which hello` /tmp/hello

mkdir -p /usr/include/sys/ || true
touch /usr/include/sys/types.h || true

if [ -d "/usr/lib/lmbench/bin/x86_64-linux-gnu/" ] ; then
ENOUGH=10000  lat_select -n 10   file
ENOUGH=10000  lat_select -n 100  file
ENOUGH=10000  lat_select -n 1000 file
else
ENOUGH=10000  lat_select file 10
ENOUGH=10000  lat_select file 100
ENOUGH=10000  lat_select file 1000
fi

ENOUGH=100000  lat_syscall null
ENOUGH=100000  lat_syscall read
ENOUGH=100000  lat_syscall write
ENOUGH=100000  lat_syscall stat
ENOUGH=100000  lat_syscall open
ENOUGH=10000  lat_proc shell
ENOUGH=10000  lat_proc exec
