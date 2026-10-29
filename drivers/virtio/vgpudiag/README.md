# Experimental virtio-gpu transport diagnostic

`vgpudiag.sys` binds modern PCI device `1af4:1050` as a System device alongside
the existing display adapter. It negotiates VERSION_1 and advertised
VIRGL/RESOURCE_BLOB/CONTEXT_INIT features, creates a split control queue,
submits fenced display and capability requests, caches the results,
then resets the device and releases the queue. It exposes read-only diagnostic
snapshot through `\\.\VgpuDiag`; there is no rendering or display-driver API.

The i386 prototype polls with bounded waits, masks legacy INTx, limits BAR and
DMA allocations, validates the response size/type/fence, and uses remove locks
for requests. If reset fails, DMA allocations remain quarantined until reboot,
bus mastering is disabled, and another probe on that device instance is rejected.
Reset failure, surprise removal, and multiprocessor operation still need fault
injection and endurance testing. This is not a production graphics driver.

## Reproduce

Use a disposable overlay and retain the primary VGA adapter. Add
`-vga std -device virtio-gpu-pci` to a separate QEMU invocation. Build the normal
`vgpudiag`, `vgputest`, and `virtioringtest` targets with tests enabled, or use
the targeted Windows/RosBE helper documented in
[tools/graphics](../../../tools/graphics/README.md).

Copy the SYS, INF, and test executables onto a test CD. Install with
`vgputest --install D:\vgpudiag.inf`, then run `vgputest 1000` and
`virtioringtest`. Installation may require a reboot; the test reports that flag.
The diagnostic test checks undersized/unsupported requests and repeated
open/read/close operations. These cycles read a cached snapshot, not repeated
GPU submissions.

## Queue initialization regression

The first VM run hit `ASSERT(opaque != NULL)` in `virtqueue_get_buf_split`.
Fresh contiguous allocations were not zeroed, and the split-ring constructor
cleared only its bookkeeping. A stale shared `used->idx` looked like a device
completion before a request actually completed. The shared constructor now
initializes the ring pages; this driver also zeroes its DMA allocations.
The original assertion remains enabled.

`virtioringtest` includes the real shared ring implementation and supplies an
emulated device. Poisoned memory reproduces a phantom completion with the
original constructor. The fixed implementation passes 264,096 completions
across 8/256-entry rings, 64/4096-byte alignment, event suppression on/off,
queue exhaustion, descriptor reuse, reverse completion order, 16-bit index
wraparound, and reinitialization. Memory guards check the allocation bounds.
It passed both on the Windows host and inside ReactOS.

The isolated QEMU 11.1.0/WHPX, i386 ReactOS, one-vCPU/2048-MiB test completed
the real fenced request with `stage=6 status=0 reset=0 completed=1 scanouts=1
flags=0`. Host artifacts and logs are under `C:\ReactOS\vm\graphics-m3`;
the disposable disk is `graphics-m3-fixed.qcow2`. The existing base image and
default VM launcher were not changed. A targeted driver build used generated
headers/import libraries from `integration-build-make`, not a fresh full OS build.

The final guest diagnostic reported scanout 0 at 640x480 and passed 1,000
cached diagnostic open/read/close cycles plus invalid-request checks. The
initial INF installation succeeded without a reboot. The original reinstall
failure is covered by the installer regression below. The milestone SYS was
staged offline in that disposable overlay before boot, and its on-disk SHA-256
was verified as
`6a05575be8b5f5a78462a4caacb2cdeb8f5a1d562c435ca409571f194937637a`.
Diagnostic and ring-test logs are `final-result.log` and `final-ring.log` in
the artifact directory.

## Driver reinstall regression

Rebuild and install the `newdev` and `setupapi` system DLL targets as well as
`vgputest`; the targeted graphics build helper only builds the driver/tests.
Use an overlay and stage system DLLs offline, or test with a freshly built OS.

Run `vgputest --install-test D:\vgpudiag.inf 5` on an isolated VM where the
diagnostic driver is already running. This deliberately forces five real
reinstall operations, checks that each succeeds and requests a reboot, and
checks distinct failures for nonexistent hardware and a missing INF. It does
not perform the requested reboot itself. The test loads the installed system
`newdev.dll`; keep test media free of app-local SetupAPI DLLs as well.

The failure was in the shared installer path, not the GPU INF: SetupAPI tried
to start the already-started stack again. Its update path now completes staging
and sets `DI_NEEDREBOOT` instead. Newdev skips absent registry-only instances
(the current `DIGCF_PRESENT` enumeration includes them), preserves installation
errors across enumeration/cleanup, validates the INF path, and frees ID buffers
between device/property queries. Live replacement of a running driver remains
unsupported; honor the reboot flag before relying on the updated binary.

Original and fixed DLLs were tested on separate disposable overlays with the
same test executable and driver media. The original returned `0xe000020b` for
both a missing INF and the first reinstall. The fixed DLLs distinguished
`ERROR_FILE_NOT_FOUND` from `ERROR_NO_SUCH_DEVINST`, completed all five
reinstalls with `reboot=1`, and retained the successful 1,000 diagnostic cycles
and 264,096 ring completions. Artifacts are under `C:\ReactOS\vm`:
`graphics-inf-baseline-final.log`, `graphics-inf-fixed-final.log`,
`graphics-inf-fixed/` (DLLs, build commands and source hashes), and
`graphics-inf-fixed.qcow2` (private overlay). Both DLLs were rebuilt from this
checkout using existing generated build inputs; this was not a clean OS build.

