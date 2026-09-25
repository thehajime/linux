# SWMMU Development Status

This document records the current development direction and milestone
boundaries for the NOMMU software MMU work. The active sections below are
authoritative; the historical appendix is retained for context only.

## Current direction

The SVM-style all-access compiler/runtime model is the correctness baseline:

- pointers retain their normal raw representation;
- supported source-level accesses are compiler-instrumented;
- runtime helpers translate addresses dynamically;
- pointer provenance is not required for correctness;
- selective pointer-state analysis and native-access selection are postponed
  optimizations.

The current selective provenance implementation, allocator-specific
propagation, free/realloc specialization, and cgraph-clone experiments are
archived experiments. They must not be extended as the correctness design.

## Current implementation checkpoint

The compiler all-access baseline covers:

- [x] scalar loads and stores;
- [x] structure fields;
- [x] arrays and pointer arithmetic;
- [x] aliases and pointer casts;
- [x] PHI joins;
- [x] cross-function accesses;
- [x] unknown pointer returns;
- [x] explicit and builtin `memcpy()`, `memmove()`, and `memset()`;
- [x] aggregate copies;
- [x] nested aggregate stores;
- [x] dynamic 32-bit compare-exchange needed by mallocng;
- [x] mallocng metadata bitfield analysis;
- [x] dynamic bitfield loads and stores;
- [x] compiler-generated mallocng metadata access lowering;
- [x] tested direct global loads and stores;
- [x] tested global pointer loads and stores;
- [x] tested const-hack-shaped pointer accesses;
- [x] TLS `%fs:0` pointer-state handling in the focused compiler/runtime test.

The default-on UML bring-up has reached:

- [x] default-on paged SWMMU process startup;
- [x] SWMMU-backed initial exec-stack allocation for SAS UML;
- [x] exec argument, environment, and auxiliary-vector setup on the
  SWMMU-backed stack;
- [x] FDPIC exec-stack setup and relocation using SWMMU virtual addresses;
- [x] host-visible aliases for SAS SWMMU stack and data mappings;
- [x] CPU-generated stack accesses including `push`, `pop`, `call`, and `ret`;
- [x] SAS active-`mm_struct` host-alias switching;
- [x] SAS parent/child host-alias switching after `fork()`;
- [x] SAS private-page behavior across `fork()`;
- [x] SWMMU-aware kernel-to-userspace copy-to paths;
- [x] SWMMU-aware `clear_user()` for SWMMU-backed destinations;
- [x] SWMMU-aware host-alias permission updates for full-VMA `mprotect()`;
- [x] private-copy FDPIC executable mappings through the SWMMU backend;
- [x] instruction fetch from SAS SWMMU executable mappings;
- [x] dynamic atomic compare-exchange needed by mallocng;
- [x] mallocng metadata, bitfield, aggregate, and atomic access lowering;
- [x] instrumented musl and dynamic loader startup under the current
  bring-up profile;
- [x] instrumented BusyBox startup through `rcS` under the current
  bring-up profile;
- [x] focused vfork/exec/waitpid probe;
- [x] focused TLS and signal-return probe;
- [x] default-on KUnit SWMMU suites.

Current validation and limitations:

- KUnit currently passes in the reported configuration.
- The GCC plugin’s all-access and builtin-memory compiler tests pass.
- kselftest is currently broken and must be restored to a stable baseline
  before using it as a reliable regression signal.
- `/sbin/init` has reached a usable shell in zpoline mode after the recent
  alias and `clear_user()` changes.
- Intermittent all-access faults remain. They are not yet cleanly
  reproducible and must not be considered fixed based on one successful run.
- The `/usr/bin/id` symlink currently refers to an uninstrumented coreutils
  binary; this binary is not a valid paged-SWMMU test until its package
  closure is rebuilt with the SWMMU profile.
- The temporary tracing and diagnostic changes are still present in some
  local builds. Remove them before clean performance or stability runs.

The focused probes establish only their individual paths. In particular,
passing the TLS/signal probe does not establish that shell signal handling,
pipelines, and full `/sbin/init` startup are correct.

## Immediate next validation

Restore a dependable baseline before doing more broad debugging:

1. Remove or disable temporary `printk()` calls, trace dumps, and GDB
   breakpoint scripts.
2. Run the KUnit SWMMU suites.
3. Run the compiler plugin regression suite:
   ```text
   make -B -C scripts/gcc-plugins/tests all-access all-access-builtins
   ```
