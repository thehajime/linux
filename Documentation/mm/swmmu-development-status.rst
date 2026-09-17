==========================
SWMMU Development Status
==========================

This document records the current development direction and milestone
boundaries for the NOMMU software MMU work.  It is intentionally concise and
should be updated at meaningful implementation milestones rather than during
every debugging iteration.

Current direction
=================

The SVM-style all-access compiler model is the correctness baseline:

* pointers retain their normal raw representation;
* supported source-level accesses are compiler-instrumented;
* runtime helpers translate addresses dynamically;
* pointer provenance is not required to decide whether an access is safe.

Selective pointer-state analysis and native-access selection are postponed
optimizations.  The current selective provenance implementation and the
mallocng-specific propagation experiments are retained only as experiments.


Current implementation checkpoint
==================================

The compiler-only all-access baseline now covers:

* [x] scalar loads and stores;
* [x] fields, arrays, aliases, and pointer arithmetic;
* [x] cross-function and unknown-pointer accesses;
* [x] builtin and explicit memory intrinsics;
* [x] aggregate copies;
* [x] SWMMU runtime tests for aggregate copies.

Default-on UML bring-up has reached:

* [x] native initial exec stack handling;
* [x] SWMMU-aware ``mprotect()``;
* [x] SWMMU-aware kernel user-copy paths;
* [x] mallocng metadata atomics needed by startup;
* [x] mallocng bitfield and aggregate access lowering;
* [x] instrumented musl, loader, and BusyBox startup through ``rcS``.

Current blocker:

* [ ] define and implement ``setjmp()``/``longjmp()`` handling for SWMMU
  buffers used by BusyBox;
* [ ] complete remaining atomic and bitfield RMW coverage;
* [ ] remove bring-up diagnostics and temporary all-access allocator
  workarounds.

The selective pointer-state and mallocng-specific work remains experimental
and should not be extended as the correctness design.

Current implementation checkpoint
==================================

The compiler-only all-access baseline now covers:

* [x] scalar loads and stores;
* [x] structure fields, arrays, aliases, and pointer arithmetic;
* [x] pointer casts, PHI joins, and cross-function calls;
* [x] unknown pointer returns in all-access functions;
* [x] explicit and builtin memory intrinsics;
* [x] aggregate copies;
* [x] nested aggregate copies in instrumented BusyBox code.

Default-on UML bring-up has reached:

* [x] native initial exec stack handling;
* [x] SWMMU-aware ``mprotect()``;
* [x] SWMMU-aware kernel user-copy paths;
* [x] dynamic 32-bit compare-exchange needed by mallocng;
* [x] mallocng metadata, bitfield, and aggregate access lowering;
* [x] instrumented musl, loader, and BusyBox startup through ``rcS``;
* [x] profile-aware kselftest with default-on mode tests skipped.

Current next milestone
=====================

* [ ] rebuild coreutils with the all-access profile;
* [ ] make ``setjmp()``/``longjmp()`` work for SWMMU-backed context buffers;
* [ ] complete remaining atomic and bitfield read-modify-write coverage;
* [ ] remove bring-up diagnostics and temporary allocator workarounds.

Mixed-mode transition tests remain intentionally skipped in the default-on
profile.  They are retained as future legacy/mixed-mode coverage.

Temporary runtime policy
========================

For the first SVM experiment, paged SWMMU may be enabled before userspace
startup for a complete instrumented root filesystem.  This is an experimental
deployment policy, not the final NOMMU ABI.

In this mode, all userspace components that perform relevant accesses must be
rebuilt with the SWMMU compiler/runtime profile, including:

* the dynamic loader;
* musl;
* statically linked musl applications;
* dynamically linked applications;
* libraries and utilities used during startup.

Rebuilding only musl is not sufficient for dynamically linked applications:
application code still contains its own source-level memory accesses.

Uninstrumented static or dynamic binaries are not generally supported in
paged SWMMU mode.  ``vfork()`` is not a compatibility mechanism; it only
supports the usual narrow vfork-then-exec usage pattern.

The eventual mode model is expected to distinguish:

``NOMMU_LEGACY``
  Existing native NOMMU behavior for legacy binaries.

