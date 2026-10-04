# SWMMU Project Handoff

This file is the current working status and guidance for SWMMU/NOMMU changes
in this repository. Keep it concise and update it when a milestone changes.
It replaces the former `Documentation/mm/swmmu-status.md`; testing commands
and user-facing SWMMU documentation remain in `Documentation/mm/swmmu.rst`
and `Documentation/mm/swmmu-kselftest-build.rst`.

## Repository and source of truth

- This checkout is `linux-um-nommu`, a Git submodule in its parent checkout.
  Perform repository operations from this directory. The active development
  branch is `feature-swmmu-nommu`; the local base branch was
  `zpoline-nommu-v6.10`.
- Use current tracked source and this file as the source of truth. Older chat
  excerpts, historical appendices, and experimental branches can be stale.
- Leave unrelated untracked files alone. Do not add them or change
  `.gitignore` unless specifically asked.
- Keep changes upstreamable: prefer small NOMMU/SWMMU-specific changes and
  reuse common MM infrastructure where it fits. Avoid broad MMU behavior
  changes solely to simplify SWMMU.

## Current design and implementation

The correctness baseline is SVM-style all-access instrumentation:

- Pointers retain their normal raw representation.
- The compiler instruments supported source-level accesses; runtime helpers
  translate addresses dynamically.
- Pointer provenance, allocator-specific propagation, native-access
  selection, and selective pointer-state analysis are not correctness
  requirements. Treat older selective-provenance and mallocng-specialization
  work as archived experiments unless deliberately revisited as optimization.
- The implementation is an experimental paged SWMMU profile for UML with
  `CONFIG_MMU=n`, `CONFIG_NOMMU_SWMMU=y`, and currently SAS enabled.

The recorded bring-up baseline has passed KUnit SWMMU suites, compiler-plugin
tests, and repeated UML NOMMU kselftest runs, plus Bash smoke tests and a short
lmbench smoke. Those results apply to the reported configurations and should
not be generalized to all binaries, modes, or multi-threaded programs. The
current branch has also had cleanup work for VMA clone preparation, transaction
state, resize locking, alias teardown, and plugin source organization.

SAS host aliases represent only the currently active SWMMU `mm_struct` in the
shared UML host address space. Switching tasks/mm instances must switch aliases;
updates to inactive mms must defer fixed alias operations. SWMMU virtual ranges
are represented in `mm->mm_mt`, with page backing held by SWMMU page tables.
The SAS physical-memory mapping and aliases must share backing storage. Do not
confuse these guest ranges with UML kernel physical memory or
`uml_to_phys()`/`uml_to_virt()`.

## Active cleanup direction

The current work is source cleanup before adding features. The VMA clone
transaction cleanup is complete, and the transaction callback prepare/commit/
abort families have no clear common abstraction worth introducing now. Keep
SWMMU-specific pagetable and rollback behavior isolated. For future duplicate
VMA operations, look for narrowly reusable setup in common MM code before
changing generic MMU paths.

The GCC plugin source was reorganized by declarations, definitions, helpers,
rewriters, and init/exit code. Further plugin cleanup should preserve behavior
and reduce patch surface; avoid a wholesale rewrite.

## Cleanup milestone ledger

- [x] Factor common VMA setup from SWMMU VMA cloning while keeping pagetable
  preparation and rollback SWMMU-specific.
- [x] Review split, replace, expand, and move transaction callbacks; no clear
  shared abstraction was found, so keep the callback families separate.
- [x] Trim unused SWMMU transaction state and fix resize-range release locking.
- [x] Invalidate SWMMU aliases when an mm is released.
- [x] Reorganize the SWMMU GCC plugin source without intending behavior changes.
- [x] Document a reproducible local build for a fresh SWMMU kselftest tree.
- [ ] Build and run a fresh kselftest tree from this workflow and record exact
  TAP totals for the tested mode.
- [ ] Continue cleanup in small, reviewable changes before adding features.

## Completed development history

The former `Documentation/mm/swmmu-status.md` recorded the following
completed milestones. This condensed history keeps the checked-item record
without carrying forward its superseded plans and experimental claims.

- [x] Establish the SVM-style all-access compiler/runtime model as the
  correctness baseline, with raw pointers and runtime address translation.
- [x] Cover scalar, aggregate, array, alias, cast, PHI, cross-function, and
  unknown-return accesses in the compiler plugin, including memory builtins
  and the tested dynamic bitfield and atomic cases.
