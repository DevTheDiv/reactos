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

This establishes basic guest transport. Vulkan execution, persistent user
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
**That path now passes with the separate WINQ-EMU Alpha 10 host package.** Even successful
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

The separate Windows-specific package has since cleared this initialization
failure, as measured below; the upstream package remains unchanged.

Both successful configurations also passed 1,000 diagnostic cycles, all 17
protocol cases, and 264,096 ring completions. Final SYS SHA-256:
`57d9032bc194d3125569e8cd3e01bc500c202b02b61f03788dcdf3fd7fb0e8f7`.
Local evidence is under `C:\ReactOS\vm`: `graphics-venus-control-final-result.log`,
`graphics-virgl-final-result.log`, `graphics-venus-failed-kernel.log`,
`graphics-venus.err`, `graphics-venus/` (manifest/build), and the matching
`graphics-venus-control.qcow2`, `graphics-virgl-test.qcow2` overlays. The failed
Venus run used the preceding probe build; its failure occurs before the final
payload-query changes are reached. This was not a clean OS build.

### WINQ-EMU Alpha 10 follow-up (2026-10-29 UTC)

The separate package at `C:\ReactOS\tools\graphics-host\winq-alpha10` passed
the diskless preflight and then an active ReactOS boot in a fresh
`graphics-winq-test.qcow2` overlay of the diagnostic control image. The device
was `virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=true`, with EGL headless,
standard VGA, WHPX, one vCPU and 2048 MiB. The same SYS hash recorded above
was verified inside the guest.

The guest fetched capsets 1/308 bytes, 2/1408 bytes and **4 (Venus)/160 bytes**.
It completed 22 fenced capability/context commands, including eight context
create/destroy acknowledgement pairs, with `gate=5 status=0`. Both ordinary
and strict diagnostics exited 0; 1,000 diagnostic cycles, 17 protocol cases,
and 264,096 ring completions passed. Device reset succeeded without DMA
quarantine. No blob mapping or actual Vulkan command stream was tested.

The host logged `Failed to create eventfd`, plus CPUID/interrupt-vector
warnings. These did not prevent the measured diagnostic operations; their
impact on rendering and synchronization remains unverified. Do not treat the
capset/context-ACK pass as a rendered-image pass.

Pinned package hashes and scoped results are in
[winq-alpha10-baseline.json](../../../tools/graphics/winq-alpha10-baseline.json).
Local evidence under `C:\ReactOS\vm` is `graphics-winq-result.log`,
`graphics-winq-kernel.log`, `graphics-winq.err`, `graphics-winq-launch.ps1`,
and `graphics-winq-host-report.json`. No default launcher or base disk changed.

