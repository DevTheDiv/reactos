# Virtio GPU / Vulkan progress checkpoint

Recorded 2026-10-28 Pacific / 2026-10-29 UTC. Branch:
`graphics/virtio-gpu-vulkan`, based on `node-support` at `dba8148521d`.
Latest verified milestone: `6b48d164725`.

## Resume here

The separate Windows Venus host now passes the ReactOS transport probe.
**Actual guest Vulkan execution and rendered-image readback remain unverified.**
The next implementation is bounded blob-resource creation, mapping, unmapping,
and release, with ownership and cleanup tied to each open file/process. No
Vulkan ICD, user submission interface, or persistent user resource API exists yet.

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

- `Failed to create eventfd`, unsupported SVM CPUID, and interrupt-vector
  warnings appear on the candidate host. They did not block the measured
  operations; rendering/synchronization consequences remain unverified.
- The driver is a polling diagnostic: it resets the device after probing.
  Cached snapshot reads are not repeated GPU submissions or performance tests.
- No validated blob mapping, process-owned GPU resources, Vulkan instance/device
  client, Vulkan ICD, native presentation, or DXVK rendering exists yet.
- No physical GPU driver or hardware acceleration on bare-metal ReactOS has
  been established by these VM tests.
- These were targeted builds using existing generated headers/import libraries,
  not a clean full OS build. Multi-vCPU endurance and fault injection remain open.
- OS-managed reboot stalled during service termination, last logged at
  `sacsvr.exe`. The installer/driver passed after an emulator reset; clean OS
  shutdown/reboot remains unverified. Preserve the shutdown log noted in the
  [driver notes](drivers/virtio/vgpudiag/README.md).

## Next work, in order

1. Define versioned, bounded resource operations and per-file ownership:
   opaque resource IDs, allocation limits, references, and cleanup rules.
2. Implement RESOURCE_CREATE_BLOB, MAP_BLOB, UNMAP_BLOB, and RESOURCE_UNREF.
   Discover and validate the host-visible window; bound every size and offset,
   including what the i386 guest can map. Establish cleanup/reset behavior
   before exposing mappings to user mode.
3. Test known-byte mappings, allocation/free churn, invalid and foreign IDs,
   overflow, process exit, failed commands, and reset/stop cleanup. A local
   mapping check alone does not establish GPU visibility or execution.
4. Connect a minimal NT user-mode Venus client and create a real Vulkan
   instance/device, then execute an off-screen clear with verified pixel readback.
5. Add native presentation and a small DXVK example after execution and resource
   lifetime are demonstrated. Measure CPU use and latency separately.

Architecture and milestone gates: [GRAPHICS_BRINGUP.md](GRAPHICS_BRINGUP.md).
Implementation and test details: [vgpudiag README](drivers/virtio/vgpudiag/README.md).