4. Repair the currently broken kselftest and record a stable passing result.
5. Boot the same rebuilt rootfs without GDB and run zpoline mode.
6. Keep the uninstrumented `id` limitation explicit; do not treat its
   behavior as evidence about the instrumented runtime.
7. Record each intermittent failure separately, with its faulting task,
   `mm`, fault address/IP, and mode. Do not merge distinct failures into one
   diagnosis.

The latest zpoline/SAS alias investigation found that inactive-mm mappings
could map fixed host aliases while another mm was recorded as active. The
current implementation defers host-alias installation for inactive VMA
updates and force-maps aliases during explicit mm activation. The latest
successful `/sbin/init` run is encouraging, but intermittent faults mean
this alias-policy change still needs repeatable validation.

## Default-on paged SVM policy

The current default-on kernel profile is an experimental paged-SWMMU profile.

The current SAS bring-up model uses:

- SWMMU-backed initial exec-stack mappings;
- host-visible aliases for active SAS SWMMU ranges;
- private-copy FDPIC executable mappings;
- executable host aliases for instruction fetch;
- active host-alias replacement when the current `mm_struct` changes;
- deferred host-alias mapping for inactive-mm VMA updates;
- forced host-alias installation when an mm is explicitly activated.

Temporary and deferred boundaries:

- executable direct file-backed mappings remain native;
- `MAP_SHARED`, device, and general file-backed SWMMU mappings remain
  unsupported;
- non-SAS NOMMU UML runner integration is deferred;
- mixed native/SWMMU VMA fork support is not a target;
- the copy-to-user SWMMU bridge is retained for relevant destinations;
- raw copy-from-user handling remains limited because raw uaccess is also used
  by kernel-internal nofault helpers;
- `AT_SYSINFO_EHDR` is omitted for SWMMU because the VDSO has no SWMMU mapping.

These are bring-up exceptions, not the final `NOMMU_PAGED` design.

### SAS host-memory model

In SAS mode, the UML kernel and guest userspace execute in one UML host process
and therefore share one host address space.

SWMMU virtual addresses are represented by VMA ranges in `mm->mm_mt`, while
their backing pages are held by the SWMMU page tables. The active SWMMU
`mm_struct` additionally has host aliases installed at the same virtual
addresses so CPU-generated accesses such as `push`, `call`, `ret`, and
instruction fetch can execute directly.

The host aliases are active mappings for the currently running `mm_struct`;
they are not permanent mappings for every guest address space. Parent and child
SWMMU spaces therefore reuse the same host virtual addresses, with the active
aliases replaced during task or `mm_struct` activation. Inactive-mm VMA
updates must not install or remove aliases in this shared host address window.

The SAS physical-memory mapping must use the same file-backed storage as the
SWMMU host aliases. Anonymous host mappings are insufficient because kernel
SWMMU helpers would update `page_address(page)` while CPU accesses would
observe a different anonymous mapping.

The non-SAS NOMMU UML configuration uses a separate userspace runner and
requires a different host-alias synchronization path.

The relevant address domains remain distinct:

```text
UML kernel/native memory:
    uml_physmem, uml_reserved, high_physmem, page_address()

SWMMU backing:
    SWMMU page tables and struct page backing

SWMMU host aliases:
    fixed host virtual addresses for the currently active SWMMU mm
```

The UML kernel physical-memory variables and `uml_to_phys()`/`uml_to_virt()`
continue to describe native UML kernel memory; they are not SWMMU guest
address-space bounds.

### Next kernel milestone: pure paged startup

Remove the remaining temporary native-stack and native-executable
exceptions:

- [x] allocate the initial exec stack through the SWMMU backend;
- [x] copy exec arguments and environment into the SWMMU-backed stack;
- [x] update FDPIC exec-stack setup and relocation;
- [x] implement instruction-fetch support for private-copy executable SWMMU
  mappings in SAS UML;
- [ ] complete executable mapping permissions and `mprotect()` handling;
- [ ] define signal-frame and `sigreturn()` handling for SWMMU-backed state;
- [ ] define stack and compiler-generated spill/reload handling;
- [ ] complete remaining atomic access coverage;
- [ ] define TLS handling;
- [ ] validate the instrumented musl loader, libc, and BusyBox startup with
  both SWMMU-backed stack and executable mappings;
- [ ] replace the minimal probe with `init=/sbin/init`.