The staged driver also passed after an emulator reset: five more successful
reinstalls, 1,000 diagnostic cycles, and all ring cases. That log is
`graphics-inf-after-reboot.log`; the boot log is `graphics-inf-reboot-kernel.log`.
Normal OS-managed restart was attempted first, but stalled during service
termination (the last log entry was for `sacsvr.exe`). The preserved log is
`graphics-inf-shutdown-stalled-kernel.log`. Clean shutdown/reboot remains an
open issue; the emulator-reset pass does not establish that path works.

[UpdateDriverForPlugAndPlayDevices requirements](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-updatedriverforplugandplaydevicesw)
describe forced installation, error reporting, and caller responsibility for
the reboot result.

This establishes basic guest transport. Venus contexts, Vulkan execution,
resource lifetime, window presentation, and DXVK rendering remain unimplemented.

## Venus capability bring-up

The original diagnostic IOCTL stays version 1/432 bytes. A separate
`IOCTL_VGPU_QUERY_VENUS` returns a version 1/368-byte snapshot with capset
metadata, transferred payload sizes, a bounded Venus payload prefix, probe
status, last request/reply types, and context-command acknowledgement counts.
Neither IOCTL accepts user GPU commands or creates persistent user resources.

The probe enumerates at most 16 capsets and fetches payloads up to 2048 bytes
through one 4-KiB DMA packet. Larger payloads are skipped, not used for
allocation. Each command validates response length/type, flags and its unique
64-bit fence. Each wait is bounded by five seconds and the capability/context
phase has a 30-second overall deadline. Timeout ends submission; reset must
complete before the outstanding DMA memory is freed. Both snapshots are
published together under the diagnostic lock.

If capset 4 (Venus), its payload, and all required negotiated features are
present, the experimental path attempts eight context create/destroy pairs.
**That path has not yet run successfully on this host.** Even successful
command acknowledgements would not establish a working renderer: QEMU's
`virgl_cmd_context_create` currently ignores the renderer's return value.
An actual Venus/Vulkan protocol operation and verified image readback remain
required. No Vulkan ICD or blob-resource/mapping interface exists yet.

`vgputest 1000` checks basic transport and reports an explicit Venus SKIP when
the backend lacks prerequisites. `vgputest --venus` instead returns failure
unless the Venus payload and eight context acknowledgement pairs are present;
it explicitly reports Vulkan execution as unverified even in that case.
`vgpuprotocoltest` exercises the same reply validator used by the driver:
17 layout, short/oversized response, wrong type/flags, full-width fence, and
device-error cases. It passed on the host and inside ReactOS.

### Active guest measurements (2026-10-29 UTC)

All tests used i386 ReactOS, QEMU 11.1.0, WHPX, one vCPU, 2048 MiB, the primary
standard VGA, separate disposable overlays, and the targeted source build.

| Device beside VGA | Actual guest result |
| --- | --- |
| `virtio-gpu-pci` | Display request passes; gate 1/no 3D; normal diagnostic succeeds and strict Venus test fails as expected. |
| `virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=false` with `-display egl-headless` | Display request passes (1280x800); four fenced capability requests succeed: capset 1 version 1/308 bytes and capset 2 version 2/1408 bytes. Gate 2/no Venus capset; strict Venus test fails as expected. |
| `virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=true` with `-display egl-headless` | First display command times out, reset succeeds; QEMU stderr identifies renderer initialization failure before capsets can be queried. |

The failure text is `Render server support was not enabled in virglrenderer`,
followed by `failed to initialize venus renderer` and
`virgl could not be initialized: -1`. Paused host startup did not exercise
this lazy initialization path. Enabling the `venus` QEMU property is therefore
insufficient on the installed Windows package. A compatible host renderer
build (including its Windows memory-sharing path), or a Linux reference host,
is the next gate before claiming guest Venus context support.

Both successful configurations also passed 1,000 diagnostic cycles, all 17
protocol cases, and 264,096 ring completions. Final SYS SHA-256:
`57d9032bc194d3125569e8cd3e01bc500c202b02b61f03788dcdf3fd7fb0e8f7`.
Local evidence is under `C:\ReactOS\vm`: `graphics-venus-control-final-result.log`,
`graphics-virgl-final-result.log`, `graphics-venus-failed-kernel.log`,
`graphics-venus.err`, `graphics-venus/` (manifest/build), and the matching
`graphics-venus-control.qcow2`, `graphics-virgl-test.qcow2` overlays. The failed
Venus run used the preceding probe build; its failure occurs before the final
payload-query changes are reached. This was not a clean OS build.

References: [Virtio GPU wire definitions](https://github.com/torvalds/linux/blob/master/include/uapi/linux/virtio_gpu.h),
[QEMU renderer/context code](https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu-virgl.c),
and [Mesa Venus requirements](https://docs.mesa3d.org/drivers/venus.html).
