# Experimental Chrome / V8 compatibility checkpoint

This checkpoint preserves the accumulated work on `node-support` as of
2026-10-27. Its purpose is to make the local development state recoverable and
reviewable. It is not a claim that Chrome works completely or that these APIs
have Windows-equivalent semantics.

## Commit groups

1. Native API, process/thread information, registry, pipe and I/O compatibility:
   syscall registration, ntdll exports, NDK declarations and kernel handlers.
2. Loader/runtime compatibility: late-loaded TLS, manifest version selection,
   API-set mapping, address waits and heap workarounds.
3. Kernel32 compatibility exports and shims: process/thread attributes, file
   information, time zones, locale, cancellation, address waits and app policies.
4. Supporting Win32 APIs: DNS/resolver wrappers, network notifications, local
   workstation/domain answers, RPC helpers, known folders, security exports,
   suspend/resume export and noninteractive missing-3D reporting.
5. SAC console startup behavior and service retry handling.
6. Investigation instrumentation: process/thread enumeration, exception and
   termination tracing, SRW wait diagnostics, CSR and user32 window/class traces.
7. Standalone diagnostic source files and these checkpoint notes.

Some files contain both compatibility behavior and instrumentation. Those files
are kept intact in the relevant subsystem commit rather than inventing an
unverified reconstruction of the earlier editing sequence. The series should be
reviewed together; declarations and exports can depend on later groups.

The previously committed page-zeroing cache change is separate from this series.
The process-enumeration traces in this checkpoint retain the subsequent
performance change that makes routine `[PROCWALK]` messages opt-in.

## Known limits to review

The following are visible in the preserved code and are not resolved by making
these commits:

- Some APIs are compatibility placeholders. Process mitigation and DLL search
  restriction setters can report success without enforcing the requested policy.
  Power, topology and cycle-time results include placeholder or synthesized data.
- IP address/route notification wrappers create events without delivering actual
  network changes. Extended name-resolution wrappers are primarily synchronous;
  asynchronous completion/cancellation behavior needs validation.
- Workstation/domain queries use local standalone/workgroup answers in place of
  the previous RPC path, including code paths accepting a server name.
- Dynamic TLS uses fixed 128-slot vectors. Registration failure cleanup, existing
  threads, slot limits and full TLS template/zero-fill behavior need review.
- The added I/O cancellation implementation walks the current thread's IRPs.
  Cross-thread cancellation and waiting for individual requests need dedicated
  validation. Address-wait timeout/wakeup semantics also need further testing.
- Heap coalescing changes avoid some assertions, and one invalid-free diagnostic
  is suppressed. This does not establish that the underlying corruption is fixed.
- Several broad diagnostics remain enabled and can affect timing or serial-output
  cost. The explicit corruption diagnostics remain in place.
- Chrome's observed SRW busy waits and the explicit SMP boot failure remain open.

These changes affect shared OS components and can affect applications beyond
Chrome, Node or V8. Keeping them as experimental checkpoints records that scope
without presenting the entire set as independently validated fixes.

## Diagnostic sources

The following root-level files are manual probes, not integrated regression tests:

- `io_probe.c`: repeated file writes and flush timing; expects `C:\js\iotest`.
- `srw_stress.c`: exclusive SRW contention stress.
- `srw_stress2.c`: shared/exclusive contention and syscall-under-lock patterns.
- `threadinfo.c`: process/thread information inspection.
- `wevtapi_stub.c`: standalone event-log API fallback DLL source with unsupported
  operations; it is not a complete event-log implementation or a build target.

Generated `.exe`/`.dll` files, malformed-path build logs and debugger captures
were intentionally left untracked. They remain on disk.

## Validation record

For this commit organization, `git diff --check` was run and the tracked-file
contents were checked against a pre-commit hash manifest. Only redundant blank
lines at EOF in `shellpath.c` and `addrinfo.c` were normalized.

No full rebuild or Chrome/V8 regression run was performed as part of committing
this accumulated state. Prior VM tests covered only the specific performance
changes and basic allocation/app probes described in the workspace's performance
reports. Those tests do not validate every compatibility shim in this series.
