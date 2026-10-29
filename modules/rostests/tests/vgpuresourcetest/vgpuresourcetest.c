/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercises the live per-file resource-ownership IOCTLs: allocation limits,
 * known-byte write/read round-trips, foreign/invalid ID rejection, overflow
 * rejection, and cleanup on both explicit close and process exit.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/vgpu_diag.h>

static HANDLE OpenProbe(void)
{
    return CreateFileW(L"\\\\.\\VgpuDiag", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL QueryResources(HANDLE Device, VGPU_RESOURCE_INFO *Info)
{
    DWORD Bytes;
    return DeviceIoControl(Device, IOCTL_VGPU_QUERY_RESOURCES, NULL, 0,
                           Info, sizeof(*Info), &Bytes, NULL) && Bytes == sizeof(*Info);
}

static BOOL CreateResource(HANDLE Device, ULONG *ResourceId)
{
    DWORD Bytes;
    return DeviceIoControl(Device, IOCTL_VGPU_RESOURCE_CREATE, NULL, 0,
                           ResourceId, sizeof(*ResourceId), &Bytes, NULL) && Bytes == sizeof(*ResourceId);
}

static BOOL WriteResource(HANDLE Device, ULONG ResourceId, const UCHAR *Data, ULONG Length)
{
    VGPU_RESOURCE_WRITE_REQUEST Req;
    DWORD Bytes;
    Req.ResourceId = ResourceId;
    Req.Length = Length;
    if (Length) memcpy(Req.Data, Data, Length);
    return DeviceIoControl(Device, IOCTL_VGPU_RESOURCE_WRITE, &Req,
                           FIELD_OFFSET(VGPU_RESOURCE_WRITE_REQUEST, Data) + Length, NULL, 0, &Bytes, NULL);
}

static BOOL ReadResource(HANDLE Device, ULONG ResourceId, ULONG Length, VGPU_RESOURCE_READ_RESPONSE *Resp)
{
    VGPU_RESOURCE_READ_REQUEST Req;
    DWORD Bytes;
    Req.ResourceId = ResourceId;
    Req.Length = Length;
    return DeviceIoControl(Device, IOCTL_VGPU_RESOURCE_READ, &Req, sizeof(Req),
                           Resp, FIELD_OFFSET(VGPU_RESOURCE_READ_RESPONSE, Data) + Length, &Bytes, NULL) &&
           Bytes == FIELD_OFFSET(VGPU_RESOURCE_READ_RESPONSE, Data) + Length;
}

static BOOL UnrefResource(HANDLE Device, ULONG ResourceId)
{
    DWORD Bytes;
    return DeviceIoControl(Device, IOCTL_VGPU_RESOURCE_UNREF, &ResourceId, sizeof(ResourceId), NULL, 0, &Bytes, NULL);
}

static int Fail(const char *Why, DWORD Error)
{
    printf("FAIL: %s (error=%lu)\n", Why, Error);
    return 1;
}

int main(int argc, char **argv)
{
    HANDLE A, B;
    VGPU_RESOURCE_INFO Info;
    ULONG Ids[VGPU_MAX_RESOURCES_PER_FILE], Extra, Foreign;
    UCHAR Pattern[VGPU_RESOURCE_BYTES];
    VGPU_RESOURCE_WRITE_REQUEST OverflowReq;
    VGPU_RESOURCE_READ_RESPONSE Resp;
    DWORD i;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc == 2 && !strcmp(argv[1], "--child"))
    {
        /* Spawned by the parent below to test cleanup on real process exit,
         * not just an explicit CloseHandle. Create resources, then exit
         * without unref'ing -- the parent verifies they were released. */
        A = OpenProbe();
        if (A == INVALID_HANDLE_VALUE) return 1;
        for (i = 0; i < 3; ++i)
        {
            if (!CreateResource(A, &Extra)) return 1;
            printf("CHILD_CREATED %lu\n", Extra);
        }
        fflush(stdout);
        ExitProcess(0); /* leave the handle open; rely on process-exit cleanup */
    }

    A = OpenProbe();
    if (A == INVALID_HANDLE_VALUE) return Fail("open", GetLastError());

    if (!QueryResources(A, &Info)) { CloseHandle(A); return Fail("query resources", GetLastError()); }
    if (!Info.LiveReady)
    {
        puts("SKIP: live resource phase unavailable (no Venus/blob backend on this host)");
        CloseHandle(A);
        return 0;
    }

    /* Allocation limit: fill this handle's quota, then the next must fail. */
    for (i = 0; i < VGPU_MAX_RESOURCES_PER_FILE; ++i)
    {
        if (!CreateResource(A, &Ids[i])) { CloseHandle(A); return Fail("create within limit", GetLastError()); }
    }
    if (CreateResource(A, &Extra) || GetLastError() != ERROR_NOT_ENOUGH_QUOTA)
    { CloseHandle(A); return Fail("limit not enforced", GetLastError()); }

    /* Known-byte write/read round trip on one owned resource. */
    for (i = 0; i < sizeof(Pattern); ++i) Pattern[i] = (UCHAR)(i * 31 + 7);
    if (!WriteResource(A, Ids[0], Pattern, sizeof(Pattern)))
    { CloseHandle(A); return Fail("write", GetLastError()); }
    if (!ReadResource(A, Ids[0], sizeof(Pattern), &Resp) || Resp.Length != sizeof(Pattern) ||
        memcmp(Resp.Data, Pattern, sizeof(Pattern)))
    { CloseHandle(A); return Fail("read mismatch", GetLastError()); }

    /* Overflow: a declared length longer than one resource must be rejected
     * before the driver ever touches the device, regardless of how much of
     * the fixed-size wire buffer is actually populated. */
    OverflowReq.ResourceId = Ids[0];
    OverflowReq.Length = sizeof(OverflowReq.Data) + 1;
    {
        DWORD Bytes;
        if (DeviceIoControl(A, IOCTL_VGPU_RESOURCE_WRITE, &OverflowReq, sizeof(OverflowReq), NULL, 0, &Bytes, NULL) ||
            GetLastError() != ERROR_INVALID_PARAMETER)
        { CloseHandle(A); return Fail("overflow write accepted", GetLastError()); }
    }

    /* Invalid ID: never created, and out of the valid base range. */
    if (UnrefResource(A, VGPU_RESOURCE_BASE_ID + VGPU_MAX_RESOURCES) || GetLastError() != ERROR_INVALID_PARAMETER)
    { CloseHandle(A); return Fail("invalid id accepted", GetLastError()); }
    if (UnrefResource(A, 1) || GetLastError() != ERROR_INVALID_PARAMETER)
    { CloseHandle(A); return Fail("below-base id accepted", GetLastError()); }

    /* Foreign ID: a second handle must not touch the first handle's resources. */
    B = OpenProbe();
    if (B == INVALID_HANDLE_VALUE) { CloseHandle(A); return Fail("open second handle", GetLastError()); }
    if (WriteResource(B, Ids[1], Pattern, 1) || GetLastError() != ERROR_ACCESS_DENIED)
    { CloseHandle(A); CloseHandle(B); return Fail("foreign write accepted", GetLastError()); }
    if (UnrefResource(B, Ids[1]) || GetLastError() != ERROR_ACCESS_DENIED)
    { CloseHandle(A); CloseHandle(B); return Fail("foreign unref accepted", GetLastError()); }
    if (!CreateResource(B, &Foreign)) { CloseHandle(A); CloseHandle(B); return Fail("create on second handle", GetLastError()); }
    if (!UnrefResource(B, Foreign)) { CloseHandle(A); CloseHandle(B); return Fail("unref own resource on second handle", GetLastError()); }
    CloseHandle(B);

    /* Explicit unref churn on the first handle's remaining resources. */
    for (i = 0; i < VGPU_MAX_RESOURCES_PER_FILE; ++i)
    {
        if (!UnrefResource(A, Ids[i])) { CloseHandle(A); return Fail("unref", GetLastError()); }
    }
    if (!QueryResources(A, &Info) || Info.ActiveResources != 0)
    { CloseHandle(A); return Fail("active count after unref", GetLastError()); }
    CloseHandle(A);

    /* Cleanup on explicit close without unref. */
    A = OpenProbe();
    if (A == INVALID_HANDLE_VALUE) return Fail("reopen", GetLastError());
    if (!CreateResource(A, &Extra)) { CloseHandle(A); return Fail("create before close-cleanup", GetLastError()); }
    CloseHandle(A);
    A = OpenProbe();
    if (A == INVALID_HANDLE_VALUE) return Fail("reopen after close-cleanup", GetLastError());
    if (!QueryResources(A, &Info) || Info.ActiveResources != 0)
    { CloseHandle(A); return Fail("active count after close-cleanup", GetLastError()); }
    CloseHandle(A);

    /* Cleanup on real process exit (not just handle close): spawn a child
     * that creates resources and exits without unref'ing. */
    {
        char Path[MAX_PATH], Command[MAX_PATH + 16];
        STARTUPINFOA Startup = {0};
        PROCESS_INFORMATION Process;
        GetModuleFileNameA(NULL, Path, sizeof(Path));
        sprintf(Command, "\"%s\" --child", Path);
        Startup.cb = sizeof(Startup);
        if (!CreateProcessA(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
            return Fail("spawn child", GetLastError());
        WaitForSingleObject(Process.hProcess, 10000);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
    A = OpenProbe();
    if (A == INVALID_HANDLE_VALUE) return Fail("reopen after process exit", GetLastError());
    if (!QueryResources(A, &Info) || Info.ActiveResources != 0)
    { CloseHandle(A); return Fail("active count after process exit", GetLastError()); }
    if (Info.CleanupUnrefs < 4) /* 1 from the close-cleanup case above + 3 from the child */
    { CloseHandle(A); return Fail("cleanup unref count", GetLastError()); }
    CloseHandle(A);

    printf("PASS: limits, write/read, overflow, invalid/foreign IDs, close and process-exit cleanup "
           "(creates=%lu unrefs=%lu limit_rejections=%lu foreign_rejections=%lu overflow_rejections=%lu cleanup_unrefs=%lu)\n",
           Info.TotalCreates, Info.TotalUnrefs, Info.LimitRejections, Info.ForeignIdRejections,
           Info.OverflowRejections, Info.CleanupUnrefs);
    return 0;
}
