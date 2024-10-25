FROM alpine:3.20.3

ADD vmlinux /root/.
ADD inittab /etc/inittab
RUN mkdir /etc/init.d
ADD rcS /etc/init.d/.
RUN sh -c "echo "nameserver 8.8.8.8" > /etc/resolv.conf"

ADD bench.sh /
ADD do_getpid /root/.
ADD lmbench2/ /
ADD lmbench_run.sh /lmbench2/bin/x86_64-linux-gnulibc1/.


RUN apk update && apk add utmps-libs libtirpc curl
RUN mkdir -p setup && cd setup && curl -L -o output.zip 'https://gitlab.alpinelinux.org/thehajime/aports/-/jobs/1566363/artifacts/download?file_type=archive'
RUN cd setup && unzip output.zip && \
   tar xfz packages/main/x86_64/musl-nommu-1.2.5-r5.apk
RUN cp setup/lib/ld-musl-x86_64.so.1 /lib/
RUN apk add --allow-untrusted setup/packages/main/x86_64/busybox-nommu-1.36.1-r32.apk
RUN rm -rf setup

ENTRYPOINT /root/vmlinux eth0=tuntap,tap100,0e:fd:0:0:0:1,172.17.0.1 root=/dev/root rootflags=/ rootfstype=hostfs rw mem=1024m loglevel=8 init=/sbin/init
