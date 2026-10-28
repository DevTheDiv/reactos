# SAC lifecycle and serial stress tests

Use a disposable ReactOS VM with EMS enabled, for example boot options
`/redirect=com2 /redirectbaudrate=115200`, and a separate kernel-debug port.
The SAC driver must be loaded. The cancellation test needs the single command
request slot available: use a VM without sacsvr running or another pending
`WAIT_CMD_REQUEST`. It reports an error rather than displacing another waiter.

Build the `sacstress` target with rostests enabled. The source can also be built
with a Windows compiler and `sdk/include/reactos` on its include path.

## Lifecycle regression

Run in the guest:

```cmd
sacstress.exe 10000
```

Each cycle opens two device handles, queues an overlapped command wait, verifies
that a competing wait returns ERROR_BUSY, closes the unrelated handle, and
checks that the original request remains pending. Even iterations cancel the
request with CancelIo and verify ERROR_OPERATION_ABORTED; odd iterations close
the owner and verify a cancelled completion. The next cycle checks slot reuse.

The parent watchdog defaults to 120 seconds; an optional second numeric argument
sets its timeout in milliseconds. For the old-driver reproducer:

```cmd
sacstress.exe 4 5000
```

An uncancellable IRP can prevent the worker from terminating even after the
watchdog fires. That is why this test belongs in a disposable VM. `--worker N`
runs the same checks directly without the parent watchdog.

## Serial channel regression

This exercises the public channel API with a native byte-echo application. It
does not depend on cmd.exe echoing typed commands back or on prompt heuristics.
Start in the guest:

```cmd
sacstress.exe --echo-server 32768
```

Then run on the host, with QEMU's EMS COM port exposed as a TCP server:

```text
python serial_stress.py --port 5560 --bytes 32768 --output sac-serial-result.json
```

The host switches to SACSTRESS, acknowledges its banner, and sends deterministic
printable data in bursts of 1, 7, 31, 127, 255 and 511 bytes. It compares every
returned byte in order and records the SHA-256 digest and elapsed time. There is
no per-character sending delay. The guest varies its channel read size to
exercise partial reads and concurrent input/output. Its receive deadline is
120 seconds; the host has bounded banner and per-burst deadlines.

## Results from 2026-10-28

- Original driver: the four-cycle test hit the five-second watchdog with a hung
  cancellation/cleanup worker.
- Patched driver: direct four-cycle smoke test passed, then the watchdog test
  completed **10,000 cycles with exit code zero**. This covered 5,000 explicit
  cancellations, 5,000 pending-owner closes, 10,000 rejected competing requests,
  10,000 unrelated closes and repeated slot reuse.
- Built all SAC driver C translation units and linked the driver using the
  existing clean build's RosBE flags and generated headers. Compiled the native
  test with `-Wall -Wextra -Werror`. Tested on one vCPU; SMP races are unverified.
- The serial test **did not pass**: the new VM exposed no channel banner/serial
  response. When the echo application's deadline expired and it closed its
  channel, the kernel asserted in the existing `ConMgrHandleEvent` stub in
  `conmgr.c`. Channel-close handling and serial bring-up remain separate issues.
  This test intentionally retains that reproducer; the cancellation fix does
  not claim to repair the channel-close assertion or serial transport.

Local host evidence: `vm/sac-validation/build.log`, `vm/sac-validation-summary.json`
(recorded from the RPC observations before the kernel assertion), `vm/sac-serial-result.json`,
`vm/sac-channel-close-assert.txt` and `vm/sac-test-kernel*.log`.
The test used `vm/sac-test-install.qcow2`, not the working Chrome VM disk.
