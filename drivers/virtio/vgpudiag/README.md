# Experimental virtio-gpu transport diagnostic

`vgpudiag.sys` binds modern PCI device `1af4:1050` as a System device alongside
the existing display adapter. It negotiates only VERSION_1, creates a split
control queue, submits one fenced GET_DISPLAY_INFO request, caches the result,
then resets the device and releases the queue. It exposes a read-only diagnostic
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
