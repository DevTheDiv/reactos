#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI EvtClose(HANDLE Object)
{
    return TRUE;
}

__declspec(dllexport) HANDLE WINAPI EvtCreateRenderContext(DWORD ValuePathsCount, LPCWSTR *ValuePaths, DWORD Flags)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

__declspec(dllexport) BOOL WINAPI EvtNext(HANDLE ResultSet, DWORD EventsSize, HANDLE *Events, DWORD Timeout, DWORD Flags, DWORD *Returned)
{
    if (Returned) *Returned = 0;
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

__declspec(dllexport) HANDLE WINAPI EvtQuery(HANDLE Session, LPCWSTR Path, LPCWSTR Query, DWORD Flags)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
}

__declspec(dllexport) BOOL WINAPI EvtRender(HANDLE Context, HANDLE Fragment, DWORD Flags, DWORD BufferSize, PVOID Buffer, DWORD *BufferUsed, DWORD *PropertyCount)
{
    if (BufferUsed) *BufferUsed = 0;
    if (PropertyCount) *PropertyCount = 0;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