``NOMMU_PAGED``
  SVM-instrumented userspace with software-paged SWMMU mappings.

``NOMMU_FLAT``
  A future flat-memory compatibility or diagnostic mode.

The current boolean SWMMU ON/OFF interface is provisional while this mode ABI
is redesigned.

Runtime access contract
=======================

The generic dynamic access path must support:

* SWMMU-managed virtual addresses;
* ordinary native addresses where the runtime policy permits them;
* checked error reporting;
* signal-mode access reporting.

The compiler/runtime design must explicitly define the treatment of accesses
that are not yet covered by the baseline, including:

* stack accesses;
* compiler-generated spills and reloads;
* TLS;
* signal frames;
* atomics;
* volatile accesses;
* vector and floating-point accesses;
* inline assembly;
* remaining atomic operations;
* ``setjmp()``/``longjmp()`` context buffers.

These limitations must not be silently treated as instrumented.

Postponed work
==============

The following work remains postponed while the default-on SVM profile and
its unsupported access boundaries are being stabilized:

* selective pointer-state optimization;
* native-access optimization;
* mallocng-specific field propagation;
* allocator-specific compiler contracts;
* ``free()`` and ``realloc()`` specialization;
* cgraph clone optimization for mixed pointer domains;
* application-owned SWMMU annotations;
* flat-memory compatibility implementation;
* final ``PR_SET_SWMMU``/``PR_GET_SWMMU`` mode ABI;
* broad application validation.

Source-of-truth rules
=====================

* The current source tree and pushed PR tip are authoritative.
* Older chat snippets and experimental userspace annotations are not
  authoritative.
* Experimental selective-provenance branches should remain available for
  comparison but should not drive the correctness design.
* Update this document together with meaningful implementation milestone
  commits.

Appendix A: Detailed project history
====================================

The following section preserves the previous project handoff and detailed
planning history.  It is retained as historical context; the "Current
direction" section above takes precedence where the two differ.

# SWMMU/NOMMU Project Handoff

## Project goal

Implement a per-process software MMU for NOMMU Linux while reusing common VMA and remap infrastructure.

The long-term deployment goal is:

- rebuild musl/libc, libraries, and applications with the SWMMU GCC plugin;
- avoid application-source annotations wherever possible;
- preserve the normal raw pointer representation;
- support existing applications such as `bash` and `nginx`;
- use software paging and runtime translation for correctness;
- use pointer-state analysis later as an optimization.

## Current preferred design direction

The paper discussed in this session uses an SVM-style design:

```text
pointers remain virtual addresses
compiler instruments memory accesses
runtime translates virtual addresses before access
```

The preferred first usable SWMMU mode is now:

```text
SVM paged mode:
    instrument supported non-stack memory accesses
    translate accesses dynamically
    do not require application-owned structure annotations

legacy/flat mode:
    native accesses
    compatibility behavior
```

Pointer-state tracking remains useful as an optimization:

```text
known ordinary access -> native access
known static SWMMU access -> specialized helper
known dynamic access -> dynamic helper
unknown access -> generic SVM translation
```

The important change from the earlier design is:

```text
unknown pointer:
    baseline SVM -> dynamically translate
    optimized mode -> optionally diagnose or fall back
```

## Latest validation status

### Kernel and SWMMU tests

- [x] KUnit MM-backed suite:
  - 21 passed.
  - 1 expected skip for occupied-target `MREMAP_FIXED`.
- [x] KUnit no-MM suite:
  - 6 passed.
- [x] Latest full Alpine/musl NOMMU UML kselftest:
  - 43 tests passed.
- [x] Dynamic ordinary/SWMMU access test.
- [x] `memcpy()` runtime test.
- [x] Overlapping `memmove()` runtime test.
- [x] Multi-page `memset()` runtime test.
- [x] `malloc()`/`free()` domain test.
- [x] Signal delivery, restart, and `siglongjmp()` tests.
- [x] Fork, mremap, munmap, MAP_FIXED, and exit-cleanup tests.

The expected skipped test remains:

```text
occupied-target MREMAP_FIXED
```

### Compiler plugin tests

