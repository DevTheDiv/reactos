# Virtio GPU / Vulkan groundwork

Branch: `graphics/virtio-gpu-vulkan`, based on `node-support` at `dba8148521d`.

The first target is a render-only virtio-gpu prototype with verified resource
lifetime and synchronization. Build toward a Vulkan ICD, then a small DXVK
application. This route does not require implementing WDDM. It still requires a
real guest kernel driver, a ported user-mode driver, and presentation support.

## Current evidence

The host preflight tool is [tools/graphics/probe_host.py](tools/graphics/probe_host.py).
Its initial results are recorded in [host-baseline.json](tools/graphics/host-baseline.json).

- Windows Vulkan instance creation and physical-device enumeration passed.
  NVIDIA RTX 4080, GTX 1080 and AMD Radeon Graphics descriptions were returned,
  with Vulkan 1.4 device APIs. Repeated descriptions in loader enumeration are
  not evidence of additional physical boards.
- Installed QEMU is `11.1.0`, package `v11.1.0-12130-ge470268ff4`. It advertises
  virtio-gpu GL devices, blob memory, hostmem and Venus properties.
- A diskless 2D control and a diskless Venus-enabled device both initialized,
  exposed PCI `1af4:1050`, answered QMP while paused, and exited cleanly.
- **No guest Vulkan context was verified, no Vulkan commands were submitted, and no
  accelerated image was rendered.** Device initialization is only the first
  host gate. Active guest testing found a host blocker in the upstream package: the bundled
  virglrenderer fails lazy Venus initialization because render-server support
  was not built in. Ordinary VirGL capset discovery/payload transfer succeeds.
  See [the guest measurements](drivers/virtio/vgpudiag/README.md#venus-capability-bring-up).
- The separate WINQ-EMU Alpha 10 package clears that initialization failure.
  The ReactOS guest fetched capsets 1, 2 and 4 (Venus), completed eight context
  create/destroy command pairs and passed the strict Venus diagnostic. These
  acknowledgements do not verify actual Vulkan execution or image readback.
  The pinned measurement is [winq-alpha10-baseline.json](tools/graphics/winq-alpha10-baseline.json).
- Host ICDs advertise Win32 external-memory handles, not Linux fd/dma-buf
  extensions. That is normal platform differentiation, not a Windows Vulkan
  failure. Stock Venus documentation specifies Linux/Android interoperability;
  actual support in this Windows host build must be measured rather than assumed.

The initial checks used fresh, paused, diskless QEMU processes. Active follow-up
used disposable disk overlays and a separate host package. Existing base disks
and the default VM launcher were not changed.

## Existing code worth reusing

| Area | Existing code | Implication |
| --- | --- | --- |
| Virtio transport | `sdk/lib/drivers/virtio/` | Modern/legacy PCI and split/packed rings exist. Audit and reuse the modern PCI/split-ring subset first. |
| NT transport integration | `drivers/network/dd/netkvm/Common/ParaNdis-VirtIO.c` | Example register, allocation and transport callbacks; its NDIS-specific operations cannot be copied unchanged into a GPU driver. |
| Display fallback | `win32ss/drivers/{displays,miniport,videoprt}/` | Preserve the existing desktop/display path during render-only development. |
| Vulkan definitions | `sdk/include/wine/vulkan*.h`, `sdk/lib/vkd3d/include/vulkan/` | Definitions exist; they do not establish a working loader, ICD or guest Vulkan runtime. |
| Diagnostics | SAC and `modules/rostests/tests/sacstress/` | Keep an independent serial control path for crashes, cancellation and reset tests. |

The initial audit found no virtio-gpu implementation or usable Vulkan ICD.
The experimental WDM/PnP driver now lives in `drivers/virtio/vgpudiag` and uses
the existing virtio library. A usable Vulkan ICD remains absent. Add no
placeholder exports that claim successful GPU operations.

## Architecture boundary

```text
Windows application / later DXVK
        |
Windows Vulkan loader + ReactOS-compatible Venus ICD
        |
native device operations: resources, mapping, contexts, fences
        |
ReactOS virtio-gpu render driver
        |
virtio PCI / virtqueues / GPU command protocol
        |
QEMU + virglrenderer Venus backend + host Vulkan driver
```

The device speaks virtio-gpu, not Linux DRM IOCTLs. Mesa's Linux-facing calls need
an NT adapter or a deliberately implemented compatibility layer. Do not start
by promising all Linux DRM interfaces or porting an entire physical GPU driver.

A future physical GPU backend has separate initialization, firmware, DMA,
interrupt, memory-management and recovery requirements. Virtual rendering cannot
validate those requirements.

## Milestones and acceptance gates

| Step | Deliverable | Required evidence |
| --- | --- | --- |
| 0: host baseline | Capability report and a known-working Venus context/render reference | Windows-specific host passes startup, capset transfer and context-command ACK checks. Actual Vulkan execution/rendering remains unverified. |
| 1: guest transport | Experimental render-only PCI driver | Correct device binding, bounded BAR mapping, feature negotiation, control queue and GET_DISPLAY_INFO response. Unsupported devices/features fail cleanly. |
| 2: resource lifetime | Resource create/attach/transfer/unref and bounded NT device interface | Deterministic 2D resource contents; create/close churn, cancellation, invalid sizes/IDs and device-stop tests without leaks or dangling DMA. |
| 3: Venus transport | Capset discovery, contexts, blobs, host-visible mapping and fences | Negotiate a Venus context and run a real off-screen Vulkan clear/readback with expected pixels. Prove context support rather than inferring it from the QEMU property. |
| 4: presentation | Native window presentation | Resize, minimize/restore and window/process close survive sustained rendering. Start with bounded readback to the existing GDI display path if necessary; measure that copy cost separately. |
| 5: DXVK sample | Pinned DXVK/Vulkan driver versions and one small D3D program | Actual rendering, pixel checks, repeated launch/exit and stable sustained execution. Check the selected DXVK release's requirements explicitly. |

Step 1 now has an experimental implementation in `drivers/virtio/vgpudiag`.
It has completed a fenced GET_DISPLAY_INFO request in an i386 ReactOS guest;
see [the transport notes](drivers/virtio/vgpudiag/README.md) for evidence and
limits. The missing host context proof remains open. Do not modify the default VM launcher or replace the primary display
driver for these experiments. Use a new overlay and add a non-VGA virtio-gpu
device beside the existing VGA display. A device initialized without a guest
driver does not count as completing step 1.

Capability bring-up now covers bounded capset enumeration/payload transfer and
Venus context-command acknowledgements in a real ReactOS guest with the
Windows-specific host. Step 3 is not complete. Implement/test resource lifetime,
blob mapping and actual context execution before connecting a user-mode Vulkan
driver. Keep the upstream-host failure separate from the working diagnostic
configuration; do not infer that any Windows QEMU package supports Venus.

## Initial driver constraints

- Target the current i386 guest first. Use a small hostmem window in experiments;
  do not copy multi-gigabyte Linux examples into a 32-bit address space. Reject
  unmappable BAR addresses, overflowing sizes and out-of-range offsets.
- Bring up modern PCI and split rings before adding alternate transport modes.
  Validate capabilities and negotiated feature dependencies against the virtio
  specification; never accept arbitrary host lengths as guest allocation sizes.
- Interrupt handlers acknowledge and schedule bounded work. Allocate memory,
  perform waits and complete user requests at appropriate IRQLs outside queue
  spin locks. Separate protocol code from NT allocation/interrupt operations.
- Every open file owns a context and its resource IDs. Use opaque IDs rather
  than kernel pointers in the user interface. Define explicit structure sizes,
  versions and limits before exposing any IOCTL ABI.
- Give resources and in-flight submissions explicit references. Closing a file
  prevents new submissions, cancels/drains its requests, resolves its fences and
  releases mappings only after device access has ended.
- Design stop/remove/reset behavior before enabling DMA. A cancelled request or
  GPU reset must not complete as successful rendering. Keep timeouts bounded and
  report the operation and fence ID through debug/SAC diagnostics.

## Verification plan

Use the same workload and pinned images for before/after measurements. Record
build commit, guest architecture, QEMU command, backend versions and host device.

1. Transport: malformed/missing capabilities, feature rejection, queue-full
   behavior, descriptor wraparound and invalid response lengths.
2. Resources: size overflow, allocation failure, invalid/foreign IDs, double
   release, mapped-resource close, and repeated process exits.
3. Concurrency: multiple handles/processes, submit/cancel/complete races,
   interrupt/completion ordering, reset during work, and later 2/4-vCPU runs.
4. Rendering: known pixel patterns and off-screen readback hashes, then native
   window presentation. Enumeration or a successful return code alone is not a
   rendering pass.
5. Endurance: repeated application launch/exit and sustained rendering with
   resource counts, nonpaged-pool use, fence latency and CPU utilization recorded.

Only write tests for implemented behavior or a concrete failing case. Keep host
capability checks distinct from guest correctness and performance results.

## Primary references

- [QEMU virtio-gpu backends and Venus configuration](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html)
- [Mesa Venus requirements, vtest and virtualization setup](https://docs.mesa3d.org/drivers/venus.html)
- [Virtio specification](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)

Pin the protocol and renderer revisions when implementing the first guest
queue; the pages above are live upstream references, not a frozen compatibility
contract.
