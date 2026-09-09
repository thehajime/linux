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
