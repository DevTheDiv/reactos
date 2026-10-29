/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <newdev.h>
#include <setupapi.h>
#include <drivers/vgpu_diag.h>

static HANDLE OpenProbe(void)
{
    return CreateFileW(L"\\\\.\\VgpuDiag", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static int CheckVenus(BOOL Required)
{
    VGPU_VENUS_INFO Info;
    DWORD Bytes = 0, Error, i;
    HANDLE Device = OpenProbe();
    BOOL Success;
    if (Device == INVALID_HANDLE_VALUE) return 1;
    Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_VENUS, NULL, 0,
                              &Info, sizeof(Info) - 1, &Bytes, NULL);
    Error = GetLastError();
    if (Success || Error != ERROR_INSUFFICIENT_BUFFER)
    { puts("FAIL: short Venus snapshot accepted/wrong error"); CloseHandle(Device); return 1; }
    Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_VENUS, NULL, 0,
                              &Info, sizeof(Info), &Bytes, NULL);
    CloseHandle(Device);
    if (!Success || Bytes != sizeof(Info) || Info.Size != sizeof(Info) ||
        Info.Version != VGPU_VENUS_VERSION || Info.CapsetCount > VGPU_MAX_CAPSETS)
    { puts("FAIL: Venus diagnostic schema/read"); return 1; }
    printf("venus gate=%lu status=%08lx capsets=%lu/%lu commands=%lu context_ack_pairs=%lu last=%lx/%lx\n",
           Info.Gate, Info.Status, Info.CapsetCount, Info.ReportedCapsets,
           Info.CompletedRequests, Info.ContextAckPairs, Info.LastCommand, Info.LastResponse);
    for (i = 0; i < Info.CapsetCount; ++i)
        printf("capset %lu: id=%lu max_version=%lu max_size=%lu data_bytes=%lu\n", i,
               Info.Capsets[i].Id, Info.Capsets[i].MaxVersion, Info.Capsets[i].MaxSize, Info.Capsets[i].DataBytes);
    printf("venus data_bytes=%lu prefix=", Info.VenusDataBytes);
    if (Info.VenusPrefixBytes > VGPU_VENUS_PREFIX_BYTES) return 1;
    for (i = 0; i < Info.VenusPrefixBytes; ++i) printf("%02x", Info.VenusPrefix[i]);
    puts("");
    if (Info.Gate == VGPU_VENUS_NOT_PROBED || Info.Gate == VGPU_VENUS_FAILED ||
        (Info.Status && Info.Status != 0xc00000bb))
    { puts("FAIL: Venus transport probe"); return 1; }
    if (Required && (Info.Status || Info.Gate != VGPU_VENUS_CONTEXT_ACK || Info.ContextAckPairs != 8))
    { puts("FAIL: Venus prerequisites/context acknowledgements unavailable"); return 1; }
    if (Info.Gate == VGPU_VENUS_CONTEXT_ACK)
        puts("PASS: Venus capset and context command acknowledgements; Vulkan execution remains unverified");
    else
        puts("SKIP: host does not provide the Venus prerequisites; Vulkan unavailable");
    return 0;
}

static int CheckBlob(BOOL Required)
{
    VGPU_BLOB_INFO Info;
    DWORD Bytes, Error;
    BOOL Success;
    HANDLE Device = OpenProbe();
    if (Device == INVALID_HANDLE_VALUE) return 1;
    Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_BLOB, NULL, 0,
                              &Info, sizeof(Info) - 1, &Bytes, NULL);
    Error = GetLastError();
    if (Success || Error != ERROR_INSUFFICIENT_BUFFER)
    { CloseHandle(Device); puts("FAIL: short blob snapshot"); return 1; }
    Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_BLOB, NULL, 0,
                              &Info, sizeof(Info), &Bytes, NULL);
    CloseHandle(Device);
    if (!Success || Bytes != sizeof(Info) || Info.Size != sizeof(Info) || Info.Version != VGPU_BLOB_VERSION)
    { puts("FAIL: blob diagnostic schema/read"); return 1; }
    printf("blob stage=%lu status=%08lx window_bar=%lu window_bytes=%08lx%08lx cycles=%lu maps=%lu/%lu resources=%lu/%lu pattern_bytes=%lu cache=%lu stale=%lu last=%lx/%lx\n",
           Info.Stage, Info.Status, Info.WindowBar, Info.WindowBytesHigh, Info.WindowBytesLow,
           Info.CyclesCompleted, Info.MapCount, Info.UnmapCount, Info.ResourceCreates,
           Info.ResourceUnrefs, Info.PatternBytes, Info.CachePolicy, Info.StaleIdRejections, Info.LastCommand, Info.LastResponse);
    if (!Required && Info.Stage == VGPU_BLOB_UNSUPPORTED && Info.Status == 0xc00000bb)
    { puts("SKIP: blob prerequisites unavailable"); return 0; }
    if (Info.Status || Info.Stage != VGPU_BLOB_COMPLETE || Info.CyclesCompleted != VGPU_BLOB_CYCLES ||
        Info.MapCount != 2 * VGPU_BLOB_CYCLES || Info.UnmapCount != Info.MapCount ||
        Info.ResourceCreates != VGPU_BLOB_CYCLES || Info.ResourceUnrefs != Info.ResourceCreates ||
        Info.PatternBytes != 2 * VGPU_BLOB_CYCLES * 4096 || Info.StaleIdRejections != VGPU_BLOB_CYCLES)
    { puts("FAIL: blob lifecycle/pattern probe"); return 1; }
    puts("PASS: kernel blob create/map/unmap/remap/unref and byte persistence; GPU execution remains unverified");
    return 0;
}