- [x] Bring up default-on paged SWMMU startup, including the initial exec
  stack, argument/environment setup, and FDPIC stack relocation.
- [x] Implement SAS host aliases for active SWMMU mappings and switch them
  across active mm changes and fork; preserve private-page behavior.
- [x] Add SWMMU-aware copy-to-user and `clear_user()` paths for supported
  destinations, and full-VMA `mprotect()` alias permission updates.
- [x] Route supported private-copy FDPIC executable mappings through SWMMU
  and support instruction fetch from SAS executable aliases.
- [x] Validate instrumented musl and loader startup, BusyBox startup through
  `rcS`, focused TLS/signal-return and vfork/exec/waitpid probes, and default-on
  KUnit SWMMU suites in their reported configurations.
- [x] Restore the normal x86 syscall rewind for the tested restart path;
  `test_signal_restart` passed after reverting the NOMMU SAS no-op override.
- [x] Complete repeated kselftest runs for the reported single-threaded lazy
  SIGSYS zpoline profile and record the intermittent fresh-build parser panic
  as postponed investigation (not as a resolved defect).
- [x] Run Bash smoke tests and a short lmbench smoke; retain their profile and
  coverage limits as stated in this handoff.
- [x] Consolidate the former status document into this `AGENTS.md` handoff and
  document the fresh local kselftest build workflow.

## Current limitations and postponed work

These remain known boundaries; do not imply they are supported just because
the focused tests pass:

- **Intermittent fresh-kselftest parser panic:** a panic was observed while
  running the host-side `kunit_parse.py` path in a fresh test build. It was not
  consistently reproducible and was postponed. If it recurs, capture the full
  boot log and retry under GDB; record the failing task, `mm`, fault address,
  instruction pointer, and zpoline/seccomp mode. The suspicion that an
  uninstrumented host-side Python executable is involved is unconfirmed.
- **Lazy zpoline patching:** the eager executable scan is disabled. The
  current prototype patches a syscall instruction on first SIGSYS using the
  host-reported continuation address minus two bytes. This has been tested in
  the reported single-threaded scope only. The two-byte rewrite is not safe
  against concurrent execution or instruction fetch by another thread; safe
  multi-thread patching or a fallback policy is postponed.
- **Signals and process startup:** focused TLS/signal and vfork/exec/waitpid
  probes pass in their tested profiles, and the syscall restart regression
  was fixed by restoring the normal x86 two-byte rewind. Full signal-frame,
  `sigreturn()`, restart, pipeline, and `/sbin/init` lifecycle behavior is not
  established by those probes.
- **Userspace scope:** paged mode requires the dynamic loader, libc, programs,
  and relevant libraries to be built with the SWMMU profile. An uninstrumented
  `id` success was incidental, not a compatibility guarantee. The planned
  broader instrumented Alpine package profile lives in the `aports`
  `nommu-uml` branch; coreutils is next, nginx later.
- **Mapping/runtime coverage:** direct executable file mappings, general
  file-backed and `MAP_SHARED` mappings, device mappings, partial-range
  `mprotect()`, complete executable permission propagation, raw copy-from-user,
  stack/spill/reload behavior, TLS/context switching, atomics, volatile,
  vector/floating-point, inline assembly, and uninstrumented-library behavior
  remain incomplete or profile-specific. Check implementation and tests before
  changing any item from postponed to supported.
- **Modes/architectures:** non-SAS UML runner alias synchronization, a final
  explicit legacy/flat/paged mode ABI, flat mode, mixed native/SWMMU VMA fork,
  ARM/ARM64, and broad application validation are deferred. The provisional
  `PR_SET_SWMMU`/`PR_GET_SWMMU` interface is not the final mode ABI.
- **Remap coverage:** occupied-target `MREMAP_FIXED` transactional behavior
  remains skipped/deferred. Expansion rollback coverage using existing
  fault-injection facilities is also a follow-up; do not add production-only
  fault hooks.
- **Performance:** defer analysis of the previously high `lat_proc shell`
  number and the SAS-seccomp slowdown until functional validation is stable.
  The reported seccomp/zpoline timing difference under GDB is not a reliable
  outside-GDB performance result.
- **SAS documentation:** continue documenting UML physical memory,
  `physmem_fd`, `page_address()`, SWMMU page backing, active aliases, and the
  distinction between SAS and the separate non-SAS runner when implementation
  work clarifies those details.