The vfork/exec/waitpid and TLS/signal probes pass in their tested
configurations, but they do not establish that the full signal, restart,
pipeline, and init paths are complete.

Until this milestone is complete, direct file-backed executable mappings and
signal/runtime boundaries remain temporary bring-up exceptions.

## Compatibility and mode policy

Mixed native/SWMMU VMAs within one paged process are not a near-term
production target. Do not expand the current temporary mixed-VMA behavior into
a general compatibility design.

The intended future compatibility model is per-process mode selection:

```text
NOMMU_LEGACY
    Existing native NOMMU behavior for legacy binaries.

NOMMU_PAGED
    Fully instrumented userspace with paged SWMMU mappings.

NOMMU_FLAT
    Future flat-memory compatibility or diagnostic mode.
```

Different processes using different modes is more meaningful than requiring a
single paged process to maintain arbitrary native and SWMMU VMA domains.

The current boolean `PR_SET_SWMMU`/`PR_GET_SWMMU` interface remains provisional.
Runtime OFF/ON switching is postponed while the explicit mode ABI is designed.

`vfork()` is not a general compatibility mechanism. It only supports the
usual narrow `vfork()`-then-`execve()` pattern.

## Userspace deployment profile

For paged SVM mode, all relevant userspace code must be rebuilt with the
SWMMU compiler/runtime profile:

- the dynamic loader;
- musl/libc;
- statically linked applications;
- dynamically linked applications;
- libraries;
- BusyBox and init;
- coreutils and startup utilities;
- benchmark programs;
- application dependencies.

Rebuilding only musl is insufficient for dynamically linked applications,
because application code still contains its own source-level memory accesses.

Static binaries must also be rebuilt with both SWMMU-aware musl and the SWMMU
compiler plugin.

Uninstrumented binaries are supported only in the future legacy/native mode,
not generally in paged SVM mode.

The current BusyBox `ptr_to_globals` access has been verified through the
SWMMU syscall path: its initialization store and subsequent loads return the
same nonzero guest pointer for the shell mm. This rules out that particular
global slot as the explanation for the current intermittent faults.

## Current runtime boundaries

The following areas remain incomplete or explicitly experimental:

- [ ] signal-frame construction for SWMMU-backed user stacks;
- [ ] `sigreturn()` restoration of SWMMU-backed register and stack state;
- [ ] signal restart and delivery behavior with SWMMU-backed state;
- [ ] TLS setup and context switching for the instrumented musl loader, libc,
  and init;
- [ ] compiler-generated spills and reloads across loader, libc, and BusyBox
  startup;
- [ ] complete atomic access support;
- [ ] volatile access coverage;
- [ ] vector and floating-point access support;
- [ ] inline-assembly access contracts;
- [ ] uninstrumented library behavior;
- [ ] final kernel user-copy architecture;
- [ ] remaining aggregate and bitfield read-modify-write cases;
- [ ] complete executable permission propagation through all VMA operations;
- [ ] partial-range `mprotect()` support;
- [ ] executable direct file-backed mappings;
- [ ] non-SAS NOMMU UML runner alias synchronization.

The focused TLS/signal test passes, but full shell pipeline and init signal
behavior remains unvalidated. UML/NOMMU signal handling remains a separate
TODO; the sampled seccomp SIGSYS context rewrite preserved the expected
register values, but other signal/restart paths are still under investigation.

The x86_64 `setjmp()`/`longjmp()` implementation has an experimental
SWMMU-aware assembly version, but its musl add-CFI integration and complete
signal-context behavior are not finalized. Do not treat it as complete.

## Validation order

The current validation order is:

1. KUnit SWMMU suites;
2. selective compiler regression suite;
3. all-access compiler regression suite;
4. SAS default-on UML kselftest with mixed-mode tests skipped;
5. minimal instrumented musl, loader, and BusyBox startup;
6. SAS host-alias stack probe;
7. SAS host-alias syscall probe;
8. SAS host-alias mmap, mprotect, fork, and munmap lifecycle probe;
9. SAS executable SWMMU instruction-fetch probe;
10. SWMMU signal-frame and `sigreturn()` tests;
11. TLS and dynamic-loader validation;
12. non-fork Bash startup and loop tests;
13. vfork/exec-oriented lmbench smoke tests;
14. rebuilt coreutils package closure;
15. coreutils command validation;
16. `init=/sbin/init`;
17. non-SAS NOMMU UML runner validation;
18. broader Bash, lmbench, and application validation.