- [x] Ordinary pointer accesses remain native in the current selective mode.
- [x] Static `swmmu_ptr` loads and stores.
- [x] Pointer aliases.
- [x] Pointer arithmetic and indexing.
- [x] Pointer casts.
- [x] Typed pointer returns.
- [x] Structures and pointer arrays.
- [x] SSA pointer-state tracking.
- [x] PHI joins.
- [x] Mixed ordinary/SWMMU PHI diagnostics.
- [x] Unknown pointer-return diagnostics.
- [x] Unannotated boundary diagnostics.
- [x] `swmmu_memop("memcpy")`.
- [x] `swmmu_memop("memmove")`.
- [x] `swmmu_memop("memset")`.
- [x] Compiler lowering for `memcpy()`.
- [x] Compiler lowering for `memmove()`.
- [x] Compiler lowering for `memset()`.
- [x] Dynamic load/store lowering.
- [x] Automatic recognition of `malloc()`.
- [x] Automatic recognition of `calloc()`.
- [x] Automatic recognition of `realloc()`.
- [x] Automatic recognition of `mmap()`/`mmap64()`/`__mmap()`/`__mmap64()`.
- [x] Dynamic pointer aliases.
- [x] Dynamic/ordinary PHI handling.
- [x] Dynamic pointer field tests.
- [x] Dynamic nested-field tests.
- [x] Positive and negative compile-only test harness.

## Current allocator status

The target userspace environment is:

```text
Alpine Linux 3.20.3
musl 1.2.5-r0
custom Alpine NOMMU patches
custom NOMMU busybox and musl packages
x86_64 NOMMU UML
```

The musl build uses:

```text
-DDL_NOMMU_SUPPORT=1
-fplugin=swmmu-plugin-alpine-gcc13.so
```

The current experimental mallocng integration showed:

- [x] Runtime SWMMU helper declarations are linkable from musl.
- [x] Dynamic runtime symbols are available in musl.
- [x] Some mallocng metadata stores are lowered.
- [x] Some mallocng dynamic loads/stores are generated.
- [ ] Full mallocng execution is not yet stable.
- [ ] `realloc()` behavior remains unresolved.
- [ ] All mallocng pointer paths are not yet covered.
- [ ] The final requirement for musl-internal annotations is not decided.

Important observed issue:

```text
mallocng accesses SWMMU-backed storage through internal metadata and
flexible-array paths that are difficult to infer with the current
per-function pointer-state implementation.
```

The following musl annotations were experimental and must not be treated as the final design:

```text
swmmu_dynamic_ptr on mallocng fields
swmmu_allocator_result on enframe()
```

The intended final design should avoid requiring similar annotations in every application-owned structure.

## Important design decision

Do not continue adding mallocng-specific annotations until the compiler model is clarified.

The compiler must eventually handle patterns such as:

```c
struct holder {
        void *pointer;
};

void store(struct holder *holder)
{
        holder->pointer = mmap(...);
}

void use(struct holder *holder)
{
        *(uint64_t *)holder->pointer = value;
}
```

without requiring application-owned structure annotations wherever possible.

The current compiler can handle some same-function cases, but cross-function pointer-field propagation remains unresolved.

## Next active milestone

### Implement an SVM-style all-access compiler baseline

Before continuing musl allocator integration:

- [ ] Add compiler-only tests for an all-access translation mode.
- [ ] Instrument ordinary source-level data loads and stores through a generic translation runtime.
- [ ] Avoid requiring pointer provenance to decide whether the access is safe.
- [ ] Keep ordinary native accesses as a later optimization, not as the correctness baseline.
- [ ] Define the generic runtime translation interface.
- [ ] Determine which accesses remain outside the baseline:
  - [ ] stack accesses;
  - [ ] compiler-generated spills and reloads;
  - [ ] inline assembly;
  - [ ] signal frames;
  - [ ] TLS;
  - [ ] atomics;
  - [ ] vector and floating-point accesses.
- [ ] Add compile-only tests for ordinary pointers, structure fields, arrays, aliases, and cross-function calls.
- [ ] Compare the SVM baseline against the current selective pointer-state implementation.
- [ ] Revisit mallocng only after the SVM baseline can instrument its internal accesses.

