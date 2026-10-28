# Official upstream integration candidate

This candidate merges official master `e46f4bb1a1ea7c722f941231565144fe0c286d3e`
(2026-10-25) into the Chrome/Node work at `73cfea8381b`. It was prepared in a
detached worktree, without adding a branch. **Do not promote it to `node-support`
yet:** the application tests below exposed unresolved failures.

## Resolutions and follow-up fixes

- Retained our real kernel32 exports and upstream export signatures; took the
  upstream known-folder implementation, removing duplicate local definitions.
- Retained local and upstream syscall additions together. Preserved cached
  zero-page mappings in hypermap rather than reintroducing the CPU bottleneck.
- Fixed clean-build generated-header dependencies and duplicate declarations.
- Shared the upstream NT6 FLS callback-record layout with thread and fiber
  teardown. Their former pointer-array indexing would call the wrong callback.
- Removed the standalone Vista timer shim's dependency on a private kernel32
  helper, preserving requested handle rights and existing named-timer access.
- Accepted executable-page CFG modifiers at the Win32 memory boundary. An IAT
  trace found V8's executable commit with protection `0x40000040` failed with
  error 87. The regression covers successful commit/protect and rejected invalid
  combinations. This compatibility handling does **not** implement CFG; the
  process mitigation query continues to report CFG disabled.

## Validation performed

A separate clean i386 Debug/NT6-export build and new VM disk were used. The
working Chrome disk and default launcher were left untouched.

- Full boot CD built and installed to the separate disk. Native MinGW Makefiles
  and native WinFlexBison were used because Ninja/MSYS tools stalled in this host
  environment. Generated Makefile path repairs were confined to build output.
- Native `NtDuplicateObject` API test: 19 checks, zero failures.
- Native shcore test: 231 checks, zero failures.
- New VirtualMemory API test: 13 checks, zero failures after deploying the rebuilt
  kernel32 DLL to the test VM.
- Separate FLS fixture: 32 thread exits plus fiber deletion, both callbacks
  invoked 33 times with distinct correct values. The complete introspective FLS
  suite was not run; OS-version-dependent layout assertions need care with this
  NT5-version/NT6-export configuration.
- Exclusive SRW stress: 400,000 operations completed; mixed stress progressed
  and completed its 25-second run.
- Focused wait completion, anonymous-section security, DPI, timer handle rights,
  limited thread-query access and known-folder checks passed. Idle CPU was 100%
  idle in the single-processor measurement before launching Chrome.
- Node v20.20.2 executed JavaScript (`42`) and a worker-thread message (`worker
  42`) with the rebuilt kernel32, without the diagnostic allocation hook.
- Chrome 155 rendered the local HTML and JavaScript. With the pointer positioned
  by SetCursorPos, an emulated mouse button click changed the counter to one.
  Search text was accepted and submitted; Google redirected to its verification
  page. This is not evidence of rendered search results or general web support.

## Promotion blockers

- A later browser breakpoint occurred during the first run. The online search
  run subsequently displayed `STATUS_INTEGER_DIVIDE_BY_ZERO` in a renderer.
  The cause and whether upstream introduced it are not established.
- A debugger run additionally recorded a second-chance invalid-handle exception
  through a socket-close path. Debugger behavior may affect that observation;
  it has not been identified as the cause of the renderer divide-by-zero.
- After a forced reset following a stalled guest reboot, the copied Node image
  on the FAT test disk raised `STATUS_FILE_CORRUPT_ERROR` during paging. Running
  the same application from the read-only CD succeeded. Reinstall and repeat
  using a clean, properly flushed disk before attributing this to upstream.
- A clean reload result was not established during this run. No claim of
  sustained Chrome stability, two-core performance, hardware acceleration,
  sandbox support or MSI installation is made.

Keep the existing software-rendering and RequestHeaderIntegrity workarounds in
[CHROME_RENDERING.md](CHROME_RENDERING.md). The late-loaded TLS issue remains.

## Local evidence

Host-side artifacts live under `C:\ReactOS\vm`: `upstream-run-*.log`,
`upstream-api-results.json`, `upstream-core-stress-results.json`, the focused
`upstream-*-result*.json` files, `upstream-chrome-cd.py`,
`upstream-chrome-ui-stack.json`, and `upstream-chrome-debug.jsonl`.
Screenshots include `upstream-cd-click.png` and `upstream-local-return.png`.
The latter records the renderer error. Diagnostic hooks are external test
artifacts and are not included in the OS source.

Microsoft's [memory protection documentation](https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants)
describes the executable-page CFG modifiers used by V8.