KUnit currently works in the reported setup. The kselftest suite is currently
broken; restore it to a stable, repeatable baseline before relying on it as a
regression signal.

The host-side KUnit parser and full `run_kselftest.sh` dependency closure are
postponed until Python and required coreutils binaries are rebuilt with the
same all-access profile.

## Postponed work

The following items are intentionally outside the current SAS host-alias and
private-copy executable-mapping work.

### Bring-up gates before `/sbin/init`

These are required before treating `init=/sbin/init` as a validated baseline:

- [ ] signal-frame construction for SWMMU-backed user stacks;
- [ ] `sigreturn()` restoration of SWMMU-backed register and stack state;
- [ ] signal restart behavior with the new executable and stack mappings;
- [ ] TLS setup and context switching for the instrumented musl loader, libc,
  and init;
- [ ] validation of compiler-generated stack spills and reloads across loader,
  libc, and BusyBox startup;
- [ ] completion of atomic paths used during init and libc startup;
- [ ] re-run instrumented musl, dynamic-loader, and BusyBox startup with
  SWMMU-backed executable mappings;
- [ ] verify whether the current musl profile requires `brk()` and either
  implement paged-mode `brk()` support or keep the affected allocator
  configuration disabled;
- [ ] validate `vfork()`/`execve()` and child cleanup with active-`mm` host
  alias switching;
- [ ] validate the `/sbin/init` process lifecycle, including signal handling,
  child reaping, and termination/restart behavior;
- [ ] restore a stable kselftest baseline and use it to verify the alias
  activation/defer policy.

### Deferred after initial `/sbin/init` bring-up

- [ ] non-SAS NOMMU UML runner integration:
  `CONFIG_MMU=n` and `CONFIG_UML_NOMMU_SAS=n`;
- [ ] direct file-backed and `MAP_SHARED` SWMMU mappings;
- [ ] device mappings;
- [ ] final executable-permission and instruction-fetch ABI;
- [ ] complete `setjmp()`/`longjmp()` integration;
- [ ] complete `sigsetjmp()`/`siglongjmp()` integration;
- [ ] volatile access coverage;
- [ ] vector and floating-point access coverage;
- [ ] inline-assembly access contracts;
- [ ] final SWMMU mode ABI;
- [ ] `brk()` integration if not required by the initial musl profile;
- [ ] flat-memory compatibility mode;
- [ ] mixed native/SWMMU VMA fork support;
- [ ] selective pointer-state optimization;
- [ ] native-access optimization;
- [ ] mallocng-specific propagation heuristics;
- [ ] allocator-specific compiler contracts;
- [ ] global `free()`/`realloc()` specialization;
- [ ] broad application validation;
- [ ] architecture-independent `setjmp()`/`longjmp()` support;
- [ ] ARM and ARM64 support.

The seccomp-mode slowdown is currently postponed. Outside GDB, the measured
`true | true` case was approximately 2.06 seconds under SAS-seccomp and 0.49
seconds under zpoline. Under GDB, seccomp showed much larger slowdown; this has
not yet been demonstrated outside GDB and is not the current correctness
blocker.

### SAS memory-model documentation

- [ ] document the relationship between UML physical memory,
  `physmem_fd`, and `page_address(page)`;
- [ ] document the difference between anonymous and file-backed host mappings;
- [ ] document how SWMMU virtual addresses are represented in `mm->mm_mt`;
- [ ] document how SWMMU page backing is shared with host aliases;
- [ ] document active host-alias switching across parent and child
  `mm_struct` instances;
- [ ] document why host aliases do not imply guest `MAP_SHARED` semantics;
- [ ] document the distinction between SAS and the non-SAS UML runner model;
- [ ] document which UML host-memory APIs may be used during kernel-to-host
  mappings and which are unsafe during userspace-entry transitions;
- [ ] document that inactive-mm VMA updates defer fixed host-alias operations,
  while explicit mm activation force-installs aliases.

## Source-of-truth rules

- The current pushed PR source is authoritative.
- Older chat snippets and experimental branches are not authoritative.
- Selective provenance and allocator-specific changes remain experimental.
- Do not add application-owned SWMMU annotations as a substitute for the SVM
  baseline.
- Update this document at meaningful implementation milestones.
- Keep the historical appendix below this section unchanged unless correcting
  historical facts.


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
