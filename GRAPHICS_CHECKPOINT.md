# Virtio GPU / Vulkan progress checkpoint

Recorded 2026-10-28 Pacific / 2026-10-29 UTC. Branch:
`graphics/virtio-gpu-vulkan`, based on `node-support` at `dba8148521d`.
Latest verified code milestone: per-file resource ownership (this checkpoint).

## Resume here

The separate Windows Venus host now passes the ReactOS transport probe.
**Actual guest Vulkan execution and rendered-image readback remain unverified.**
Kernel-owned blob creation/mapping/remapping/release passes a bounded
eight-cycle probe with a private polling host adapter, and that proven
CREATE_BLOB/MAP_BLOB/UNMAP_BLOB/UNREF path is now also reachable live, per
open file handle, through five new bounded IOCTLs (`IOCTL_VGPU_QUERY_RESOURCES`,
`_RESOURCE_CREATE/_WRITE/_READ/_UNREF`). Every transfer is still transactional
(map, copy, unmap, every call) and kernel-mediated -- no raw VA is ever handed
to user mode, only bytes. The next implementation is bounded user-mode
mappings, a Vulkan ICD, and a real user submission interface; none exist yet.

## Completed and verified

| Milestone | Evidence / commit |
| --- | --- |
| Graphics branch, architecture, host preflight | `52d818f0b54`; WDDM is not required for this experimental virtio/Venus route. |
| Modern PCI diagnostic driver and native tests | `516bc46c2c9`; `vgpudiag.sys` binds `1af4:1050`, negotiates features, uses a split control queue, and completes fenced GET_DISPLAY_INFO. |
| Phantom queue completion fixed | `516bc46c2c9`; uninitialized shared ring memory caused the null-opaque assertion. The constructor now initializes ring pages; the assertion remains enabled. The poisoned-memory baseline fails, and the fixed ring passes 264,096 completions. |
| Driver reinstall fixed | `e604b97478b`; SetupAPI stages updates to an already-running device and requests reboot. Newdev skips absent instances and preserves useful errors. Repeated reinstall and negative-path tests pass. |
| Bounded capability/context diagnostics | `926a211e88f`; separate versioned snapshot, capped payloads, unique fences, response validation, timeouts, and context-command acknowledgement tests. |
| Separate Windows host preparation | `f75b558378b`; pinned release hash, 7-Zip extraction, no installer execution or default QEMU replacement. |
| Active Windows Venus transport pass | `6b48d164725`; WINQ-EMU Alpha 10 clears the upstream package's lazy renderer initialization failure. |
| Kernel-owned blob lifetime probe | `5732c1b18b5`; eight creates/unrefs, 16 maps/unmaps, 65,536 verified bytes, eight stale-ID rejections, 31 protocol checks. Requires the private polling host adapter described below. |
| Per-file live resource ownership | `vgpuresourcetest`; the boot-time probe above now keeps its proven transport alive (one dedicated GPU context, id 64) instead of resetting, gated on that probe having already reached `VGPU_BLOB_COMPLETE`. Allocation limits (8/file, 32 total), known-byte write/read round-trips, invalid-ID and cross-handle foreign-ID rejection, overflow rejection, and cleanup on both explicit close and real process exit (a child process killed mid-ownership) all pass live against the winq-alpha10-poll2 host. `IRP_MN_STOP_DEVICE`/`REMOVE_DEVICE` now tear the live phase down properly; three full reboot cycles in the same guest each re-established the live phase from a clean boot with identical stats, with no stall, leak, or DMA quarantine. |

The latest guest run fetched capsets 1 (308 bytes), 2 (1408 bytes), and
4/Venus (160 bytes). It completed 22 fenced capability/context commands,
including eight context create/destroy acknowledgement pairs. Both the normal
diagnostic and strict `vgputest --venus` exited 0. It also passed 1,000
open/read/close cycles, 17 protocol/layout/error cases, and 264,096 ring
completions. Reset succeeded without DMA quarantine.

Context command ACKs are not proof of successful Vulkan execution. The audited
QEMU context path can ignore renderer return values. Resource operations and a
real Vulkan command stream with verified output are still required.

