FROM alpine:3.20.3

ARG MODE=
ADD vmlinux /root/.
ADD inittab /etc/inittab
RUN mkdir /etc/init.d
ADD rcS /etc/init.d/.
RUN sed "s/\'/\"/g" /etc/profile > /.profile


ADD bench.sh /
ADD iperf3.sh /root/
ADD netperf-bench.sh /root/
ADD iperf3.static /root/
ADD do_getpid /root/.
ADD clone /root/.
ADD futex /root/.
ADD nullptr /root/.
ADD ctest /root/.
ADD stackp /root/.
ADD init_array /root/.
ADD entropy /root/.
ADD locale /root/.
ADD vdso_test /root/.
ADD pthread /root/.
ADD Makefile /root/.
ADD lmbench2/ /lmbench2
ADD lmbench_run.sh /lmbench2/bin/x86_64-linux-gnulibc1/
ADD netperf /root/.
ADD lib/ /lib
ADD kselftest_install/ /root/kselftest_install
ADD kselftest.sh /root/
RUN mkdir -p /root/tools/testing/
ADD kunit/  /root/tools/testing/kunit


RUN apk update && apk add utmps-libs libtirpc curl make iperf3 coreutils python3

RUN if [[ ${MODE} == "um-nommu" ]]; then mkdir -p setup && cd setup && curl -L -o output.zip \
    'https://gitlab.alpinelinux.org/thehajime/aports/-/jobs/2475393/artifacts/download?file_type=archive' \
    && unzip output.zip ; fi
RUN if [[ ${MODE} == "um-nommu" ]]; then apk add --allow-untrusted \
   setup/packages/main/x86_64/busybox-nommu-1.36.1-r29.apk \
   setup/packages/main/x86_64/musl-nommu-1.2.5-r0.apk &&  rm -rf setup; fi

# for ifupdown
RUN mkdir -p /etc/network
ADD interfaces /etc/network/.

# XXX
RUN mkdir -p /usr/share/udhcpc
ADD default.script /usr/share/udhcpc/.
RUN chmod +x /usr/share/udhcpc/default.script

ENTRYPOINT ["/root/vmlinux", "vec0:transport=raw,ifname=eth0,depth=128,gro=1", "root=/dev/root", "rootflags=/", "rootfstype=hostfs", "rw", "mem=2048m", "loglevel=0", "init=/sbin/init"]
