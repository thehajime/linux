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
