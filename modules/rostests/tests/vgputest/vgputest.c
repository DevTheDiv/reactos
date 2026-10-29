/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/vgpu_diag.h>

static HANDLE OpenProbe(void)
{
    return CreateFileW(L"\\\\.\\VgpuDiag", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static int Install(const char *Path)
{
    typedef BOOL (WINAPI *UPDATE)(HWND, LPCWSTR, LPCWSTR, DWORD, PBOOL);
    WCHAR Input[MAX_PATH], Full[MAX_PATH];
    HMODULE Library;
    UPDATE Update;
    BOOL Reboot = FALSE, Success;
    DWORD Length;
    if (!MultiByteToWideChar(CP_ACP, 0, Path, -1, Input, MAX_PATH)) return 1;
    Length = GetFullPathNameW(Input, MAX_PATH, Full, NULL);
    if (!Length || Length >= MAX_PATH) return 1;
    Library = LoadLibraryW(L"newdev.dll");
    if (!Library) return 1;
    Update = (UPDATE)GetProcAddress(Library, "UpdateDriverForPlugAndPlayDevicesW");
    if (!Update) { FreeLibrary(Library); return 1; }
    Success = Update(NULL, L"PCI\\VEN_1AF4&DEV_1050", Full, 1 | 4, &Reboot);
    printf("install success=%d error=%lu reboot=%d\n", Success,
           Success ? 0 : GetLastError(), Reboot);
    FreeLibrary(Library);
    return Success ? 0 : 1;
}

int main(int argc, char **argv)
{
    VGPU_DIAG_INFO Info, Next;
    HANDLE Device;
    DWORD Bytes, Error, i, Count = 1000;
    BOOL Success;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--install")) return Install(argv[2]);
    if (argc == 2) Count = strtoul(argv[1], NULL, 10);
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
    return 0;
}