## Postponed TODOs

### Compiler/runtime access model

- [ ] Define the generic SVM translation runtime.
- [ ] Use generic translation as the correctness fallback for unknown pointers.
- [ ] Preserve pointer-state analysis as an optimization layer.
- [ ] Use explicit API contracts only for opaque or special library boundaries.
- [ ] Add interprocedural summaries where they improve optimization or coverage.
- [ ] Evaluate optional LTO support.
- [ ] Support compiler-generated spills and reloads.
- [ ] Support stack accesses.
- [ ] Decide between ordinary stacks and split-stack support.
- [ ] Support atomics.
- [ ] Support volatile accesses.
- [ ] Support vector and floating-point accesses.
- [ ] Define inline-assembly requirements.
- [ ] Support structure copies.
- [ ] Support pointers stored inside SWMMU memory.
- [ ] Support TLS.
- [ ] Support signal frames.
- [ ] Define `setjmp()`/`longjmp()` interaction.
- [ ] Define exception and interrupt handling.
- [ ] Define uninstrumented-library behavior.
- [ ] Define libc and dependency-library contracts.

### Native-access optimization

- [ ] Use native accesses only as an optimization after the SVM baseline works.
- [ ] Skip translation for accesses proven to target ordinary memory.
- [ ] Use specialized direct helpers for statically known SWMMU accesses.
- [ ] Retain generic dynamic translation for unknown or escaped pointers.
- [ ] Measure the performance tradeoff between full translation and selective native access.

### Alpine/musl mallocng

- [ ] Revisit mallocng after the SVM baseline is available.
- [ ] Remove temporary mallocng-specific annotations where generic analysis replaces them.
- [ ] Define allocator metadata and payload access domains.
- [ ] Make mallocng metadata accesses safe.
- [ ] Make `malloc()` behavior stable with SWMMU enabled.
- [ ] Make `calloc()` behavior stable with SWMMU enabled.
- [ ] Make `realloc(NULL, size)` stable.
- [ ] Support realloc growth.
- [ ] Support realloc shrink.
- [ ] Support realloc movement.
- [ ] Preserve the original allocation after realloc failure.
- [ ] Make `free()` safe for ordinary and SWMMU-backed allocations.
- [ ] Cover `aligned_alloc()`.
- [ ] Cover `posix_memalign()`.
- [ ] Cover `strdup()` and related allocation-returning APIs.
- [ ] Rebuild and validate the custom Alpine musl package.
- [ ] Validate `bash`.
- [ ] Validate `nginx`.
- [ ] Validate OpenSSL and other relevant dependencies.

### Allocation-management ABI

- [ ] Remove provisional `nommu_swmmu_alloc()` syscall.
- [ ] Remove provisional `nommu_swmmu_free()` syscall.
- [ ] Remove their syscall numbers, declarations, wrappers, and plumbing.
- [ ] Use standard `mmap()`, `munmap()`, and `mremap()` for allocation management.
- [ ] Keep compiler allocator contracts independent from allocation syscalls.

### NOMMU mode ABI

- [ ] Define explicit `PR_NOMMU_LEGACY`.
- [ ] Define explicit `PR_NOMMU_FLAT`.
- [ ] Define explicit `PR_NOMMU_PAGED`.
- [ ] Initially preserve:
  ```text
  PR_NOMMU_LEGACY == existing flat NOMMU behavior
  PR_NOMMU_FLAT   == equivalent behavior initially
  PR_NOMMU_PAGED  == current software-paged SWMMU behavior
  ```
- [ ] Preserve `PR_SWMMU_OFF` and `PR_SWMMU_ON` as compatibility aliases while the new ABI is developed.
- [ ] Define the difference between legacy native access and flat checked access.
- [ ] Reuse common VMA/range/lifetime management for the future flat backend.
- [ ] Remove or deprecate the boolean ON/OFF model after the explicit mode ABI is stable.

### `MAP_NOMMU_SWMMU_FLAT`

