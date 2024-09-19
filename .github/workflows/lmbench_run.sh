cp /usr/lib/lmbench/bin/x86_64-linux-gnu/hello /var/tmp/lmbench/hello

PATH=/usr/lib/lmbench/bin/x86_64-linux-gnu/:$PATH

ENOUGH=10000  lat_select -n 10   file
ENOUGH=10000  lat_select -n 100  file
ENOUGH=10000  lat_select -n 1000 file

ENOUGH=100000  lat_syscall null
ENOUGH=100000  lat_syscall read
ENOUGH=100000  lat_syscall write
ENOUGH=100000  lat_syscall stat
ENOUGH=100000  lat_syscall open
ENOUGH=10000  lat_proc shell
ENOUGH=10000  lat_proc exec
