.. SPDX-License-Identifier: GPL-2.0

UML has been built with CONFIG_MMU since day 0.  The patchset
introduces the nommu mode on UML in a different angle from what Linux
Kernel Library tried.

.. contents:: :local:

What is it for ?
================

- Alleviate syscall hook overhead implemented with ptrace(2)
- To exercises nommu code over UML (and over KUnit)
- Less dependency to host facilities


How it works ?
==============

To illustrate how this feature works, the below shows how syscalls are
called under nommu/UML environment.

- boot kernel, install seccomp filter if ``syscall`` instructions are
  called from userspace memory based on the address of instruction
  pointer
- (userspace starts)
- calls ``vfork``/``execve`` syscalls
- ``SIGSYS`` signal raised, handler calls syscall entry point ``__kernel_vsyscall``
- call handler function in ``sys_call_table[]`` and follow how UML syscall
  works.
- return to userspace


What are the differences from MMU-full UML ?
============================================

The current nommu implementation adds 3 different functions which
MMU-full UML doesn't have:

- kernel address space can directly be accessible from userspace
  - so, ``uaccess()`` always returns 1
  - generic implementation of memcpy/strcpy/futex is also used
- alternate syscall entrypoint without ptrace
- alternate syscall hook
  - hook syscall by seccomp filter

With those modifications, it allows us to use unmodified userspace
binaries with nommu UML.


History
=======

This feature was originally introduced by Ricardo Koller at Open
Source Summit NA 2020, then integrated with the syscall translation
functionality with the clean up to the original code.

Building and run
================

::

   make ARCH=um x86_64_nommu_defconfig
   make ARCH=um

will build UML with ``CONFIG_MMU=n`` applied.

Kunit tests can run with the following command::

   ./tools/testing/kunit/kunit.py run --kconfig_add CONFIG_MMU=n

To run a typical Linux distribution, we need nommu-aware userspace.
We can use a stock version of Alpine Linux with nommu-built version of
busybox and musl-libc.


Preparing root filesystem
=========================

nommu UML requires to use a specific standard library which is aware
of nommu kernel.  We have tested custom-build musl-libc and busybox,
both of which have built-in support for nommu kernels.

There are no available Linux distributions for nommu under x86_64
architecture, so we need to prepare our own image for the root
filesystem.  We use Alpine Linux as a base distribution and replace
busybox and musl-libc on top of that.  The following are the step to
prepare the filesystem for the quick start::

     container_id=$(docker create ghcr.io/thehajime/alpine:3.20.3-um-nommu)
     docker start $container_id
     docker wait $container_id
     docker export $container_id > alpine.tar
     docker rm $container_id

     mnt=$(mktemp -d)
     dd if=/dev/zero of=alpine.ext4 bs=1 count=0 seek=1G
     sudo chmod og+wr "alpine.ext4"
     yes 2>/dev/null | mkfs.ext4 "alpine.ext4" || true
     sudo mount "alpine.ext4" $mnt
     sudo tar -xf alpine.tar -C $mnt
     sudo umount $mnt

This will create a file image, ``alpine.ext4``, which contains busybox
and musl with nommu build on the Alpine Linux root filesystem.  The
file can be specified to the argument ``ubd0=`` to the UML command line::

  ./vmlinux ubd0=./alpine.ext4 rw mem=1024m loglevel=8 init=/sbin/init

We plan to upstream apk packages for busybox and musl so that we can
follow the proper procedure to set up the root filesystem.


Quick start with docker
=======================

There is a docker image that you can quickly start with a simple step::

  docker run -it -v /dev/shm:/dev/shm --rm ghcr.io/thehajime/alpine:3.20.3-um-nommu

This will launch a UML instance with an pre-configured root filesystem.

Benchmark
=========

The below shows an example of performance measurement conducted with
lmbench and (self-crafted) getpid benchmark (with v6.12-rc2 uml/next
tree).

.. csv-table:: lmbench (usec)
  :header: ,native,um,um-nommu(s)

  select-10    ,0.5544,29.7143,2.8920
  select-100   ,2.3992,27.7262,3.7794
  select-1000  ,20.4708,42.0885,12.6920
  syscall      ,0.1734,26.2471,2.6070
  read         ,0.3433,29.8828,2.6923
  write        ,0.2866,25.9753,2.6925
  stat         ,1.9195,40.1164,3.1813
  open/close   ,3.8657,63.4730,6.2049
  fork+sh      ,1161.1111,5216.5000,462.3077
  fork+execve  ,536.5263,2117.0000,131.0633

.. csv-table:: do_getpid bench (nsec)
  :header: ,native,um,um-nommu(s)

  getpid,  172 , 26807 , 2614

Limitations
===========

generic nommu limitations
-------------------------
Since this port is a kernel of nommu architecture so, the
implementation inherits the characteristics of other nommu kernels
(riscv, arm, etc), described below.

- vfork(2) should be used instead of fork(2)
- ELF loader only loads PIE (position independent executable) binaries
- processes share the address space among others
- mmap(2) offers a subset of functionalities (e.g., unsupported
  MMAP_FIXED)

Thus, we have limited options to userspace programs.  We have tested
Alpine Linux with musl-libc, which has a support nommu kernel.

supported architecture
----------------------
The current implementation of nommu UML only works on x86_64 SUBARCH.
We have not tested with 32-bit environment.


Further readings about NOMMU UML
================================

- NOMMU UML (original code by Ricardo Koller)
 - https://static.sched.com/hosted_files/ossna2020/ec/kollerr_linux_um_nommu.pdf
