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
They also do not trigger the GL renderer's lazy initialization. Active guest
testing subsequently found the installed Windows Venus backend fails at that
point because render-server support is missing. See the
[guest measurements](../../drivers/virtio/vgpudiag/README.md#venus-capability-bring-up).
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

## Separate Windows Venus host candidate

The installed upstream Windows package fails active Venus initialization.
[WINQ-EMU Alpha 10](https://github.com/cmspam/winq-emu/releases/tag/alpha10)
is a candidate with Windows-specific QEMU/virglrenderer patches. Its release
notes identify QEMU 11.0.0 and virglrenderer 1.3.0 plus the project's patch
series. It is not yet validated with this ReactOS guest. A newer upstream
QEMU version alone does not supply the required Windows renderer integration.

From `C:\ReactOS` in PowerShell:

```powershell
.\source\tools\graphics\prepare_host.ps1
```

This downloads the pinned `alpha10` release, verifies its
[published SHA-256](https://github.com/cmspam/winq-emu/releases/expanded_assets/alpha10),
and uses 7-Zip to extract it into `C:\ReactOS\tools\graphics-host\winq-alpha10`.
It does not execute the installer, change PATH, or replace the existing QEMU
installation. `host-package.json` records the release and extracted binary
hashes. Use `-ArchivePath <downloaded-exe>` for an offline archive, `-SevenZip`
for a nonstandard 7z.exe path, or `-Destination` for a different new directory.
Existing destinations are rejected rather than overwritten.

The script parsed successfully and rejected an incorrect archive digest during
local verification. Actual download/extraction could not be completed in the
restricted execution environment; the GitHub download connection failed.

After extraction, use the candidate's explicit executable path when running
`probe_host.py --qemu <path> --output <report>`. Then boot a **new** disposable
overlay of the graphics test image with that same executable and
`virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=true`, retaining standard VGA.
Check the candidate's supported display backends before choosing EGL or SDL
with GL enabled. Run `vgputest --venus` in the guest and inspect QEMU stderr.
Do not select it in the normal VM launcher until active tests pass. Paused
startup and context-command ACKs still do not prove Vulkan execution.

## Targeted guest driver build (Windows/RosBE)

From `C:\ReactOS`, with an existing i386 MinGW Makefiles build containing
generated headers, import libraries, and SAC driver linker inputs:

```powershell
python source/tools/graphics/build_driver.py --build-dir C:/ReactOS/integration-build-make --output C:/ReactOS/vm/graphics-m3
```

The helper builds `vgpudiag.sys`, its INF, `vgputest.exe`, and
`virtioringtest.exe`, and `vgpuprotocoltest.exe`. It compiles all virtio C sources into a private archive
from the checkout containing the script, even when the supplied build directory
was configured against another worktree. It does not overwrite that worktree's
virtio library. `build.log` records commands; `build-manifest.json` records
source hashes, checkout HEAD, generated-build provenance, and driver SHA-256.
This is a targeted build using existing generated inputs, not a clean OS build.
Use `--rosbe` to override the default sibling `tools/RosBE` directory.

See [vgpudiag](../../drivers/virtio/vgpudiag/README.md) for the isolated VM
procedure, queue regression, and limitations. Do not install this probe as the
primary display driver or infer Vulkan support from its diagnostic result.
