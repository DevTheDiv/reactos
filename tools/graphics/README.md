# Graphics host preflight

On the Windows development host, run with Python 3.9 or later:

```text
python tools/graphics/probe_host.py --smoke
```

Override paths when needed:

```text
python tools/graphics/probe_host.py --qemu "C:\Program Files\qemu\qemu-system-x86_64.exe" --smoke --output C:\ReactOS\vm\graphics-host-report.json
```

The default JSON output is `tools/graphics/artifacts/host-report.json`, ignored
by Git. Python's standard library is sufficient; the Vulkan SDK and vulkaninfo
are not required. Windows must have a Vulkan loader/ICD to pass Vulkan inspection.

The probe records:

- QEMU version, device names and exposed Venus properties.
- Windows Vulkan loader version, physical-device descriptions and relevant
  external-memory extensions. Duplicate descriptions are preserved rather than
  incorrectly interpreting them as separate physical boards or merging boards.
- With `--smoke`, actual QEMU initialization of a 2D control and a Venus-enabled
  device, paused-guest status, PCI device presence and clean QMP shutdown.

Smoke processes have no disk, NIC or serial device and execute no guest code.
QEMU uses TCG while paused, so this is not a WHPX/KVM or guest boot test. Each
child connects to a temporary loopback QMP listener; command/reply handling is
sequential, waits are bounded, and failed children are killed and reaped.

Exit codes: 0 means the requested inspections passed; 1 means a loader, QEMU
inspection or 2D-control failure; 2 means Venus device initialization failed.
See the detailed JSON for the exact failing stage and QEMU stderr.

**A pass does not prove Venus context creation, host/guest memory sharing,
rendering, guest Vulkan support, or DXVK support.** The report deliberately keeps
`guest_vulkan_validated` false. Follow the acceptance gates in
[GRAPHICS_BRINGUP.md](../../GRAPHICS_BRINGUP.md).

The initial host measurement is summarized in [host-baseline.json](host-baseline.json).
