/*
 * PROJECT:     ReactOS Services
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Usermode helper that spawns a cmd.exe session over the
 *              SAC/EMS headless console whenever "cmd" is typed at the
 *              SAC> prompt. Runs as a normal auto-start Windows service.
 */

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
    SAC_IOCTL_CHANNEL_CREATE CreateRequest;
    DWORD BytesReturned;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, SvcCtrlHandlerEx, NULL);
    if (!ServiceStatusHandle) return;

    ZeroMemory(&ServiceStatus, sizeof(ServiceStatus));
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);

    ServiceStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    SacDevice = CreateFileW(SAC_DEVICE_PATH,
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);
    if (SacDevice == INVALID_HANDLE_VALUE)
    {
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
        return;
    }

    ZeroMemory(&CreateRequest, sizeof(CreateRequest));
    lstrcpyW(CreateRequest.NameBuffer, L"Cmd0001");
    lstrcpyW(CreateRequest.DescriptionBuffer, L"Command Prompt");

    if (!DeviceIoControl(SacDevice, IOCTL_SAC_CHANNEL_CREATE,
                         &CreateRequest, sizeof(CreateRequest),
                         NULL, 0, &BytesReturned, NULL))
    {
        CloseHandle(SacDevice);
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
        return;
    }

    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);

    while (WaitForSingleObject(ServiceStopEvent, 0) != WAIT_OBJECT_0)
    {
        if (!DeviceIoControl(SacDevice, IOCTL_SAC_WAIT_CMD_REQUEST,
                             NULL, 0, NULL, 0, &BytesReturned, NULL))
        {
            break;
        }

        RunCmdSession(SacDevice);
    }

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