## Reproducible configuration and evidence

- Host package: `C:\ReactOS\tools\graphics-host\winq-alpha10`, with its
  `bin\qemu-system-x86_64.exe` invoked explicitly. It reports QEMU 11.0.0;
  the release packages patched virglrenderer 1.3.0.
- Guest: i386 ReactOS, WHPX, one vCPU, 2048 MiB, standard VGA retained, plus
  `virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=true`; `-display egl-headless`.
- Guest driver source checkpoint: `926a211e88f`. Build/test source hashes were
  checked against the artifact manifest. The SYS hash was verified in the guest.
- Launcher: `C:\ReactOS\vm\graphics-winq-launch.ps1`.
- Disposable overlay: `C:\ReactOS\vm\graphics-winq-test.qcow2`.
- Test ISO: `C:\ReactOS\vm\graphics-venus-final.iso`.
- Logs under `C:\ReactOS\vm`: `graphics-winq-result.log`,
  `graphics-winq-kernel.log`, `graphics-winq.err`, and
  `graphics-winq-host-report.json`.
- Exact release/binary hashes and scoped results:
  [winq-alpha10-baseline.json](tools/graphics/winq-alpha10-baseline.json).

The original upstream Windows QEMU 11.1.0 package still fails active Venus
initialization with missing render-server support. Its paused startup pass did
not exercise that path. Use the pinned Windows-specific package for these
experiments. Existing base disks and the default VM launcher were not changed.

## Remaining limitations

- The original Alpha 10 EGL eventfd warning is now confirmed to prevent real
  proxy context creation. A private adapter clears threaded/async-fence flags
  and logs actual renderer return codes; the blob probe passes with it. The
  original package is not sufficient for this resource test. See
  [the blob notes](drivers/virtio/vgpudiag/README.md#kernel-owned-blob-lifecycle-probe)
  and [blob-baseline.json](tools/graphics/blob-baseline.json).
- Unsupported SVM CPUID and interrupt-vector warnings did not block the
  measured operations. The eventfd failure did block real contexts on the
  original EGL configuration; the private polling adapter avoids that path.
  Its behavior for actual Vulkan submissions remains unverified.
- The driver is a polling diagnostic: it resets the device after probing.
  Cached snapshot reads are not repeated GPU submissions or performance tests.
- Blob mappings are validated only inside the kernel-owned probe. No process-owned
  GPU resources, user mapping interface, Vulkan instance/device client, Vulkan
  ICD, native presentation, or DXVK rendering exists yet. Fenced unmap stalls
  in the pinned host; this CPU-only probe uses ordinary unmap completion ACKs.
- No physical GPU driver or hardware acceleration on bare-metal ReactOS has
  been established by these VM tests.
- These were targeted builds using existing generated headers/import libraries,
  not a clean full OS build. Multi-vCPU endurance and fault injection remain open.
- OS-managed reboot stalled during service termination, last logged at
  `sacsvr.exe`. The installer/driver passed after an emulator reset; clean OS
  shutdown/reboot remains unverified. Preserve the shutdown log noted in the
  [driver notes](drivers/virtio/vgpudiag/README.md).

## Next work, in order

1. Bounded user-mode mappings: hand a caller-visible view of a resource's
   bytes without a full raw-VA mapping (e.g. a read/write IOCTL pair is
   already done; a real mapped view, if still needed once an ICD exists,
   wants its own cancellation/stop story before it's added).
2. Connect a minimal NT user-mode Venus client and create a real Vulkan
   instance/device, then execute an off-screen clear with verified pixel readback.
3. Add native presentation and a small DXVK example after execution and resource
   lifetime are demonstrated. Measure CPU use and latency separately.

Per-file resource ownership (opaque IDs, allocation limits, invalid/foreign-ID
rejection, close and process-exit cleanup, reset/stop teardown) is done -- see
the table above and [the driver notes](drivers/virtio/vgpudiag/README.md#per-file-live-resource-ownership).

Architecture and milestone gates: [GRAPHICS_BRINGUP.md](GRAPHICS_BRINGUP.md).
Implementation and test details: [vgpudiag README](drivers/virtio/vgpudiag/README.md).
