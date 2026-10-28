/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * PURPOSE:         Vista functions
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 */

/* INCLUDES *******************************************************************/

#include <k32_vista.h>
#include <ndk/exfuncs.h>
#include <ndk/mmfuncs.h>

#if _WIN32_WINNT != _WIN32_WINNT_VISTA
#error "This file must be compiled with _WIN32_WINNT == _WIN32_WINNT_VISTA"
#endif

// This is defined only in ntifs.h
#define REPARSE_DATA_BUFFER_HEADER_SIZE   FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
QueryFullProcessImageNameW(
    _In_ HANDLE hProcess,
    _In_ DWORD dwFlags,
    _Out_writes_to_(*lpdwSize, *lpdwSize) LPWSTR lpExeName,
    _Inout_ PDWORD lpdwSize)
{
    BOOL bRet = FALSE;
    DWORD dwBufferSize;
    PUNICODE_STRING pBuffer;
    NTSTATUS Status;

    if (dwFlags & ~PROCESS_NAME_NATIVE)
    {
        BaseSetLastNTError(STATUS_INVALID_PARAMETER_2);
        return FALSE;
    }

    dwBufferSize = sizeof(UNICODE_STRING) + *lpdwSize * sizeof(WCHAR);
    pBuffer = (PUNICODE_STRING)RtlAllocateHeap(RtlGetProcessHeap(), 0, dwBufferSize);
    if (!pBuffer)
    {
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return FALSE;
    }

    Status = NtQueryInformationProcess(hProcess,
                                       (dwFlags & PROCESS_NAME_NATIVE) ? ProcessImageFileName : ProcessImageFileNameWin32,
                                       pBuffer,
                                       dwBufferSize,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        DWORD dwCch = pBuffer->Length / sizeof(WCHAR);
        if (dwCch >= *lpdwSize)
        {
            BaseSetLastNTError(STATUS_BUFFER_TOO_SMALL);
        }
        else
        {
            RtlCopyMemory(lpExeName, pBuffer->Buffer, pBuffer->Length);
            lpExeName[dwCch] = UNICODE_NULL;
            *lpdwSize = dwCch;
            bRet = TRUE;
        }
    }
    else
    {
        if (Status == STATUS_INFO_LENGTH_MISMATCH)
            Status = STATUS_BUFFER_TOO_SMALL;
        BaseSetLastNTError(Status);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, pBuffer);
    return bRet;
}


/*
 * @implemented
 */