References: [Virtio GPU wire definitions](https://github.com/torvalds/linux/blob/master/include/uapi/linux/virtio_gpu.h),
[QEMU renderer/context code](https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu-virgl.c),
and [Mesa Venus requirements](https://docs.mesa3d.org/drivers/venus.html).

## Kernel-owned blob lifecycle probe

`5732c1b18b5` adds a boot-time probe and a separate version 1/68-byte
`IOCTL_VGPU_QUERY_BLOB` snapshot. `vgputest --blob` requires this probe to pass.
The original display and Venus snapshot layouts are unchanged. There are still
no process-owned resource handles, user mappings, or arbitrary submissions.

The probe validates the modern PCI shared-memory capability (type 8, ID 1),
including its full 64-bit offset/length, the assigned BAR span, page alignment,
overflow and the initial i386 mapping limit below 4 GiB. It maps only a single
4-KiB resource at a time, not the full 256-MiB host window. Cache policy comes
from the validated MAP_BLOB response. CPU mappings are released before host
unmap, and no CPU mapping survives the probe.

Eight iterations reuse resource ID 256 after CREATE_BLOB, MAP_BLOB,
UNMAP_BLOB, remap and RESOURCE_UNREF. Each iteration writes a distinct byte
pattern, checks it, then checks persistence after remapping. A MAP_BLOB request
after each unref must return INVALID_RESOURCE_ID. This tests actual absence
before reusing the ID, rather than trusting an unref ACK alone.

All operations except unmap use unique fenced responses. Unmap uses an ordinary
command-completion ACK with zero fence/flags: this transport-memory probe has
no GPU submissions in flight. Fenced unmap stalled in the pinned host's deferred
path. This exception must not be used to unmap memory still referenced by GPU
work; a future submission API must drain the relevant GPU fences first.
Response validation rejects mismatched fenced/unfenced replies. Any failed
command ends submission and falls back to device reset. The existing bounded
reset/quarantine policy applies. The phase has a 30-second overall deadline.

### Windows host adapter required for this probe

More detailed renderer logging established that the original Alpha 10 EGL
`eventfd` warning is consequential: proxy context creation fails fencing, even
though QEMU returns context-command ACKs. CREATE_BLOB then fails with an invalid
context. The earlier capset/ACK measurement remains valid only within its
recorded scope; it did not establish a usable context.

`tools/graphics/virgl_poll.c` is an experimental host adapter around the pinned
renderer. It removes THREAD_SYNC/ASYNC_FENCE_CB at initialization, leaves QEMU's
renderer polling active, logs actual renderer context-creation return values,
and forwards all other exports unchanged. It never fabricates resource or fence
success. In the successful run, renderer context creation returned 0 and the
real host subsequently created and mapped the blobs.

Build a separate copy with an x64 MinGW GCC and objdump:

```powershell
python source/tools/graphics/build_poll_host.py --package C:/ReactOS/tools/graphics-host/winq-alpha10 --output C:/ReactOS/tools/graphics-host/winq-alpha10-poll
```

The output must be a new directory. The original renderer hash is enforced,
binary/source hashes are recorded, and original named/ordinal exports are
forwarded (including the GUID exports QEMU imports). The original package and
default launcher stay untouched. This is a diagnostic workaround, not a
production host scheduling/fencing implementation. Its behavior for actual
Vulkan submissions and concurrent contexts is unverified.

### Verified results

The successful WHPX/i386/one-vCPU/2048-MiB test used the local private host
`winq-alpha10-poll2`, EGL headless, standard VGA, and the existing Venus GPU
options. It completed eight creates/unrefs, 16 maps/unmaps, 65,536 verified bytes
and eight stale-ID rejections, then destroyed its context and reset without DMA
quarantine. `vgputest --blob` exited 0. The 1,000 cached diagnostic cycles,
31 protocol/layout/bounds/fence checks and 264,096 ring completions also passed.
The non-3D control passed its normal diagnostic and explicitly skipped unsupported
Venus/blob probes. These were targeted builds, not a clean OS build or endurance
test. Local evidence under `C:\ReactOS\vm`:

- `graphics-blob-final-result.log`, `graphics-blob-control-result.log`;
- `graphics-blob-poll-kernel.log`, `graphics-blob-poll.err`,
  `graphics-blob-final-renderer.log`;
- `graphics-blob-final-launch.ps1`, `graphics-blob-test.qcow2`,
  `graphics-blob-final.iso`, `graphics-blob/build-manifest.json`.

See [blob-baseline.json](../../../tools/graphics/blob-baseline.json) for hashes
and scoped measurements. These results prove CPU mapping/persistence and
host resource release, not GPU access to the pattern or Vulkan rendering.

## Per-file live resource ownership

The kernel-owned blob probe above only ever proved CREATE_BLOB/MAP_BLOB/
UNMAP_BLOB/UNREF against one hardcoded resource ID, torn down again before
the driver finished starting. There was no live device left for any later
IOCTL to act on. Five new bounded IOCTLs make that same, already-proven
command path reachable live, scoped per open file handle:

| IOCTL | Direction | Purpose |
| --- | --- | --- |
| `IOCTL_VGPU_QUERY_RESOURCES` | out | Versioned snapshot: live-ready flag and running create/unref/rejection counters. |
| `IOCTL_VGPU_RESOURCE_CREATE` | out | Creates a one-page blob, returns its opaque id (`VGPU_RESOURCE_BASE_ID` + a driver-owned slot index). |
| `IOCTL_VGPU_RESOURCE_WRITE` | in | Transactional: map, `WRITE_REGISTER_BUFFER_UCHAR`, unmap, in that one call. |
| `IOCTL_VGPU_RESOURCE_READ` | in/out | Same shape, reverse direction. Aliases one METHOD_BUFFERED buffer; the request fields are captured to locals before the response is written. |
| `IOCTL_VGPU_RESOURCE_UNREF` | in | Releases a resource this handle owns. |

No mapping is ever held across two IOCTL calls, and no CPU virtual address is
ever returned to user mode -- only bytes, same transactional discipline as the
boot-time probe. `VGPU_MAX_RESOURCES_PER_FILE` (8) bounds each handle;
`VGPU_MAX_RESOURCES` (32) bounds the device total.

**Device lifecycle change required for this:** `GpuProbe`'s `Done:` path used
to unconditionally reset and tear everything down before `IRP_MN_START_DEVICE`
even completed, which is why no later IOCTL ever had a live queue to use.
`Done:` now keeps the already-proven transport (BARs, virtio device, queue,
packet page) alive instead, but only when the blob probe just reached
`VGPU_BLOB_COMPLETE` -- one more GPU context (id `VGPU_LIVE_CONTEXT`, 64) is
created on the same queue, and `Ext->LiveReady` gates everything else.
`IRP_MN_STOP_DEVICE` (newly handled; it previously fell straight through to
the lower driver with no driver-specific action) and `IRP_MN_REMOVE_DEVICE`
both call a new `GpuTeardownLive`, which mirrors `Done:`'s own teardown.
`IRP_MJ_CLEANUP` releases whatever the closing handle still owns -- NT sends
`CLEANUP` for every open handle during process rundown the same as an explicit
`CloseHandle`, so this covers both uniformly. A `FAST_MUTEX` serializes every
live resource op (and `GpuTeardownLive`) against each other and against the
single in-flight command packet the transport has always been limited to.

`vgpuresourcetest` exercises: filling and exceeding the per-file allocation
limit, a known-byte write/read round trip, an oversized write/read rejected
before the driver ever touches the device, an invalid id and one below
`VGPU_RESOURCE_BASE_ID`, a second handle rejected from writing/unref'ing the
first handle's resource (and succeeding on its own), explicit unref churn,
cleanup on an unref'd `CloseHandle`, and cleanup on a *real* process exit --
a spawned child process creates resources and calls `ExitProcess` without
closing cleanly, and the parent confirms they were released. All of the above
passed live against the `winq-alpha10-poll2` host in the same run that
re-verified the existing blob/Venus probes unchanged. The guest was then
rebooted three times in a row (via SAC's native `shutdown` command) to
exercise the new `STOP_DEVICE`/`REMOVE_DEVICE` teardown path; each boot
re-established the live phase from scratch with identical stats, no stall
(including at the `sacsvr.exe` shutdown checkpoint), leak, or DMA quarantine.

Next: bounded user-mode mappings, a Vulkan ICD, and a real user submission
interface -- followed by actual Venus/Vulkan execution and pixel readback.
