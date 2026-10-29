/*
 * PROJECT:     ReactOS Services
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Usermode helper that spawns a cmd.exe session over the
 *              SAC/EMS headless console whenever "cmd" is typed at the
 *              SAC> prompt. Runs as a normal auto-start Windows service.
 */

/* CancelIoEx is Vista+; this build's default _WIN32_WINNT (0x502) hides its
 * declaration even though kernel32 implements and exports it. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <drivers/sac/sacioctl.h>

#define SAC_DEVICE_PATH L"\\\\.\\SAC"
#define SERVICE_NAME L"sacsvr"
#define PUMP_BUFFER_SIZE 256
#define INPUT_POLL_MS 50

typedef struct _PUMP_CONTEXT
{
    HANDLE SacDevice;
    HANDLE ChildStdinWrite;
    HANDLE ChildStdoutRead;
    HANDLE StopEvent;
} PUMP_CONTEXT, *PPUMP_CONTEXT;

static SERVICE_STATUS ServiceStatus;
static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static HANDLE ServiceStopEvent;

static
DWORD
WINAPI
InputPumpThread(LPVOID Parameter)
{
    PPUMP_CONTEXT Context = (PPUMP_CONTEXT)Parameter;
    CHAR Buffer[PUMP_BUFFER_SIZE];
    DWORD BytesReturned, BytesWritten;

    /* Forward anything the user typed into this channel to cmd.exe's stdin */
    while (WaitForSingleObject(Context->StopEvent, INPUT_POLL_MS) == WAIT_TIMEOUT)
    {
        if (DeviceIoControl(Context->SacDevice, IOCTL_SAC_CHANNEL_READ,
                            NULL, 0, Buffer, sizeof(Buffer), &BytesReturned, NULL) &&
            (BytesReturned > 0))
        {
            if (!WriteFile(Context->ChildStdinWrite, Buffer, BytesReturned, &BytesWritten, NULL))
            {
                break;
            }
        }
    }

    return 0;
}

static
DWORD
WINAPI
OutputPumpThread(LPVOID Parameter)
{
    PPUMP_CONTEXT Context = (PPUMP_CONTEXT)Parameter;
    CHAR Buffer[PUMP_BUFFER_SIZE];
    DWORD BytesRead, BytesReturned;

    /* Forward cmd.exe's stdout/stderr into the channel for display */
    while (ReadFile(Context->ChildStdoutRead, Buffer, sizeof(Buffer), &BytesRead, NULL) &&
           (BytesRead > 0))
    {
        DeviceIoControl(Context->SacDevice, IOCTL_SAC_CHANNEL_WRITE,
                        Buffer, BytesRead, NULL, 0, &BytesReturned, NULL);
    }

    /* cmd.exe closed its stdout (it's exiting) -- stop the input pump too */
    SetEvent(Context->StopEvent);
    return 0;
}

static
BOOL
RunCmdSession(IN HANDLE SacDevice)
{
    SECURITY_ATTRIBUTES SecurityAttributes;
    HANDLE StdinRead = NULL, StdinWrite = NULL, StdoutRead = NULL, StdoutWrite = NULL;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    PUMP_CONTEXT Context;
    HANDLE InputThread, OutputThread;
    WCHAR CommandLine[] = L"cmd.exe";
    BOOL Result;

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL;
    SecurityAttributes.bInheritHandle = TRUE;

    if (!CreatePipe(&StdinRead, &StdinWrite, &SecurityAttributes, 0)) return FALSE;
    SetHandleInformation(StdinWrite, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&StdoutRead, &StdoutWrite, &SecurityAttributes, 0))
    {
        CloseHandle(StdinRead);
        CloseHandle(StdinWrite);
        return FALSE;
    }
    SetHandleInformation(StdoutRead, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = StdinRead;
    StartupInfo.hStdOutput = StdoutWrite;
    StartupInfo.hStdError = StdoutWrite;

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    Result = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL,
                            &StartupInfo, &ProcessInfo);

    /* These ends now belong to the child (or failed); we don't need them */
    CloseHandle(StdinRead);
    CloseHandle(StdoutWrite);

    if (!Result)
    {
        CloseHandle(StdinWrite);
        CloseHandle(StdoutRead);
        return FALSE;
    }

    Context.SacDevice = SacDevice;
    Context.ChildStdinWrite = StdinWrite;
    Context.ChildStdoutRead = StdoutRead;
    Context.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    InputThread = CreateThread(NULL, 0, InputPumpThread, &Context, 0, NULL);
    OutputThread = CreateThread(NULL, 0, OutputPumpThread, &Context, 0, NULL);

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    SetEvent(Context.StopEvent);

    WaitForSingleObject(InputThread, 2000);
    WaitForSingleObject(OutputThread, 2000);

    CloseHandle(InputThread);
    CloseHandle(OutputThread);
    CloseHandle(Context.StopEvent);
    CloseHandle(StdinWrite);
    CloseHandle(StdoutRead);
    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(ProcessInfo.hThread);

    return TRUE;
}