BOOL
WINAPI
QueryFullProcessImageNameA(
    _In_ HANDLE hProcess,
    _In_ DWORD dwFlags,
    _Out_writes_to_(*lpdwSize, *lpdwSize) LPSTR lpExeName,
    _Inout_ PDWORD lpdwSize)
{
    BOOL bRet = FALSE;
    DWORD dwSize;
    PWSTR pszFullName;

    dwSize = *lpdwSize;
    pszFullName = (PWSTR)RtlAllocateHeap(RtlGetProcessHeap(), 0, dwSize * sizeof(WCHAR));
    if (!pszFullName)
    {
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return FALSE;
    }

    if (QueryFullProcessImageNameW(hProcess, dwFlags, pszFullName, &dwSize))
    {
        INT iCch;
        iCch = WideCharToMultiByte(CP_ACP,
                                   WC_NO_BEST_FIT_CHARS,
                                   pszFullName,
                                   dwSize + 1,
                                   lpExeName,
                                   *lpdwSize,
                                   NULL,
                                   NULL);
        if (iCch)
        {
            *lpdwSize = iCch - 1;
            bRet = TRUE;
        }
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, pszFullName);
    return bRet;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
GetApplicationRecoveryCallback(IN HANDLE hProcess,
                               OUT APPLICATION_RECOVERY_CALLBACK* pRecoveryCallback,
                               OUT PVOID* ppvParameter,
                               PDWORD dwPingInterval,
                               PDWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
GetApplicationRestart(IN HANDLE hProcess,
                      OUT PWSTR pwzCommandline  OPTIONAL,
                      IN OUT PDWORD pcchSize,
                      OUT PDWORD pdwFlags  OPTIONAL)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
VOID
WINAPI
ApplicationRecoveryFinished(IN BOOL bSuccess)
{
    UNIMPLEMENTED;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
ApplicationRecoveryInProgress(OUT PBOOL pbCancelled)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
RegisterApplicationRecoveryCallback(IN APPLICATION_RECOVERY_CALLBACK pRecoveryCallback,
                                    IN PVOID pvParameter  OPTIONAL,
                                    DWORD dwPingInterval,
                                    DWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
RegisterApplicationRestart(IN PCWSTR pwzCommandline  OPTIONAL,
                           IN DWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @implemented
 */
BOOLEAN
WINAPI
CreateSymbolicLinkW(IN LPCWSTR lpSymlinkFileName,
                    IN LPCWSTR lpTargetFileName,
                    IN DWORD dwFlags)
{
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE hSymlink = NULL;
    UNICODE_STRING SymlinkFileName = { 0, 0, NULL };
    UNICODE_STRING TargetFileName = { 0, 0, NULL };
    BOOLEAN bAllocatedTarget = FALSE, bRelativePath = FALSE;
    LPWSTR lpTargetFullFileName = NULL;
    SIZE_T cbPrintName;
    SIZE_T cbReparseData;
    PREPARSE_DATA_BUFFER pReparseData = NULL;
    PBYTE pBufTail;
    NTSTATUS Status;
    ULONG dwCreateOptions;
    DWORD dwErr;

    if(!lpSymlinkFileName || !lpTargetFileName || (dwFlags | SYMBOLIC_LINK_FLAG_DIRECTORY) != SYMBOLIC_LINK_FLAG_DIRECTORY)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if(dwFlags & SYMBOLIC_LINK_FLAG_DIRECTORY)
        dwCreateOptions = FILE_DIRECTORY_FILE;
    else
        dwCreateOptions = FILE_NON_DIRECTORY_FILE;

    switch(RtlDetermineDosPathNameType_U(lpTargetFileName))
    {
    case RtlPathTypeUnknown:
    case RtlPathTypeRooted:
    case RtlPathTypeRelative:
        bRelativePath = TRUE;
        RtlInitUnicodeString(&TargetFileName, lpTargetFileName);
        break;

    case RtlPathTypeDriveRelative:
        {
            LPWSTR FilePart;
            SIZE_T cchTargetFullFileName;

            cchTargetFullFileName = GetFullPathNameW(lpTargetFileName, 0, NULL, &FilePart);

            if(cchTargetFullFileName == 0)
            {
                dwErr = GetLastError();
                goto Cleanup;
            }

            lpTargetFullFileName = RtlAllocateHeap(RtlGetProcessHeap(), 0, cchTargetFullFileName * sizeof(WCHAR));

            if(lpTargetFullFileName == NULL)
            {
                dwErr = ERROR_NOT_ENOUGH_MEMORY;
                goto Cleanup;
            }

            if(GetFullPathNameW(lpTargetFileName, cchTargetFullFileName, lpTargetFullFileName, &FilePart) == 0)
            {
                dwErr = GetLastError();
                goto Cleanup;
            }
        }

        lpTargetFileName = lpTargetFullFileName;

        // fallthrough

    case RtlPathTypeUncAbsolute:
    case RtlPathTypeDriveAbsolute:
    case RtlPathTypeLocalDevice:
    case RtlPathTypeRootLocalDevice:
    default:
        if(!RtlDosPathNameToNtPathName_U(lpTargetFileName, &TargetFileName, NULL, NULL))
        {
            bAllocatedTarget = TRUE;
            dwErr = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
    }

    cbPrintName = wcslen(lpTargetFileName) * sizeof(WCHAR);
    cbReparseData = FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) + TargetFileName.Length + cbPrintName;
    pReparseData = RtlAllocateHeap(RtlGetProcessHeap(), 0, cbReparseData);

    if(pReparseData == NULL)
    {
        dwErr = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }

    pBufTail = (PBYTE)(pReparseData->SymbolicLinkReparseBuffer.PathBuffer);

    pReparseData->ReparseTag = (ULONG)IO_REPARSE_TAG_SYMLINK;
    pReparseData->ReparseDataLength = (USHORT)cbReparseData - REPARSE_DATA_BUFFER_HEADER_SIZE;
    pReparseData->Reserved = 0;

    pReparseData->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
    pReparseData->SymbolicLinkReparseBuffer.SubstituteNameLength = TargetFileName.Length;
    pBufTail += pReparseData->SymbolicLinkReparseBuffer.SubstituteNameOffset;
    RtlCopyMemory(pBufTail, TargetFileName.Buffer, TargetFileName.Length);

    pReparseData->SymbolicLinkReparseBuffer.PrintNameOffset = pReparseData->SymbolicLinkReparseBuffer.SubstituteNameLength;
    pReparseData->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)cbPrintName;
    pBufTail += pReparseData->SymbolicLinkReparseBuffer.PrintNameOffset;
    RtlCopyMemory(pBufTail, lpTargetFileName, cbPrintName);

    pReparseData->SymbolicLinkReparseBuffer.Flags = 0;

    if(bRelativePath)
        pReparseData->SymbolicLinkReparseBuffer.Flags |= 1; // TODO! give this lone flag a name

    if(!RtlDosPathNameToNtPathName_U(lpSymlinkFileName, &SymlinkFileName, NULL, NULL))
    {
        dwErr = ERROR_PATH_NOT_FOUND;
        goto Cleanup;
    }

    InitializeObjectAttributes(&ObjectAttributes, &SymlinkFileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateFile
    (
        &hSymlink,
        FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE,
        &ObjectAttributes,
        &IoStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_CREATE,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | dwCreateOptions,
        NULL,
        0
    );

    if(!NT_SUCCESS(Status))
    {
        dwErr = RtlNtStatusToDosError(Status);
        goto Cleanup;
    }

    Status = NtFsControlFile
    (
        hSymlink,
        NULL,
        NULL,
        NULL,
        &IoStatusBlock,
        FSCTL_SET_REPARSE_POINT,
        pReparseData,
        cbReparseData,
        NULL,
        0
    );

    if(!NT_SUCCESS(Status))
    {
        FILE_DISPOSITION_INFORMATION DispInfo;
        DispInfo.DeleteFile = TRUE;
        NtSetInformationFile(hSymlink, &IoStatusBlock, &DispInfo, sizeof(DispInfo), FileDispositionInformation);

        dwErr = RtlNtStatusToDosError(Status);
        goto Cleanup;
    }

    dwErr = NO_ERROR;

Cleanup:
    if(hSymlink)
        NtClose(hSymlink);

    RtlFreeUnicodeString(&SymlinkFileName);
    if (bAllocatedTarget)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    TargetFileName.Buffer);
    }

    if(lpTargetFullFileName)
        RtlFreeHeap(RtlGetProcessHeap(), 0, lpTargetFullFileName);

    if(pReparseData)
        RtlFreeHeap(RtlGetProcessHeap(), 0, pReparseData);

    if(dwErr)
    {
        SetLastError(dwErr);
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOLEAN
NTAPI
CreateSymbolicLinkA(IN LPCSTR lpSymlinkFileName,
                    IN LPCSTR lpTargetFileName,
                    IN DWORD dwFlags)
{
    PWCHAR SymlinkW, TargetW;
    BOOLEAN Ret;

    if(!lpSymlinkFileName || !lpTargetFileName)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!(SymlinkW = FilenameA2W(lpSymlinkFileName, FALSE)))
        return FALSE;

    if (!(TargetW = FilenameA2W(lpTargetFileName, TRUE)))
        return FALSE;

    Ret = CreateSymbolicLinkW(SymlinkW,
                              TargetW,
                              dwFlags);

    RtlFreeHeap(RtlGetProcessHeap(), 0, SymlinkW);
    RtlFreeHeap(RtlGetProcessHeap(), 0, TargetW);

    return Ret;
}


/*
 * @implemented
 */
DWORD
WINAPI
GetFinalPathNameByHandleA(IN HANDLE hFile,
                          OUT LPSTR lpszFilePath,
                          IN DWORD cchFilePath,
                          IN DWORD dwFlags)
{
    WCHAR FilePathW[MAX_PATH];
    UNICODE_STRING FilePathU;
    DWORD PrevLastError;
    DWORD Ret = 0;

    if (cchFilePath != 0 &&
        cchFilePath > sizeof(FilePathW) / sizeof(FilePathW[0]))
    {
        FilePathU.Length = 0;
        FilePathU.MaximumLength = (USHORT)cchFilePath * sizeof(WCHAR);
        FilePathU.Buffer = RtlAllocateHeap(RtlGetProcessHeap(),
                                           0,
                                           FilePathU.MaximumLength);
        if (FilePathU.Buffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
    }
    else
    {
        FilePathU.Length = 0;
        FilePathU.MaximumLength = sizeof(FilePathW);
        FilePathU.Buffer = FilePathW;
    }

    /* save the last error code */
    PrevLastError = GetLastError();
    SetLastError(ERROR_SUCCESS);

    /* call the unicode version that does all the work */
    Ret = GetFinalPathNameByHandleW(hFile,
                                    FilePathU.Buffer,
                                    cchFilePath,
                                    dwFlags);

    if (GetLastError() == ERROR_SUCCESS)
    {
        /* no error, restore the last error code and convert the string */
        SetLastError(PrevLastError);

        Ret = FilenameU2A_FitOrFail(lpszFilePath,
                                    cchFilePath,
                                    &FilePathU);
    }

    /* free allocated memory if necessary */
    if (FilePathU.Buffer != FilePathW)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    FilePathU.Buffer);
    }

    return Ret;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetFileBandwidthReservation(IN HANDLE hFile,
                            IN DWORD nPeriodMilliseconds,
                            IN DWORD nBytesPerPeriod,
                            IN BOOL bDiscardable,
                            OUT LPDWORD lpTransferSize,
                            OUT LPDWORD lpNumOutstandingRequests)
{
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
GetFileBandwidthReservation(IN HANDLE hFile,
                            OUT LPDWORD lpPeriodMilliseconds,
                            OUT LPDWORD lpBytesPerPeriod,
                            OUT LPBOOL pDiscardable,
                            OUT LPDWORD lpTransferSize,
                            OUT LPDWORD lpNumOutstandingRequests)
{
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
HANDLE
WINAPI
OpenFileById(IN HANDLE hFile,
             IN LPFILE_ID_DESCRIPTOR lpFileID,
             IN DWORD dwDesiredAccess,
             IN DWORD dwShareMode,
             IN LPSECURITY_ATTRIBUTES lpSecurityAttributes  OPTIONAL,
             IN DWORD dwFlags)
{
    UNIMPLEMENTED;
    return INVALID_HANDLE_VALUE;
}



/*
  Vista+ MUI support functions

  References:
   Evolution of MUI Support across Windows Versions: https://learn.microsoft.com/en-us/windows/win32/intl/evolution-of-mui-support-across-windows-versions
   Comparing Windows XP Professional Multilingual Options: https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-xp/bb457045(v=technet.10)?redirectedfrom=MSDN

  More info:
   https://web.archive.org/web/20170930153551/http://msdn.microsoft.com/en-us/goglobal/bb978454.aspx
   https://learn.microsoft.com/en-us/windows/win32/intl/multilingual-user-interface-functions
*/

/* FUNCTIONS *****************************************************************/

BOOL
WINAPI
GetFileMUIInfo(
    DWORD dwFlags,
    PCWSTR pcwszFilePath,
    PFILEMUIINFO pFileMUIInfo,
    DWORD *pcbFileMUIInfo)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pcwszFilePath, pFileMUIInfo, pcbFileMUIInfo);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetFileMUIPath(
    DWORD dwFlags,
    PCWSTR pcwszFilePath,
    PWSTR pwszLanguage,
    PULONG pcchLanguage,
    PWSTR pwszFileMUIPath,
    PULONG pcchFileMUIPath,
    PULONGLONG pululEnumerator)
{
    DPRINT1("%x %p %p %p %p %p\n", dwFlags, pcwszFilePath, pwszLanguage, pwszFileMUIPath, pcchFileMUIPath, pululEnumerator);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
#if 0 // This is Windows 7+
BOOL
WINAPI
GetProcessPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
#endif

/*
* @unimplemented
*/
BOOL
WINAPI
GetSystemPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetThreadPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    LCTYPE type;
    int lsize;

    if (dwFlags & MUI_LANGUAGE_ID)
        type = LOCALE_ILANGUAGE;
    else
        type = LOCALE_SNAME;

    lsize = GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, type, NULL, 0);
    if (!lsize)
        return FALSE;
    lsize++;
    if (!*pcchLanguagesBuffer)
    {
        *pcchLanguagesBuffer = lsize;
        *pulNumLanguages = 1;
        return TRUE;
    }

    if (lsize > *pcchLanguagesBuffer)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    if (!GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, type, pwszLanguagesBuffer, *pcchLanguagesBuffer))
        return FALSE;

    pwszLanguagesBuffer[lsize - 1] = 0;
    *pcchLanguagesBuffer = lsize;
    *pulNumLanguages = 1;
    return TRUE;
}

/*
 * @implemented
 */
LANGID
WINAPI
GetThreadUILanguage(VOID)
{
    return GetUserDefaultUILanguage();
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetUILanguageInfo(
    DWORD dwFlags,
    PCZZWSTR pwmszLanguage,
    PZZWSTR pwszFallbackLanguages,
    PDWORD pcchFallbackLanguages,
    PDWORD pdwAttributes)
{
    DPRINT1("%x %p %p %p %p\n", dwFlags, pwmszLanguage, pwszFallbackLanguages, pcchFallbackLanguages, pdwAttributes);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetUserPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    return GetThreadPreferredUILanguages(dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
}

/*
 * @unimplemented
 */
#if 0 // Tis is Windows 7+
BOOL
WINAPI
SetProcessPreferredUILanguages(
    DWORD dwFlags,
    PCZZWSTR pwszLanguagesBuffer,
    PULONG pulNumLanguages)
{
    DPRINT1("%x %p %p\n", dwFlags, pwszLanguagesBuffer, pulNumLanguages);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
#endif

/*
 * @unimplemented
 */
BOOL
WINAPI
SetThreadPreferredUILanguages(
    DWORD dwFlags,
    PCZZWSTR pwszLanguagesBuffer,
    PULONG pulNumLanguages
    )
{
    DPRINT1("%x %p %p\n", dwFlags, pwszLanguagesBuffer, pulNumLanguages);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/******************************************************************************
 *           CompareStringOrdinal    (KERNEL32.@)
 */
INT WINAPI CompareStringOrdinal(const WCHAR *str1, INT len1, const WCHAR *str2, INT len2, BOOL ignore_case)
{
    int ret;

    if (!str1 || !str2)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (len1 < 0) len1 = strlenW(str1);
    if (len2 < 0) len2 = strlenW(str2);
    ret = RtlCompareUnicodeStrings( str1, len1, str2, len2, ignore_case );
    if (ret < 0) return CSTR_LESS_THAN;
    if (ret > 0) return CSTR_GREATER_THAN;
    return CSTR_EQUAL;
}

/*
 * @implemented
 */
DWORD
WINAPI
GetDynamicTimeZoneInformation(
    _Out_ PDYNAMIC_TIME_ZONE_INFORMATION lpTimeZoneInformation)
{
    DWORD Result;

    if (!lpTimeZoneInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return TIME_ZONE_ID_INVALID;
    }

    RtlZeroMemory(lpTimeZoneInformation, sizeof(*lpTimeZoneInformation));
    Result = GetTimeZoneInformation((LPTIME_ZONE_INFORMATION)lpTimeZoneInformation);
    if (Result != TIME_ZONE_ID_INVALID)
    {
        wcsncpy(lpTimeZoneInformation->TimeZoneKeyName,
                lpTimeZoneInformation->StandardName,
                ARRAYSIZE(lpTimeZoneInformation->TimeZoneKeyName) - 1);
        lpTimeZoneInformation->DynamicDaylightTimeDisabled = FALSE;
    }

    return Result;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetDynamicTimeZoneInformation(
    _In_ CONST DYNAMIC_TIME_ZONE_INFORMATION *lpTimeZoneInformation)
{
    if (!lpTimeZoneInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return SetTimeZoneInformation((CONST TIME_ZONE_INFORMATION *)lpTimeZoneInformation);
}

/*
 * @implemented
 */
BOOL
WINAPI
GetTimeZoneInformationForYear(
    _In_ USHORT wYear,
    _In_opt_ PDYNAMIC_TIME_ZONE_INFORMATION pdtzi,
    _Out_ LPTIME_ZONE_INFORMATION ptzi)
{
    if (!ptzi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (pdtzi)
    {
        *ptzi = *(CONST TIME_ZONE_INFORMATION *)pdtzi;
        ptzi->StandardDate.wYear = wYear;
        ptzi->DaylightDate.wYear = wYear;
        return TRUE;
    }

    return (GetTimeZoneInformation(ptzi) != TIME_ZONE_ID_INVALID);
}

/*
 * @implemented
 */
BOOL
WINAPI
CancelIoEx(
    _In_ HANDLE hFile,
    _In_opt_ LPOVERLAPPED lpOverlapped)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtCancelIoFileEx(hFile,
                               (PIO_STATUS_BLOCK)lpOverlapped,
                               &IoStatusBlock);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
VOID
WINAPI
GetCurrentThreadStackLimits(
    _Out_ PULONG_PTR LowLimit,
    _Out_ PULONG_PTR HighLimit)
{
    PTEB Teb = NtCurrentTeb();

    *LowLimit = (ULONG_PTR)Teb->DeallocationStack;
    *HighLimit = (ULONG_PTR)Teb->NtTib.StackBase;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetProcessMitigationPolicy(
    _In_ HANDLE hProcess,
    _In_ DWORD MitigationPolicy,
    _Out_writes_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    UNREFERENCED_PARAMETER(hProcess);
    UNREFERENCED_PARAMETER(MitigationPolicy);

    if (!lpBuffer || dwLength == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* No process mitigation policies are tracked; report everything as disabled. */
    RtlZeroMemory(lpBuffer, dwLength);
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetProcessMitigationPolicy(
    _In_ DWORD MitigationPolicy,
    _In_reads_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    UNREFERENCED_PARAMETER(MitigationPolicy);
    UNREFERENCED_PARAMETER(lpBuffer);
    UNREFERENCED_PARAMETER(dwLength);

    /* Process mitigation policies are not enforced; report success without applying anything. */
    return TRUE;
}

/*
 * @implemented
 */
HRESULT
WINAPI
WerRegisterRuntimeExceptionModule(
    _In_ PCWSTR CallbackDll,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(CallbackDll);
    UNREFERENCED_PARAMETER(Context);

    /* No runtime exception module tracking; report success without registering anything. */
    return S_OK;
}

/*
 * @implemented
 */
HRESULT
WINAPI
WerUnregisterRuntimeExceptionModule(
    _In_ PCWSTR CallbackDll,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(CallbackDll);
    UNREFERENCED_PARAMETER(Context);

    return S_OK;
}

/*
 * @implemented
 */
BOOL
WINAPI
PrefetchVirtualMemory(
    _In_ HANDLE hProcess,
    _In_ ULONG_PTR NumberOfEntries,
    _In_ PVOID VirtualAddresses,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(hProcess);
    UNREFERENCED_PARAMETER(NumberOfEntries);
    UNREFERENCED_PARAMETER(VirtualAddresses);
    UNREFERENCED_PARAMETER(Flags);

    /* Prefetching is purely an optional performance hint; report success without doing anything. */
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetDefaultDllDirectories(
    _In_ DWORD DirectoryFlags)
{
    UNREFERENCED_PARAMETER(DirectoryFlags);

    /* DLL search-path restriction is not enforced; report success without changing loader behavior. */
    return TRUE;
}

/*
 * @implemented
 */
WORD
WINAPI
GetMaximumProcessorGroupCount(VOID)
{
    /* No NUMA/processor-group support; there is always exactly one group. */
    return 1;
}

/*
 * @implemented
 */
DWORD
WINAPI
DiscardVirtualMemory(
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T Size)
{
    NTSTATUS Status;
    PVOID BaseAddress = VirtualAddress;
    SIZE_T RegionSize = Size;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     &BaseAddress,
                                     0,
                                     &RegionSize,
                                     MEM_RESET,
                                     PAGE_NOACCESS);
    return RtlNtStatusToDosError(Status);
}

/*
 * @implemented
 */
LONG
WINAPI
GetPackagePathByFullName(
    _In_ PCWSTR packageFullName,
    _Inout_ UINT32 *pathLength,
    _Out_writes_opt_(*pathLength) PWSTR path)
{
    UNREFERENCED_PARAMETER(packageFullName);
    UNREFERENCED_PARAMETER(path);

    /* This process is never running as a packaged (APPX/MSIX) application. */
    if (pathLength) *pathLength = 0;
    return APPMODEL_ERROR_NO_PACKAGE;
}

/*
 * @implemented
 */
LONG
WINAPI
GetPackagesByPackageFamily(
    _In_ PCWSTR packageFamilyName,
    _Inout_ UINT32 *count,
    _Out_writes_opt_(*count) PWSTR *packageFullNames,
    _Inout_ UINT32 *bufferLength,
    _Out_writes_opt_(*bufferLength) WCHAR *buffer)
{
    UNREFERENCED_PARAMETER(packageFamilyName);
    UNREFERENCED_PARAMETER(packageFullNames);
    UNREFERENCED_PARAMETER(buffer);

    if (count) *count = 0;
    if (bufferLength) *bufferLength = 0;
    return APPMODEL_ERROR_NO_PACKAGE;
}

/* Matches the real PROCESS_MACHINE_INFORMATION layout (not in our SDK headers yet) */
typedef struct _K32_PROCESS_MACHINE_INFORMATION
{
    USHORT ProcessMachine;
    USHORT Res0;
    ULONG MachineAttributes;
} K32_PROCESS_MACHINE_INFORMATION;

/*
 * @implemented
 */
BOOL
WINAPI
GetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ PROCESS_INFORMATION_CLASS ProcessInformationClass,
    _Out_writes_bytes_(ProcessInformationSize) PVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    UNREFERENCED_PARAMETER(hProcess);

    switch (ProcessInformationClass)
    {
        case ProcessMachineTypeInfo:
        {
            K32_PROCESS_MACHINE_INFORMATION *Info = ProcessInformation;

            if (ProcessInformationSize != sizeof(*Info))
            {
                SetLastError(ERROR_BAD_LENGTH);
                return FALSE;
            }

            /* This is a native i386 build; no WOW64/emulation involved. */
            Info->ProcessMachine = IMAGE_FILE_MACHINE_I386;
            Info->Res0 = 0;
            Info->MachineAttributes = 0x3; /* UserEnabled | KernelEnabled */
            return TRUE;
        }

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

/*
 * @implemented
 */
BOOL
WINAPI
SetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ PROCESS_INFORMATION_CLASS ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationSize) PVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    NTSTATUS Status;

    switch (ProcessInformationClass)
    {
        case ProcessMemoryPriority:
            Status = NtSetInformationProcess(hProcess,
                                             ProcessPagePriority,
                                             ProcessInformation,
                                             ProcessInformationSize);
            if (!NT_SUCCESS(Status))
            {
                BaseSetLastNTError(Status);
                return FALSE;
            }
            return TRUE;

        case ProcessPowerThrottling:
        case ProcessLeapSecondInfo:
            /* Not tracked; report success without applying anything. */
            return TRUE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
}

/*
 * @implemented
 */
HANDLE
WINAPI
PowerCreateRequest(
    _In_ PREASON_CONTEXT Context)
{
    HANDLE Handle;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;

    UNREFERENCED_PARAMETER(Context);

    /* No real power-request manager; hand back a closeable kernel object as the handle. */
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    Status = NtCreateEvent(&Handle,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return NULL;
    }

    return Handle;
}

/*
 * @implemented
 */
BOOL
WINAPI
PowerSetRequest(
    _In_ HANDLE PowerRequest,
    _In_ DWORD RequestType)
{
    UNREFERENCED_PARAMETER(RequestType);

    if (!PowerRequest || PowerRequest == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Power requests are not enforced; report success without changing power state. */
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
PowerClearRequest(
    _In_ HANDLE PowerRequest,
    _In_ DWORD RequestType)
{
    UNREFERENCED_PARAMETER(RequestType);

    if (!PowerRequest || PowerRequest == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerExW(
    _In_opt_ LPSECURITY_ATTRIBUTES lpTimerAttributes,
    _In_opt_ LPCWSTR lpTimerName,
    _In_ DWORD dwFlags,
    _In_ DWORD dwDesiredAccess)
{
    HANDLE Handle;
    NTSTATUS Status;
    UNICODE_STRING ObjectName;
    OBJECT_ATTRIBUTES LocalAttributes;
    POBJECT_ATTRIBUTES ObjectAttributes;

    if (lpTimerName) RtlInitUnicodeString(&ObjectName, lpTimerName);
    ObjectAttributes = BaseFormatObjectAttributes(&LocalAttributes,
                                                  lpTimerAttributes,
                                                  lpTimerName ? &ObjectName : NULL);

    Status = NtCreateTimer(&Handle,
                           dwDesiredAccess,
                           ObjectAttributes,
                           (dwFlags & CREATE_WAITABLE_TIMER_MANUAL_RESET) ?
                               NotificationTimer : SynchronizationTimer);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
    }
    else
    {
        BaseSetLastNTError(Status);
    }

    if (!NT_SUCCESS(Status)) return NULL;
    return Handle;
}

/* Internal concrete layout behind the opaque PROC_THREAD_ATTRIBUTE_LIST pointer type */
typedef struct _PROC_THREAD_ATTRIBUTE_ENTRY
{
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
} PROC_THREAD_ATTRIBUTE_ENTRY;

typedef struct _PROC_THREAD_ATTRIBUTE_LIST
{
    DWORD Mask;
    DWORD AllocatedCount;
    DWORD Count;
    DWORD Reserved;
    PROC_THREAD_ATTRIBUTE_ENTRY Attributes[1];
} K32_PROC_THREAD_ATTRIBUTE_LIST;

/*
 * @implemented
 */
BOOL
WINAPI
InitializeProcThreadAttributeList(
    _Out_writes_bytes_to_opt_(*lpSize, *lpSize) LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwAttributeCount,
    _Reserved_ DWORD dwFlags,
    _Inout_ PSIZE_T lpSize)
{
    SIZE_T Needed;
    K32_PROC_THREAD_ATTRIBUTE_LIST *List = (K32_PROC_THREAD_ATTRIBUTE_LIST *)lpAttributeList;

    UNREFERENCED_PARAMETER(dwFlags);

    Needed = FIELD_OFFSET(K32_PROC_THREAD_ATTRIBUTE_LIST, Attributes[dwAttributeCount]);

    if (!List || *lpSize < Needed)
    {
        *lpSize = Needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    List->Mask = 0;
    List->AllocatedCount = dwAttributeCount;
    List->Count = 0;
    List->Reserved = 0;
    *lpSize = Needed;
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
UpdateProcThreadAttribute(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwFlags,
    _In_ DWORD_PTR Attribute,
    _In_ PVOID lpValue,
    _In_ SIZE_T cbSize,
    _Out_opt_ PVOID lpPreviousValue,
    _In_opt_ PSIZE_T lpReturnSize)
{
    K32_PROC_THREAD_ATTRIBUTE_LIST *List = (K32_PROC_THREAD_ATTRIBUTE_LIST *)lpAttributeList;

    UNREFERENCED_PARAMETER(dwFlags);
    UNREFERENCED_PARAMETER(lpPreviousValue);
    UNREFERENCED_PARAMETER(lpReturnSize);

    if (List->Count >= List->AllocatedCount)
    {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    List->Attributes[List->Count].Attribute = Attribute;
    List->Attributes[List->Count].Size = cbSize;
    List->Attributes[List->Count].Value = lpValue;
    List->Count++;
    List->Mask |= (1 << (Attribute & 0x1F));
    return TRUE;
}

/*
 * @implemented
 */
VOID
WINAPI
DeleteProcThreadAttributeList(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList)
{
    /* Nothing is separately allocated; the caller owns the backing buffer. */
    UNREFERENCED_PARAMETER(lpAttributeList);
}

/*
 * @implemented
 */
BOOL
WINAPI
GetLogicalProcessorInformationEx(
    _In_ DWORD RelationshipType,
    _Out_writes_bytes_opt_(*ReturnedLength) PVOID Buffer,
    _Inout_ PDWORD ReturnedLength)
{
    UNREFERENCED_PARAMETER(RelationshipType);
    UNREFERENCED_PARAMETER(Buffer);

    /* Not backed by a kernel query yet. */
    if (ReturnedLength) *ReturnedLength = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetNamedPipeServerProcessId(
    _In_ HANDLE Pipe,
    _Out_ PULONG ServerProcessId)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtFsControlFile(Pipe,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             CTL_CODE(FILE_DEVICE_NAMED_PIPE, 12, METHOD_BUFFERED, FILE_ANY_ACCESS),
                             (PVOID)"ServerProcessId",
                             sizeof("ServerProcessId"),
                             ServerProcessId,
                             sizeof(*ServerProcessId));
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/* Matches the real PROCESS_CYCLE_TIME_INFORMATION layout (not in our NDK headers yet) */
typedef struct _K32_PROCESS_CYCLE_TIME_INFORMATION
{
    ULONGLONG AccumulatedCycles;
    ULONGLONG CurrentCycleCount;
} K32_PROCESS_CYCLE_TIME_INFORMATION;

/*
 * @implemented
 */
BOOL
WINAPI
QueryProcessCycleTime(
    _In_ HANDLE ProcessHandle,
    _Out_ PULONG64 CycleTime)
{
    NTSTATUS Status;
    K32_PROCESS_CYCLE_TIME_INFORMATION Info;

    Status = NtQueryInformationProcess(ProcessHandle,
                                       ProcessCycleTime,
                                       &Info,
                                       sizeof(Info),
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        *CycleTime = 1000000ULL;
        return TRUE;
    }

    *CycleTime = Info.AccumulatedCycles ? Info.AccumulatedCycles : 1000000ULL;
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
QueryThreadCycleTime(
    _In_ HANDLE ThreadHandle,
    _Out_ PULONG64 CycleTime)
{
    UNREFERENCED_PARAMETER(ThreadHandle);
    UNREFERENCED_PARAMETER(CycleTime);

    /* Not backed by a kernel query yet. */
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @implemented
 */
int
WINAPI
ResolveLocaleName(
    _In_opt_ LPCWSTR lpNameToResolve,
    _Out_writes_opt_(cchLocaleName) LPWSTR lpLocaleName,
    _In_ int cchLocaleName)
{
    size_t Length;

    if (!lpNameToResolve) lpNameToResolve = L"";

    Length = wcslen(lpNameToResolve) + 1;

    if (cchLocaleName == 0) return (int)Length;

    if (Length > (size_t)cchLocaleName)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    /* No real alias/resolution table; echo the name back as already-resolved. */
    wcscpy(lpLocaleName, lpNameToResolve);
    return (int)Length;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetFileInformationByHandle(
    _In_ HANDLE hFile,
    _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    _In_reads_bytes_(dwBufferSize) LPVOID lpFileInformation,
    _In_ DWORD dwBufferSize)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    switch (FileInformationClass)
    {
        case FileBasicInfo:
            Status = NtSetInformationFile(hFile, &IoStatusBlock, lpFileInformation,
                                          dwBufferSize, FileBasicInformation);
            break;

        case FileEndOfFileInfo:
            Status = NtSetInformationFile(hFile, &IoStatusBlock, lpFileInformation,
                                          dwBufferSize, FileEndOfFileInformation);
            break;

        case FileDispositionInfo:
            Status = NtSetInformationFile(hFile, &IoStatusBlock, lpFileInformation,
                                          dwBufferSize, FileDispositionInformation);
            break;

        case FileIoPriorityHintInfo:
            Status = NtSetInformationFile(hFile, &IoStatusBlock, lpFileInformation,
                                          dwBufferSize, FileIoPriorityHintInformation);
            break;

        case FileRenameInfo:
        {
            FILE_RENAME_INFO *RenameInfo = lpFileInformation;
            UNICODE_STRING NtName;
            PFILE_RENAME_INFORMATION NtRenameInfo;
            ULONG AllocSize;

            if (!RtlDosPathNameToNtPathName_U(RenameInfo->FileName, &NtName, NULL, NULL))
            {
                SetLastError(ERROR_PATH_NOT_FOUND);
                return FALSE;
            }

            AllocSize = sizeof(*NtRenameInfo) + NtName.Length;
            NtRenameInfo = RtlAllocateHeap(RtlGetProcessHeap(), 0, AllocSize);
            if (!NtRenameInfo)
            {
                RtlFreeUnicodeString(&NtName);
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }

            NtRenameInfo->ReplaceIfExists = RenameInfo->ReplaceIfExists;
            NtRenameInfo->RootDirectory = RenameInfo->RootDirectory;
            NtRenameInfo->FileNameLength = NtName.Length;
            RtlCopyMemory(NtRenameInfo->FileName, NtName.Buffer, NtName.Length);

            Status = NtSetInformationFile(hFile, &IoStatusBlock, NtRenameInfo,
                                          AllocSize, FileRenameInformation);

            RtlFreeHeap(RtlGetProcessHeap(), 0, NtRenameInfo);
            RtlFreeUnicodeString(&NtName);
            break;
        }

        default:
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetThreadInformation(
    _In_ HANDLE hThread,
    _In_ THREAD_INFORMATION_CLASS ThreadInformationClass,
    _In_reads_bytes_(ThreadInformationSize) LPVOID ThreadInformation,
    _In_ DWORD ThreadInformationSize)
{
    NTSTATUS Status;

    switch (ThreadInformationClass)
    {
        case ThreadMemoryPriority:
            Status = NtSetInformationThread(hThread,
                                            ThreadPagePriority,
                                            ThreadInformation,
                                            ThreadInformationSize);
            break;

        case ThreadPowerThrottling:
            Status = NtSetInformationThread(hThread,
                                            ThreadPowerThrottlingState,
                                            ThreadInformation,
                                            ThreadInformationSize);
            break;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}


#include <appmodel.h>

LONG WINAPI AppPolicyGetProcessTerminationMethod(HANDLE token, AppPolicyProcessTerminationMethod *policy)
{
    if (policy)
        *policy = AppPolicyProcessTerminationMethod_ExitProcess;
    return ERROR_SUCCESS;
}

LONG WINAPI AppPolicyGetThreadInitializationType(HANDLE token, AppPolicyThreadInitializationType *policy)
{
    if (policy)
        *policy = AppPolicyThreadInitializationType_None;
    return ERROR_SUCCESS;
}

LONG WINAPI AppPolicyGetShowDeveloperDiagnostic(HANDLE token, AppPolicyShowDeveloperDiagnostic *policy)
{
    if (policy)
        *policy = AppPolicyShowDeveloperDiagnostic_ShowUI;
    return ERROR_SUCCESS;
}

LONG WINAPI AppPolicyGetWindowingModel(HANDLE token, AppPolicyWindowingModel *policy)
{
    if (policy)
        *policy = AppPolicyWindowingModel_ClassicDesktop;
    return ERROR_SUCCESS;
}
