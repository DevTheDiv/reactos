/* SPDX-License-Identifier: BSD-2-Clause */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <drivers/sac/sacioctl.h>

static HANDLE OpenSac(void)
{
    return CreateFileW(L"\\\\.\\SAC", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
}

/* A real user of the public channel API for the serial round-trip test. */
static int EchoServer(unsigned long Target)
{
    SAC_IOCTL_CHANNEL_CREATE Request = {0};
    BYTE Buffer[256];
    DWORD Bytes, Ignored, Total = 0, Started = GetTickCount();
    HANDLE Device = CreateFileW(L"\\\\.\\SAC", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (Device == INVALID_HANDLE_VALUE) return 1;
    wcscpy(Request.NameBuffer, L"SACSTRESS");
    wcscpy(Request.DescriptionBuffer, L"SAC stress-test byte echo");
    if (!DeviceIoControl(Device, IOCTL_SAC_CHANNEL_CREATE, &Request,
                          sizeof(Request), NULL, 0, &Ignored, NULL))
    {
        printf("FAIL: channel create error=%lu\n", GetLastError());
        CloseHandle(Device);
        return 1;
    }
    puts("READY: switch to SACSTRESS and acknowledge the channel banner");
    while (Total < Target && GetTickCount() - Started < 120000)
    {
        /* Vary read sizes to exercise partial reads while serial input arrives. */
        DWORD Size = 1 + (Total % sizeof(Buffer));
        if (!DeviceIoControl(Device, IOCTL_SAC_CHANNEL_READ, NULL, 0,
                              Buffer, Size, &Bytes, NULL)) break;
        if (!Bytes) { Sleep(1); continue; }
        if (!DeviceIoControl(Device, IOCTL_SAC_CHANNEL_WRITE, Buffer, Bytes,
                              NULL, 0, &Ignored, NULL)) break;
        Total += Bytes;
    }
    CloseHandle(Device);
    printf("%s: echoed %lu/%lu bytes\n", Total == Target ? "PASS" : "FAIL", Total, Target);
    return Total == Target ? 0 : 1;
}

static BOOL QueueWait(HANDLE Device, OVERLAPPED *Overlapped)
{
    DWORD Bytes = 0, Error;
    ResetEvent(Overlapped->hEvent);
    Overlapped->Internal = Overlapped->InternalHigh = 0;
    if (DeviceIoControl(Device, IOCTL_SAC_WAIT_CMD_REQUEST,
                        NULL, 0, NULL, 0, &Bytes, Overlapped))
    {
        printf("FAIL: unsolicited cmd request\n");
        return FALSE;
    }
    Error = GetLastError();
    if (Error != ERROR_IO_PENDING)
    {
        printf("FAIL: queue error %lu (stop sacsvr before testing)\n", Error);
        return FALSE;
    }
    return TRUE;
}

static int Worker(unsigned long Iterations)
{
    HANDLE Device, Other;
    OVERLAPPED Wait = {0}, Competing = {0};
    DWORD i, Bytes, Error;
    BOOL Success;

    setvbuf(stdout, NULL, _IONBF, 0);
    Wait.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Competing.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Wait.hEvent || !Competing.hEvent) return 1;

    for (i = 0; i < Iterations; ++i)
    {
        Device = OpenSac();
        Other = OpenSac();
        if (Device == INVALID_HANDLE_VALUE || Other == INVALID_HANDLE_VALUE)
        {
            printf("FAIL: open error %lu\n", GetLastError());
            return 1;
        }
        if (!i) puts("stage: queue owner");
        if (!QueueWait(Device, &Wait)) return 1;
        if (!i) puts("stage: competing waiter");

        /* A competing waiter must not replace the owner's IRP. */
        Bytes = 0;
        Success = DeviceIoControl(Other, IOCTL_SAC_WAIT_CMD_REQUEST,
                                  NULL, 0, NULL, 0, &Bytes, &Competing);
        Error = GetLastError();
        if (Success || Error != ERROR_BUSY)
        {
            printf("FAIL: second waiter result=%d error=%lu\n", Success, Error);
            return 1;
        }

        /* Closing an unrelated file object must not cancel the owner. */
        CloseHandle(Other);
        if (!i) puts("stage: unrelated handle closed");
        if (WaitForSingleObject(Wait.hEvent, 0) != WAIT_TIMEOUT)
        {
            printf("FAIL: unrelated cleanup completed the owner\n");
            return 1;
        }

        if (i & 1)
        {
            /* The last user handle closes while its I/O is still pending. */
            CloseHandle(Device);
            Device = INVALID_HANDLE_VALUE;
        }
        else
        {
            /* On the old driver this call can hang indefinitely. */
            if (!i) puts("stage: CancelIo");
            if (!CancelIo(Device))
            {
                printf("FAIL: CancelIo error=%lu\n", GetLastError());
                return 1;
            }
        }
        if (!i) puts("stage: cancellation returned");
        if (WaitForSingleObject(Wait.hEvent, 2000) != WAIT_OBJECT_0)
        {
            printf("FAIL: cancellation did not complete at iteration %lu\n", i);
            return 1;
        }
        if (Device != INVALID_HANDLE_VALUE)
        {
            Bytes = 0xdeadbeef;
            Success = GetOverlappedResult(Device, &Wait, &Bytes, FALSE);
            Error = GetLastError();
            if (Success || Error != ERROR_OPERATION_ABORTED)
            {
                printf("FAIL: cancelled result=%d error=%lu\n", Success, Error);
                return 1;
            }
            CloseHandle(Device);
        }
        else if ((LONG)Wait.Internal != (LONG)0xc0000120)
        {
            printf("FAIL: close completion status=%lx\n", (unsigned long)Wait.Internal);
            return 1;
        }
        if ((i + 1) % 100 == 0) printf("progress %lu/%lu\n", i + 1, Iterations);
    }
    CloseHandle(Wait.hEvent);
    CloseHandle(Competing.hEvent);
    printf("PASS: %lu cycles; cancel, close, competing waiter, unrelated cleanup, slot reuse\n",
           Iterations);
    return 0;
}

int main(int argc, char **argv)
{
    char Path[MAX_PATH], Command[2 * MAX_PATH];
    STARTUPINFOA Startup = {0};
    PROCESS_INFORMATION Process;
    DWORD ExitCode, Timeout = argc > 2 ? strtoul(argv[2], NULL, 10) : 120000;
    unsigned long Iterations = argc > 1 ? strtoul(argv[1], NULL, 10) : 1000;

    if (argc > 2 && !strcmp(argv[1], "--worker")) return Worker(strtoul(argv[2], NULL, 10));
    if (argc > 2 && !strcmp(argv[1], "--echo-server"))
        return EchoServer(strtoul(argv[2], NULL, 10));
    if (!Iterations || Iterations > 100000 || !Timeout) return 1;
    GetModuleFileNameA(NULL, Path, sizeof(Path));
    sprintf(Command, "\"%s\" --worker %lu", Path, Iterations);
    Startup.cb = sizeof(Startup);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    Startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    Startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(Startup.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(Startup.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (!CreateProcessA(NULL, Command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &Startup, &Process)) return 1;
    if (WaitForSingleObject(Process.hProcess, Timeout) != WAIT_OBJECT_0)
    {
        puts("FAIL: worker hung (pending I/O cancellation/cleanup); use a disposable VM");
        TerminateProcess(Process.hProcess, 2);
        ExitCode = 2;
    }
    else GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return ExitCode;
}