static
DWORD
WINAPI
SvcCtrlHandlerEx(DWORD Control, DWORD EventType, LPVOID EventData, LPVOID Context)
{
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);

    switch (Control)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
            SetEvent(ServiceStopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static
VOID
WINAPI
ServiceMain(DWORD Argc, LPWSTR *Argv)
{
    HANDLE SacDevice;
    HANDLE WaitDevice = INVALID_HANDLE_VALUE;
    OVERLAPPED WaitOverlapped;
    HANDLE WaitEvents[2];
    SAC_IOCTL_CHANNEL_CREATE CreateRequest;
    DWORD BytesReturned, Error, WaitResult;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, SvcCtrlHandlerEx, NULL);
    if (!ServiceStatusHandle) return;

    ZeroMemory(&ServiceStatus, sizeof(ServiceStatus));
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);

    ServiceStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    /*
     * SAC driver might initialize asynchronously or have a slight startup delay.
     * Retry connecting to \\.\SAC for up to 60 seconds, updating the SCM
     * with SERVICE_START_PENDING.
     */
    {
        DWORD CheckPoint = 1;
        SacDevice = INVALID_HANDLE_VALUE;

        while (WaitForSingleObject(ServiceStopEvent, 0) != WAIT_OBJECT_0)
        {
            ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
            ServiceStatus.dwWaitHint = 3000;
            ServiceStatus.dwCheckPoint = CheckPoint++;
            SetServiceStatus(ServiceStatusHandle, &ServiceStatus);

            SacDevice = CreateFileW(SAC_DEVICE_PATH,
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    NULL,
                                    OPEN_EXISTING,
                                    0,
                                    NULL);
            if (SacDevice != INVALID_HANDLE_VALUE)
            {
                ZeroMemory(&CreateRequest, sizeof(CreateRequest));
                lstrcpyW(CreateRequest.NameBuffer, L"Cmd0001");
                lstrcpyW(CreateRequest.DescriptionBuffer, L"Command Prompt");

                if (DeviceIoControl(SacDevice, IOCTL_SAC_CHANNEL_CREATE,
                                    &CreateRequest, sizeof(CreateRequest),
                                    NULL, 0, &BytesReturned, NULL))
                {
                    /* Successfully connected and created channel */
                    break;
                }

                CloseHandle(SacDevice);
                SacDevice = INVALID_HANDLE_VALUE;
            }

            if (CheckPoint > 60)
            {
                /* Timed out after 60 attempts (~60s) */
                break;
            }

            Sleep(1000);
        }
    }

    if (SacDevice == INVALID_HANDLE_VALUE)
    {
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        ServiceStatus.dwWin32ExitCode = GetLastError();
        ServiceStatus.dwWaitHint = 0;
        ServiceStatus.dwCheckPoint = 0;
        SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
        return;
    }

    /*
     * A dedicated overlapped handle for the idle wait only. SacDevice stays
     * synchronous for channel creation and the pump threads' I/O. Issuing
     * IOCTL_SAC_WAIT_CMD_REQUEST overlapped lets a SERVICE_CONTROL_STOP/
     * SHUTDOWN request (delivered on the SCM's own thread, which only sets
     * ServiceStopEvent) actually interrupt this thread instead of leaving it
     * blocked until someone types "cmd" -- or, if nobody ever does, forcing
     * the OS to kill the process outright during shutdown.
     */
    WaitDevice = CreateFileW(SAC_DEVICE_PATH,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED,
                             NULL);
    if (WaitDevice == INVALID_HANDLE_VALUE)
    {
        CloseHandle(SacDevice);
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        ServiceStatus.dwWin32ExitCode = GetLastError();
        ServiceStatus.dwWaitHint = 0;
        ServiceStatus.dwCheckPoint = 0;
        SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
        return;
    }

    ZeroMemory(&WaitOverlapped, sizeof(WaitOverlapped));
    WaitOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    WaitEvents[0] = WaitOverlapped.hEvent;
    WaitEvents[1] = ServiceStopEvent;

    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ServiceStatus.dwWaitHint = 0;
    ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);

    for (;;)
    {
        if (WaitForSingleObject(ServiceStopEvent, 0) == WAIT_OBJECT_0)
        {
            break;
        }

        ResetEvent(WaitOverlapped.hEvent);
        if (!DeviceIoControl(WaitDevice, IOCTL_SAC_WAIT_CMD_REQUEST,
                             NULL, 0, NULL, 0, &BytesReturned, &WaitOverlapped))
        {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING)
            {
                break;
            }

            WaitResult = WaitForMultipleObjects(2, WaitEvents, FALSE, INFINITE);
            if (WaitResult == WAIT_OBJECT_0 + 1)
            {
                /* Stop requested while idle at the SAC prompt */
                CancelIoEx(WaitDevice, &WaitOverlapped);
                WaitForSingleObject(WaitOverlapped.hEvent, 2000);
                break;
            }

            if (!GetOverlappedResult(WaitDevice, &WaitOverlapped, &BytesReturned, FALSE))
            {
                break;
            }
        }

        RunCmdSession(SacDevice);
    }

    CloseHandle(WaitOverlapped.hEvent);
    CloseHandle(WaitDevice);
    CloseHandle(SacDevice);
    ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
}

int
wmain(VOID)
{
    SERVICE_TABLE_ENTRYW ServiceTable[] =
    {
        { SERVICE_NAME, ServiceMain },
        { NULL, NULL }
    };

    StartServiceCtrlDispatcherW(ServiceTable);
    return 0;
}
