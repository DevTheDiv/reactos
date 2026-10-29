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
initial INF installation succeeded without a reboot. Repeating installation
on the already-bound final overlay returned `ERROR_NO_SUCH_DEVINST`
(`0xe000020b`); repeat-install behavior remains unresolved. The final SYS was
staged offline in that disposable overlay before boot, and its on-disk SHA-256
was verified as
`6a05575be8b5f5a78462a4caacb2cdeb8f5a1d562c435ca409571f194937637a`.
Diagnostic and ring-test logs are `final-result.log` and `final-ring.log` in
the artifact directory. These passes do not establish installer reliability.

This establishes basic guest transport. Venus contexts, Vulkan execution,
resource lifetime, window presentation, and DXVK rendering remain unimplemented.
