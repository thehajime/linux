.. SPDX-License-Identifier: GPL-2.0

UML has been built with CONFIG_MMU since day 0.  The feature
introduces the nommu mode in a different angle from what Linux Kernel
Library tried.

.. contents:: :local:

What is it for ?
===============

- Alleviate syscall hook overhead implemented with ptrace(2)
- To exercises nommu code over UML (and over KUnit)
- Less dependency to host facilities


How it works ?
==============

To illustrate how this feature works, the below shows how syscalls are
called under NOMMU/UML environment.

- boot kernel, setup zpoline trampoline code (detailed later) at address 0x0
- (userspace starts)
- calls vfork/execve syscalls
- during execve, more specifically during load_elf_fdpic_binary()
  function, kernel translates `syscall/sysenter` instructions with `call
  *%rax`, which usually point to address 0 to NR_syscalls (around
  512), where trampoline code was installed during startup.
- when syscalls are issued by userspace, it jumps to *%rax, slides
  until `nop` instructions end, and jump to hooked function,
  `__kernel_vsyscall`, which is an entrypoint for syscall under NOMMU
  UML environment.
- call handler function in sys_call_table[] and follow how UML syscall
  works.
- return to userspace


What are the differences from MMU-full UML ?
============================================

The current NOMMU implementation adds 3 different functions which
MMU-full UML doesn't have:

- kernel address space can directly be accessible from userspace
  - so, uaccess() always returns 1
  - generic implementation of memcpy/strcpy/futex is also used
- alternate syscall entrypoint without ptrace
- translation of syscall/sysenter instructions to a trampoline code
  and syscall hooks

With those modifications, it allows us to use unmodified userspace
binaries with NOMMU UML.


History
=======

This feature was originally introduced by Ricardo Koller at Open
Source Summit NA 2020, then integrated with the syscall translation
functionality with the clean up to the original code.

Building and run
================

```
% make ARCH=um x86_64_nommu_defconfig
% make ARCH=um
```

will build UML with CONFIG_MMU=n applied.

To run a typical Linux distribution, we need NOMMU-aware userspace.
We can use a stock version of Alpine Linux with nommu-built version of
busybox.

Those tests can run with the following kselftest command:

```
% ./tools/testing/kunit/kunit.py run --kconfig_add CONFIG_MMU=n
```

Limitations
===========

generic nommu limitations
-------------------------
Since this port is a kernel of nommu architecture so, the
implementation inherits the characteristics of other nommu kernels
(riscv, arm, etc), described below.

- vfork(2) should be used instead of fork(2)
- ELF loader only loads PIE (position independent executable)
- processes share the address space among others
- mmap(2) offers a subset of functionalities (e.g., unsupported
  MMAP_FIXED)

Thus, we have limited options to userspace programs.  We have tested
Alpine Linux with musl-libc, which has a support nommu kernel.

mmap(2)
-------
Limited support of mmap(2) syscall has workaround when MMAP_FIXED flag
is used.  This is implemented in musl-libc under nommu environment.
But this features is available as a compile-time option, not a runtime
option.  We may prepare a dedicated libc for nommu UML, but may
propose to change the functionality as a runtime-configurable to
musl-libc community.

access to mmap_min_addr
----------------------
As the mechanism of syscall translations relies on an ability to
write/read memory address zero (0x0), we need to configure host kernel
with the following command:

```
% sh -c "echo 0 > /proc/sys/vm/mmap_min_addr"
```

supported architecture
----------------------
The current implementation of nommu UML only works on x86_64 SUBARCH.
We have not tested with 32-bit environment.

target of syscall translation
-----------------------------
The syscall translation only applies to the executable and interpreter
of ELF binary files which are processed by execve(2) syscall for the
moment: other libraries such as linked library and dlopen-ed one
aren't translated; we may be able to trigger the translation by
LD_PRELOAD.

Note that with musl-libc in Alpine Linux which we've been tested, most
of syscalls are implemented in the interpreter file
(ld-musl-x86_64.so) and calling syscall/sysenter instructions from the
linked/loaded libraries might be rare.  But it is definitely possible
so, a workaround with LD_PRELOAD is effective.


Further readings about NOMMU UML
================================

- NOMMU UML (original code by Ricardo Koller)
https://static.sched.com/hosted_files/ossna2020/ec/kollerr_linux_um_nommu.pdf

- zpoline: syscall translation mechanism
https://www.usenix.org/conference/atc23/presentation/yasukata
