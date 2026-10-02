Occupied-target MREMAP_FIXED
----------------------------

MREMAP_FIXED with an occupied destination is currently unsupported for the
SWMMU path.

The current remap flow removes the destination mapping before preparing and
executing the source move. If source move preparation, pagetable allocation,
page copying, or destination insertion fails, restoring the original
destination mapping is not transactional.

This limitation is not inherently SWMMU-specific. The MMU implementation also
does not currently provide the shared transactional behavior required for this
case. Implementing occupied-target MREMAP_FIXED should therefore be handled in
a separate MMU/SWMMU patchset.

Until then, MREMAP_FIXED with an occupied destination remains unsupported and
its test is skipped. Empty-target MREMAP_FIXED remains supported.

Fault and error behavior
------------------------

SWMMU checked accesses support sizes of 1, 2, 4, and 8 bytes. The complete
access is validated before data is copied.

A successful load returns zero and writes the loaded value to the result
argument. A successful store returns zero.

The access syscall supports two modes:

``NOMMU_SWMMU_ACCESS_CHECKED``
   Return an error to the caller without delivering a signal.

``NOMMU_SWMMU_ACCESS_SIGNAL``
   Convert SWMMU address and permission faults into synchronous ``SIGSEGV``
   delivery.

The checked-access error contract is:

``-EINVAL``
   The access size, flags, or another argument is invalid.

``-EFAULT``
   The address is unmapped, outside a SWMMU VMA, or exceeds the mapped range.

``-EACCES``
   The access violates the mapping protection.

``-EOPNOTSUPP``
   SWMMU mode is disabled for the current process.

For signal-mode accesses, the kernel maps SWMMU faults as follows:

``-EFAULT``
   ``SIGSEGV`` with ``si_code`` set to ``SEGV_MAPERR``.

``-EACCES``
   ``SIGSEGV`` with ``si_code`` set to ``SEGV_ACCERR``.

The faulting address is reported through ``siginfo_t.si_addr``. Invalid
arguments and disabled SWMMU mode remain ordinary syscall errors and do not
generate ``SIGSEGV``.

When a signal handler returns, the faulting access is retried. The default
``SIGSEGV`` disposition terminates the process.

Kernel mapping inconsistencies are internal errors and are not user-visible
SWMMU access faults.

UML configuration scope
-----------------------

The current SWMMU host-alias implementation targets the SAS UML
configuration:

.. code-block:: text

   CONFIG_MMU=n
   CONFIG_UML_NOMMU_SAS=y
   CONFIG_NOMMU_SWMMU=y

SWMMU itself is only available when ``CONFIG_MMU=n``. The combination of
``CONFIG_MMU=y`` and ``CONFIG_NOMMU_SWMMU=y`` is therefore not supported by
Kconfig and is not a target configuration.

The non-SAS NOMMU UML configuration is deferred:

.. code-block:: text

   CONFIG_MMU=n
   CONFIG_UML_NOMMU_SAS=n
   CONFIG_NOMMU_SWMMU=y

In non-SAS mode, userspace executes in a separate UML runner process. Host
aliases for SWMMU ranges must therefore be synchronized into that runner
through the existing ``current_mm_sync()`` and userspace-runner mapping path.
The current Option-A implementation maps aliases in the SAS host address
space only.

For SAS mode, host aliases are active mappings for the currently running
``mm_struct``. They are not permanent mappings for every SWMMU address space.
When the current task changes, the active host aliases must be replaced with
the aliases belonging to the new ``mm_struct``.

Testing
-------

SWMMU UML testing uses the NOMMU selftests in
``tools/testing/selftests/mm/nommu``. Build the kernel with the SAS and SWMMU
options enabled, then build the GCC plugin and install the tests into a
separate output directory. The host compiler must have GCC plugin headers
installed for its GCC version.

For example, configure the kernel output directory with:

.. code-block:: sh

   make ARCH=um O=build defconfig
   scripts/config --file build/.config --disable MMU \
           --enable NOMMU_SWMMU \
           --enable GCC_PLUGINS \
           --enable GCC_PLUGIN_SWMMU \
           --enable NOMMU_SWMMU_KUNIT_TEST \
           --enable NOMMU_SWMMU_DEFAULT_ON \
           --enable UML_NOMMU_SAS \
           --enable BINFMT_ELF_FDPIC
   make ARCH=um O=build -j32

Build the selftest plugin and install the selftests with:

.. code-block:: sh

   make -C tools/testing/selftests/mm/nommu \
           SWMMU_BUILD_PLUGIN=1 SWMMU_HOSTCXX=g++ \
           SWMMU_PLUGIN="$PWD/build-kselftest/swmmu_plugin.so" \
           swmmu-plugin
   make ARCH=um NOMMU=1 O=build-kselftest \
           SWMMU_ALL_ACCESS=1 \
           SWMMU_PLUGIN="$PWD/build-kselftest/swmmu_plugin.so" \
           TARGETS=mm/nommu kselftest-all kselftest-install

The installed tests are under
``build-kselftest/kselftest/kselftest_install``. Copy the installed tree into
the test root filesystem used by the UML init script. Keep this output
directory separate from the kernel build directory so each build can be
cleaned independently.

Run the resulting UML kernel with a root filesystem containing the installed
tests. For example, when the root filesystem is at ``rootfs``:

.. code-block:: sh

   ./build/vmlinux root=/dev/root rootflags="$PWD/rootfs" \
           rootfstype=hostfs rw mem=2g loglevel=8 zpoline=1 init=/sbin/init

The root filesystem's init script is responsible for invoking the installed
kselftests. The configuration above enables the SWMMU KUnit test as well;
KUnit results are reported during kernel boot. Tests that rely on unsupported
occupied-target ``MREMAP_FIXED`` behavior are currently skipped as described
above.