static int Install(const char *Path, DWORD Cycles, BOOL Regression)
{
    typedef BOOL (WINAPI *UPDATE)(HWND, LPCWSTR, LPCWSTR, DWORD, PBOOL);
    WCHAR Input[MAX_PATH], Full[MAX_PATH], Missing[MAX_PATH], LibraryPath[MAX_PATH];
    HMODULE Library;
    UPDATE Update;
    BOOL Reboot = FALSE, Success;
    DWORD Length, i, Error;
    HANDLE Probe;
    BOOL Failed = FALSE;
    if (!MultiByteToWideChar(CP_ACP, 0, Path, -1, Input, MAX_PATH)) return 1;
    Length = GetFullPathNameW(Input, MAX_PATH, Full, NULL);
    if (!Length || Length >= MAX_PATH) return 1;
    /* Exercise the installed system DLL, even if test media has a local copy. */
    Length = GetSystemDirectoryW(LibraryPath, MAX_PATH);
    if (!Length || Length + 12 >= MAX_PATH) return 1;
    wcscat(LibraryPath, L"\\newdev.dll");
    Library = LoadLibraryW(LibraryPath);
    if (!Library) return 1;
    Update = (UPDATE)GetProcAddress(Library, "UpdateDriverForPlugAndPlayDevicesW");
    if (!Update) { FreeLibrary(Library); return 1; }
    if (Regression)
    {
        /* This regression specifically exercises updating a running device. */
        Probe = OpenProbe();
        if (Probe == INVALID_HANDLE_VALUE) { FreeLibrary(Library); return 1; }
        CloseHandle(Probe);
        Success = Update(NULL, L"VGPUTEST\\ABSENT_DEVICE", Full,
                         INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
        Error = GetLastError();
        if (Success || Error != ERROR_NO_SUCH_DEVINST)
        { printf("FAIL: absent hardware result=%d error=%08lx\n", Success, Error); Failed = TRUE; }
        if (wcslen(Full) + 8 >= MAX_PATH) { FreeLibrary(Library); return 1; }
        wcscpy(Missing, Full);
        wcscat(Missing, L".missing");
        if (GetFileAttributesW(Missing) != INVALID_FILE_ATTRIBUTES)
        { FreeLibrary(Library); return 1; }
        Success = Update(NULL, L"PCI\\VEN_1AF4&DEV_1050", Missing,
                         INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
        Error = GetLastError();
        if (Success || Error != ERROR_FILE_NOT_FOUND)
        { printf("FAIL: missing INF result=%d error=%08lx\n", Success, Error); Failed = TRUE; }
        if (!Failed) puts("PASS: absent hardware and missing INF have distinct errors");
    }
    Success = FALSE;
    for (i = 0; i < Cycles; ++i)
    {
        Reboot = FALSE;
        Success = Update(NULL, L"PCI\\VEN_1AF4&DEV_1050", Full,
                         INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
        printf("install cycle=%lu success=%d error=%08lx reboot=%d\n", i + 1,
               Success, Success ? 0 : GetLastError(), Reboot);
        if (!Success || (Regression && !Reboot))
        { FreeLibrary(Library); return 1; }
    }
    if (Regression && !Failed) printf("PASS: %lu running-device reinstalls staged; reboot required\n", Cycles);
    FreeLibrary(Library);
    return Success && !Failed ? 0 : 1;
}

int main(int argc, char **argv)
{
    VGPU_DIAG_INFO Info, Next;
    HANDLE Device;
    DWORD Bytes, Error, i, Count = 1000;
    BOOL Success;
    BOOL RequireVenus = FALSE;
    BOOL RequireBlob = FALSE;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--install")) return Install(argv[2], 1, FALSE);
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "--install-test"))
    {
        Count = argc == 4 ? strtoul(argv[3], NULL, 10) : 5;
        if (!Count || Count > 1000) return 1;
        return Install(argv[2], Count, TRUE);
    }
    if (argc == 2 && !strcmp(argv[1], "--blob")) RequireBlob = TRUE;
    else if (argc == 2 && !strcmp(argv[1], "--venus")) RequireVenus = TRUE;
    else if (argc == 2) Count = strtoul(argv[1], NULL, 10);
    if (!Count || Count > 100000) return 1;
    Device = OpenProbe();
    if (Device == INVALID_HANDLE_VALUE)
    { printf("FAIL: open error=%lu\n", GetLastError()); return 1; }
    for (i = 0; i < 2; ++i)
    {
        Bytes = 0xdeadbeef;
        memset(&Info, 0xcc, sizeof(Info));
        Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_DIAGNOSTICS,
                                  NULL, 0, &Info, i ? sizeof(Info) - 1 : 0, &Bytes, NULL);
        Error = GetLastError();
        /* DeviceIoControl leaves Bytes unspecified on this error. */
        if (Success || Error != ERROR_INSUFFICIENT_BUFFER)
        { printf("FAIL: short buffer result=%d error=%lu bytes=%lu\n", Success, Error, Bytes); CloseHandle(Device); return 1; }
    }
    Success = DeviceIoControl(Device, CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_READ_ACCESS),
                              NULL, 0, &Info, sizeof(Info), &Bytes, NULL);
    Error = GetLastError();
    if (Success || Error != ERROR_INVALID_FUNCTION)
    { printf("FAIL: unsupported IOCTL result=%d error=%lu\n", Success, Error); CloseHandle(Device); return 1; }
    Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_DIAGNOSTICS,
                              NULL, 0, &Info, sizeof(Info), &Bytes, NULL);
    CloseHandle(Device);
    if (!Success || Bytes != sizeof(Info) || Info.Size != sizeof(Info) ||
        Info.Version != VGPU_DIAG_VERSION)
    { puts("FAIL: diagnostic schema/read"); return 1; }
    printf("stage=%lu status=%08lx reset=%08lx flags=%lx requests=%lu scanouts=%lu\n",
           Info.Stage, Info.Status, Info.ResetStatus, Info.Flags,
           Info.CompletedRequests, Info.EnabledScanouts);
    printf("host_features=%08lx%08lx negotiated=%08lx%08lx\n",
           Info.HostFeaturesHigh, Info.HostFeaturesLow, Info.GuestFeaturesHigh, Info.GuestFeaturesLow);
    if (Info.Stage != VGPU_STAGE_COMPLETE || Info.Status || Info.ResetStatus ||
        Info.Flags || Info.CompletedRequests != 1)
    { puts("FAIL: hardware probe did not complete"); return 1; }
    for (i = 0; i < VGPU_DIAG_MAX_SCANOUTS; ++i)
        if (Info.Scanouts[i].Enabled)
            printf("scanout %lu: %lux%lu at %lu,%lu flags=%lx\n", i,
                   Info.Scanouts[i].Width, Info.Scanouts[i].Height,
                   Info.Scanouts[i].X, Info.Scanouts[i].Y, Info.Scanouts[i].Flags);
    for (i = 0; i < Count; ++i)
    {
        Device = OpenProbe();
        if (Device == INVALID_HANDLE_VALUE) return 1;
        Success = DeviceIoControl(Device, IOCTL_VGPU_QUERY_DIAGNOSTICS,
                                  NULL, 0, &Next, sizeof(Next), &Bytes, NULL);
        CloseHandle(Device);
        if (!Success || Bytes != sizeof(Next) || memcmp(&Next, &Info, sizeof(Info)))
        { printf("FAIL: read/open/close cycle %lu\n", i); return 1; }
    }
    printf("PASS: fenced GET_DISPLAY_INFO; %lu diagnostic open/read/close cycles; invalid requests rejected\n", Count);
    if (CheckVenus(RequireVenus || RequireBlob)) return 1;
    return CheckBlob(RequireBlob);
}
