# Chrome rendering investigation — 2026-10-28

Chrome 155.0.8059.85 now paints its browser controls and a local HTML/CSS page,
and executes page JavaScript, in the isolated `vm/chrome-render.qcow2` overlay.
This is an experimental compatibility result, not full Chrome support.
The original `reactos.vhdx` was not modified by this investigation.

`tools/vm_run.cmd` preserves the host launcher from `C:\ReactOS\vm_run.cmd`,
which is outside this Git repository. It boots the tested overlay using the
existing `C:\ReactOS` workspace layout. The overlay and its backing disks are
local test artifacts and are not included in Git.

## Confirmed failures and changes

| Area | Observed failure | Change / result |
| --- | --- | --- |
| Display DPI | `GetDpiForMonitor` returned success without writing either output. Chrome logged scale 0 and dimensions 2147483647. | Return the screen DC's DPI, with argument validation. Guest reports 96×96; Chrome logs scale 1 and 800×600. |
| Anonymous section security | An explicit empty DACL was discarded. Duplicating a read-only section handle could regain write access, violating Chrome's shared-memory validation. | Honor explicit security descriptors in `ObInsertObject`, including unnamed sections. Write escalation now fails with access denied; reduced read access, original write rights, NULL DACLs and default security still work. |
| SRW locks | Contended Chrome threads spun continuously, delaying startup for minutes. Waiters also inferred ownership from the global lock flags before their stack wait record was explicitly released. | Spin briefly, then rendezvous through keyed events. Return only after the specific waiter's wake flag is released. Both shared and exclusive queue paths use the same wake protocol. |
| Wait unregistration | Chrome's UI froze in `base::win::ObjectWatcher::StopWatching`. `UnregisterWaitEx` returned success after a one-shot callback had completed, but never signaled the supplied completion event. | Publish the completion handle before cancellation and use the worker/deregistration reference handoff to signal it exactly when both sides have finished. The before-fix probe timed out; the fixed probe signals. Idle, active-callback and synchronous cancellation cases pass. |
| Optional shell APIs | Delay-loading `SHGetPropertyStoreForWindow` raised a missing-export exception; `SHCreateAssociationRegistration` raised a Wine stub exception. | Export callable functions which clear their output and return `E_NOTIMPL`. Window property stores and association registration are still unsupported. |
| Real-time API set | Delay-loading `api-ms-win-core-realtime-l1-1-1.dll!QueryUnbiasedInterruptTimePrecise` failed. | Add the API-set mapping and a coarse interrupt-clock fallback in kernel32. It does not provide precise interpolation or separate suspend accounting. |
| Snap query | Delay-loading `user32!IsWindowArranged` failed. | Return false; ReactOS has no persistent snap-arrangement state. |

## Required launch workarounds

Copy `chrome-render-test.cmd` beside the guest's `chrome.exe`. Pass a local
HTML path as its first argument, or put `test.html` alongside it.

The tested rendering settings are:

```text
--no-sandbox
--disable-gpu
--disable-gpu-compositing
--disable-software-rasterizer
--disable-features=RemoveRedirectionBitmap,RequestHeaderIntegrity
```

The renderer and GPU service remain separate processes. `--single-process` is not a working
configuration: it reached the document title but crashed in a separate startup
path. Running the compositor inside the browser initially helped isolate the
problem. After fixing wait unregistration, the normal separate GPU service also
paints successfully, handles page clicks, and reloads the document. The final
configuration does not need `--in-process-gpu`. Disabling
`RemoveRedirectionBitmap` selects the GDI output path instead of the modern
DXGI/DirectComposition software swap chain, as shown in Chromium's
[software output device factory](https://raw.githubusercontent.com/chromium/chromium/main/components/viz/service/display_embedder/software_output_device_win.cc).
Hardware acceleration is not fixed.

`RequestHeaderIntegrity` is disabled to avoid a confirmed late-loaded static-TLS
failure in `chromecompaneros.dll`. The browser otherwise paints the page and
then crashes in a worker at that DLL's RVA 0x1280 while dereferencing an empty
TLS slot. The existing `LdrpHandleTlsData` implementation initializes the loading
thread and future threads, but does not populate already-running workers. This
general loader defect remains unfixed. Disabling the feature avoids that DLL's
startup path; it does not repair TLS support.

The sandbox remains disabled as in the original compatibility launches. This
configuration is intended for local VM experiments.

## Validation and artifacts

- Built and booted the modified kernel, ntdll, kernel32, user32, shcore and shell32.
- DPI probes covered all three query types, invalid monitor/type and null outputs.
- Section probes covered empty, NULL and default DACLs, read subsets, same-access
  duplicates, rejected write escalation, and retained original write rights.
- Exclusive SRW stress completed 400,000 operations in one second. Mixed
  readers/writers with syscalls and long-held locks progressed throughout its
  25-second run and completed.
- Added native section-security and shcore DPI regressions to the existing test
  sources, plus idle/completed wait notification regressions in ntdll's existing
  threadpool tests; all three changed test translation units compile. The complete rostests
  suites were not run.
- The rendered page's JavaScript button responded to a native mouse click and
  changed from `Clicks: 0` to `Clicks: 1` after the wait-completion fix. Clicking
  Chrome's reload button reloaded the page and reset the counter to zero.
  Screenshots: `vm/chrome-finalmulti-click.png` and
  `vm/chrome-finalmulti-reload.png`. These use the separate renderer/GPU service.
- Debugger evidence is in `vm/chrome-debug-events*.jsonl`; event 10 identifies
  the `chromecompaneros.dll` TLS crash. `vm/chrome-start-working.py` records the
  successful diagnostic launch. Focused probe results are in
  `vm/chrome-*-verified.json`.
- Build scripts and logs are `vm/chrome-build-*.py` and matching build logs.
  They compile changed translation units using the existing Ninja graph's flags
  and relink against existing build artifacts; this was not a clean full build.

Other multimedia, network, database and OS-version compatibility errors remain
in Chrome's logs. Successful local rendering does not establish general website,
networking, media, sandbox or long-term stability support.