## Additional deferred work from the former status document

The former status document had a longer TODO ledger. The entries below retain
items that were not already captured above. Entries superseded by the current
all-access design are identified as optimization work; do not treat them as
requirements for the correctness baseline.

- [ ] **Compiler and runtime optimizations:** preserve pointer-state analysis
  as an optional optimization; consider explicit contracts only at opaque
  library boundaries, interprocedural summaries where they improve coverage or
  performance, and optional LTO support. Measure native-access and generic
  translation tradeoffs before implementing optimization layers.
- [ ] **Access boundaries:** define exception and interrupt interaction;
  finish the kernel user-copy architecture; cover remaining aggregate and
  bitfield read-modify-write patterns. These complement the access types
  already listed above.
- [ ] **Nonlocal control flow:** complete `setjmp()`/`longjmp()` integration
  and `sigsetjmp()`/`siglongjmp()` integration for SWMMU-instrumented userspace.
- [ ] **Allocator and userspace profile:** maintain a reproducible Alpine
  SWMMU package/rootfs profile and rebuild musl, relevant libraries, Bash,
  coreutils, nginx, OpenSSL, and application dependencies with the plugin as
  needed. Validate the custom musl package and these applications; do not
  revive mallocng-specific propagation heuristics as correctness machinery.
- [ ] **Allocation ABI audit:** determine whether provisional
  `nommu_swmmu_alloc()` / `nommu_swmmu_free()` interfaces remain in the current
  source. If present and still provisional, remove their syscall numbers,
  declarations, wrappers, and plumbing in favor of standard
  `mmap()`/`munmap()`/`mremap()`; keep compiler allocator contracts separate
  from allocation syscalls.
- [ ] **Mode and flat-memory ABI:** design explicit legacy, flat, and paged
  mode values and their compatibility relationship to the provisional
  `PR_SET_SWMMU`/`PR_GET_SWMMU` interface. Reassess `MAP_NOMMU_SWMMU_FLAT`
  after the mode design and allocator profile are settled; retain or remove
  it based on demonstrated need. Runtime OFF/ON switching remains postponed.
- [ ] **UML signal audit:** verify NOMMU SAS object selection and user-context
  detection/propagation; audit signal relay paths and validate SIGABRT,
  SIGSEGV, and SIGBUS delivery and default termination with focused regression
  coverage. Keep UML signal/context work separate from the SWMMU fault ABI.
- [ ] **VMA/remap follow-ups:** add expansion rollback coverage using existing
  FAILSLAB or `fail_function` facilities where applicable; do not add a
  production-only test hook. Revisit `MREMAP_DONTUNMAP`, and validate the
  `CONFIG_MMU=y` path so pre-existing MMU failures can be distinguished from
  SWMMU regressions. Occupied-target `MREMAP_FIXED` remains covered above.
- [ ] **Platform and compatibility scope:** evaluate ARM and ARM64 only after
  x86_64 validation; vanilla binaries remain a legacy/ordinary NOMMU concern,
  not a paged SWMMU compatibility promise. Avoid requiring manually maintained
  per-application `-swmmu` packages if a complete profile can be built.

## Validation workflow

Do not add or run tests unless requested. When the user asks for implementation
verification, build with the already-configured output tree when possible. The
full configuration used for a fresh kernel output is:

```sh
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
```

For a disposable, fresh kselftest build, use a separate output directory and
plugin as documented in `Documentation/mm/swmmu-kselftest-build.rst`. The
reported UML invocation is:

```sh
./build/vmlinux root=/dev/root rootflags="$PWD/rootfs" \
        rootfstype=hostfs rw mem=2g loglevel=8 zpoline=1 init=/sbin/init
```

Use `zpoline=0` to exercise SAS-seccomp mode. Keep exact KUnit and kselftest
TAP totals and the tested mode/configuration when reporting results. A boot or
smoke test is not equivalent to the full kselftest suite.

## Updating this handoff

- Mark work done only when source and relevant validation support the claim.
- Keep active next steps separate from intentionally postponed items.
- Record intermittent failures as intermittent, with reproduction evidence;
  do not turn hypotheses into facts.
- Preserve upstream reviewability: small changes, kernel style, tests for
  behavior changes, and no speculative scaffolding.


---

## Historical postponed TODO inventory (pre-consolidation snapshot)

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