- [ ] Keep `MAP_NOMMU_SWMMU_FLAT` temporarily as a compatibility and diagnostic feature.
- [ ] Keep its kernel implementation and kselftest coverage while allocator integration is incomplete.
- [ ] Do not use it as the final mallocng design.
- [ ] Reevaluate whether it is still needed after the SVM baseline works.
- [ ] Remove it later if fully instrumented userspace makes it unnecessary.

### UML/NOMMU signal handling

- [ ] Audit UML NOMMU builds with `CONFIG_UML_NOMMU_SAS=y`.
- [ ] Verify object selection for `trap.o`, `trap-nommu.o`, and NOMMU signal handlers.
- [ ] Fix or validate `is_user` detection and propagation.
- [ ] Investigate SIGABRT delivery and default termination.
- [ ] Investigate endless host-signal relay loops.
- [ ] Audit `relay_signal()`.
- [ ] Audit `nommu_relay_signal()`.
- [ ] Audit `set_mc_relay_signal()`.
- [ ] Validate SIGABRT, SIGSEGV, and SIGBUS.
- [ ] Validate signal-frame construction and restoration.
- [ ] Validate `sigreturn()`.
- [ ] Add minimal UML regression tests.
- [ ] Keep this separate from the SWMMU fault ABI.

### VMA/remap

- [ ] Add post-backend-prepare expansion rollback coverage using existing fault-injection facilities.
- [ ] Use `FAILSLAB` or `fail_function` where applicable.
- [ ] Do not add a custom production-only KUnit fault hook.
- [ ] Implement occupied-target `MREMAP_FIXED` transactionally for MMU and SWMMU in a separate patchset.
- [ ] Preserve the destination until source preparation and execution succeed.
- [ ] Share rollback behavior between MMU and SWMMU.
- [ ] Add failure coverage for source preparation, pagetable allocation, page copying, and destination insertion.
- [ ] Revisit `MREMAP_DONTUNMAP`.
- [ ] Restore and continuously validate `CONFIG_MMU`.
- [ ] Separate pre-existing MMU failures from SWMMU regressions.

### Application deployment

- [ ] Define an Alpine SWMMU build profile or repository.
- [ ] Rebuild musl/libc with the SWMMU plugin.
- [ ] Rebuild relevant libraries with the SWMMU plugin.
- [ ] Rebuild `bash` with the SWMMU plugin.
- [ ] Rebuild `nginx` with the SWMMU plugin.
- [ ] Rebuild OpenSSL and other libraries that access application buffers.
- [ ] Validate x86_64 first.
- [ ] Evaluate ARM support.
- [ ] Evaluate ARM64 support.
- [ ] Do not require manually maintained `-swmmu` packages for every application if a complete SWMMU rootfs/profile can be built consistently.
- [ ] Vanilla binaries remain supported only in legacy/ordinary NOMMU mode, not generally in paged SWMMU mode.

## Design principles

- Use the current pushed PR and exact current source as the source of truth.
- Do not assume earlier snippets are still present.
- Ask for the complete relevant current functions before proposing non-trivial changes.
- Prefer one coherent patch over speculative scaffolding.
- Clearly label suggestions:
  - `FINAL`;
  - `EXPERIMENTAL`;
  - `TEST ONLY`;
  - `TODO`.
- Do not present experimental musl annotations as the final application model.
- Avoid requiring application-owned structure annotations.
- Reuse existing VMA and remap infrastructure.
- Keep generic VMA code independent of SWMMU internals.
- Keep pointers ABI-compatible.
- Prefer runtime translation over changing pointer representation.
- Keep generic compiler/runtime behavior architecture-independent where possible.
- Keep UML-specific signal/context handling isolated.
- Do not introduce a second address-space tree.
- Keep code within Linux kernel 80-column style.
- Do not add a new data type when an existing transaction/state object can be reused cleanly.
- Treat uninstrumented prebuilt libraries as unsafe for arbitrary SWMMU-backed pointers.
- Rebuild applications and relevant libraries with the plugin for paged SWMMU deployment.
- Use Copy Markdown for handoff and milestone text.
- Update milestones and postponed TODOs after each completed step.
- Prefer concrete copy/pasteable code snippets over conceptual sketches.
- Keep responses concise and focus on the latest request.
