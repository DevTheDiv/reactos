/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for FLS implementation details
 * COPYRIGHT:   Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 */

#include "precomp.h"
#include <ndk/pstypes.h>
#include <ndk/rtlfuncs.h>

#include <pseh/pseh2.h>

/* XP does not have these functions */
static DWORD (WINAPI *pFlsAlloc)(PFLS_CALLBACK_FUNCTION);
static BOOL (WINAPI *pFlsFree)(DWORD);
static PVOID (WINAPI *pFlsGetValue)(DWORD);
static BOOL (WINAPI *pFlsSetValue)(DWORD,PVOID);
static BOOL (WINAPI *pRtlIsCriticalSectionLockedByThread)(RTL_CRITICAL_SECTION *);


#define NtCurrentPeb() (NtCurrentTeb()->ProcessEnvironmentBlock)
#define WINVER_2003    0x0502

static DWORD g_WinVersion = 0;
PVOID g_FlsData1 = NULL;
LONG g_FlsCalled1 = 0;
PVOID g_FlsData2 = NULL;
LONG g_FlsCalled2 = 0;
PVOID g_FlsData3 = NULL;
LONG g_FlsCalled3 = 0;
BOOL g_FlsExcept3 = FALSE;


VOID WINAPI FlsCallback1(PVOID lpFlsData)
{
    ok(lpFlsData == g_FlsData1, "Expected g_FlsData1(%p), got %p\n", g_FlsData1, lpFlsData);
    InterlockedIncrement(&g_FlsCalled1);
}

VOID WINAPI FlsCallback2(PVOID lpFlsData)
{
    ok(lpFlsData == g_FlsData2, "Expected g_FlsData2(%p), got %p\n", g_FlsData2, lpFlsData);
    InterlockedIncrement(&g_FlsCalled2);
}

VOID WINAPI FlsCallback3(PVOID lpFlsData)
{
    ok(lpFlsData == g_FlsData3, "Expected g_FlsData3(%p), got %p\n", g_FlsData3, lpFlsData);

    if (g_WinVersion <= WINVER_2003)
        ok(pRtlIsCriticalSectionLockedByThread(NtCurrentPeb()->FastPebLock), "Expected lock on PEB\n");
    InterlockedIncrement(&g_FlsCalled3);
    if (g_FlsExcept3)
    {
        RaiseException(ERROR_INVALID_PARAMETER, EXCEPTION_NONCONTINUABLE, 0, NULL);
    }
}

typedef struct _FLS_CALLBACK_INFO
{
    PFLS_CALLBACK_FUNCTION lpCallback;
    PVOID Unknown;
} FLS_CALLBACK_INFO, *PFLS_CALLBACK_INFO;


void ok_fls_(DWORD dwIndex, PVOID pValue, PFLS_CALLBACK_FUNCTION lpCallback)
{
    PFLS_CALLBACK_INFO FlsCallback;
    PVOID* FlsData;
    PVOID gotValue;

    FlsCallback = (PFLS_CALLBACK_INFO)NtCurrentPeb()->FlsCallback;
    FlsData = (PVOID*)NtCurrentTeb()->FlsData;

    winetest_ok(FlsData != NULL, "Expected FlsData\n");
    winetest_ok(FlsCallback != NULL, "Expected FlsCallback\n");

    if (FlsData == NULL || FlsCallback == NULL)
    {
        winetest_skip("Unable to continue test\n");
        return;
    }

    if (g_WinVersion <= WINVER_2003)
    {
        winetest_ok(NtCurrentPeb()->FlsCallback[dwIndex] == lpCallback,
                    "Expected NtCurrentPeb()->FlsCallback[%lu] to be %p, was %p\n",
                    dwIndex,
                    lpCallback,
                    NtCurrentPeb()->FlsCallback[dwIndex]);
    }
    else
    {
        winetest_ok(FlsCallback[dwIndex].lpCallback == lpCallback,
                    "Expected FlsCallback[%lu].lpCallback to be %p, was %p\n",
                    dwIndex,
                    lpCallback,
                    FlsCallback[dwIndex].lpCallback);
        if (lpCallback != &FlsCallback3 || !g_FlsExcept3)
        {
            winetest_ok(FlsCallback[dwIndex].Unknown == NULL,
                        "Expected FlsCallback[%lu].Unknown to be %p, was %p\n",
                        dwIndex,
                        NULL,
                        FlsCallback[dwIndex].Unknown);
        }
    }
    winetest_ok(FlsData[dwIndex + 2] == pValue,
                "Expected FlsData[%lu + 2] to be %p, was %p\n",
                dwIndex,
                pValue,
                FlsData[dwIndex + 2]);

    gotValue = pFlsGetValue(dwIndex);
    winetest_ok(gotValue == pValue, "Expected FlsGetValue(%lu) to be %p, was %p\n", dwIndex, pValue, gotValue);
}

#define ok_fls         (winetest_set_location(__FILE__, __LINE__), 0) ? (void)0 : ok_fls_

static VOID init_funcs(void)
{
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    HMODULE hNTDLL = GetModuleHandleA("ntdll.dll");

#define X(f) p##f = (void*)GetProcAddress(hKernel32, #f);
    X(FlsAlloc);
    X(FlsFree);
    X(FlsGetValue);
    X(FlsSetValue);
#undef X
    pRtlIsCriticalSectionLockedByThread = (void*)GetProcAddress(hNTDLL, "RtlIsCriticalSectionLockedByThread");
}



static DWORD RundownIndices[2];
static LONG RundownCalls[2];
static PVOID RundownMainFiber;

static VOID WINAPI RundownCallback1(PVOID value)
{
    ok(value == (PVOID)0x1111, "Unexpected first callback value %p\n", value);
    InterlockedIncrement(&RundownCalls[0]);
}

static VOID WINAPI RundownCallback2(PVOID value)
{
    ok(value == (PVOID)0x2222, "Unexpected second callback value %p\n", value);
    InterlockedIncrement(&RundownCalls[1]);
}

static DWORD WINAPI RundownThread(PVOID context)
{
    ok(pFlsSetValue(RundownIndices[0], (PVOID)0x1111), "First FlsSetValue failed\n");
    ok(pFlsSetValue(RundownIndices[1], (PVOID)0x2222), "Second FlsSetValue failed\n");
    return 0;
}

static VOID WINAPI RundownFiber(PVOID context)
{
    RundownThread(context);
    SwitchToFiber(RundownMainFiber);
}

static VOID test_fls_rundown(VOID)
{
    HANDLE thread;
    PVOID fiber;
    DWORD result;

    RundownIndices[0] = pFlsAlloc(RundownCallback1);
    RundownIndices[1] = pFlsAlloc(RundownCallback2);
    if (RundownIndices[0] == FLS_OUT_OF_INDEXES || RundownIndices[1] == FLS_OUT_OF_INDEXES)
    {
        skip("Cannot allocate FLS rundown indices\n");
        if (RundownIndices[0] != FLS_OUT_OF_INDEXES) pFlsFree(RundownIndices[0]);
        if (RundownIndices[1] != FLS_OUT_OF_INDEXES) pFlsFree(RundownIndices[1]);
        return;
    }

    thread = CreateThread(NULL, 0, RundownThread, NULL, 0, NULL);
    ok(thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (thread)
    {
        result = WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
        ok(result == WAIT_OBJECT_0, "Thread rundown returned %lu\n", result);
        if (result != WAIT_OBJECT_0) return;
        ok_int(RundownCalls[0], 1);
        ok_int(RundownCalls[1], 1);
    }

    RundownMainFiber = ConvertThreadToFiber(NULL);
    ok(RundownMainFiber != NULL, "ConvertThreadToFiber failed: %lu\n", GetLastError());
    if (RundownMainFiber)
    {
        RundownCalls[0] = RundownCalls[1] = 0;
        fiber = CreateFiber(0, RundownFiber, NULL);
        ok(fiber != NULL, "CreateFiber failed: %lu\n", GetLastError());
        if (fiber)
        {
            SwitchToFiber(fiber);
            DeleteFiber(fiber);
            ok_int(RundownCalls[0], 1);
            ok_int(RundownCalls[1], 1);
        }
        ok(ConvertFiberToThread(), "ConvertFiberToThread failed: %lu\n", GetLastError());
    }
    pFlsFree(RundownIndices[0]);
    pFlsFree(RundownIndices[1]);
}

START_TEST(FLS)
{
    RTL_OSVERSIONINFOW rtlinfo = { sizeof(rtlinfo) };
    DWORD dwIndex1, dwIndex2, dwIndex3, dwErr;
    BOOL bRet;

    init_funcs();
    if (!pFlsAlloc || !pFlsFree || !pFlsGetValue || !pFlsSetValue)
    {
        skip("Fls functions not available\n");
        return;
    }
    if (!pRtlIsCriticalSectionLockedByThread)
    {
        skip("RtlIsCriticalSectionLockedByThread function not available\n");
        return;
    }

    test_fls_rundown();

    RtlGetVersion(&rtlinfo);
    g_WinVersion = (rtlinfo.dwMajorVersion << 8) | rtlinfo.dwMinorVersion;

    dwIndex1 = pFlsAlloc(FlsCallback1);
    ok(dwIndex1 != FLS_OUT_OF_INDEXES, "Unable to allocate FLS index\n");
    dwIndex2 = pFlsAlloc(FlsCallback2);
    ok(dwIndex2 != FLS_OUT_OF_INDEXES, "Unable to allocate FLS index\n");
    ok(dwIndex1 != dwIndex2, "Expected different indexes, got %lu\n", dwIndex1);

    dwIndex3 = pFlsAlloc(FlsCallback3);
    ok(dwIndex3 != FLS_OUT_OF_INDEXES, "Unable to allocate FLS index\n");
    ok(dwIndex1 != dwIndex3, "Expected different indexes, got %lu\n", dwIndex1);

    if (dwIndex1 == FLS_OUT_OF_INDEXES || dwIndex2 == FLS_OUT_OF_INDEXES || dwIndex3 == FLS_OUT_OF_INDEXES)
    {
        skip("Unable to continue test\n");
        return;
    }

    ok_fls(dwIndex1, g_FlsData1, &FlsCallback1);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);

    g_FlsData1 = (PVOID)0x123456;
    ok(pFlsSetValue(dwIndex1, g_FlsData1), "FlsSetValue(%lu, %p) failed\n", dwIndex1, g_FlsData1);

    ok_fls(dwIndex1, g_FlsData1, &FlsCallback1);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);

    ok_int(g_FlsCalled1, 0);
    ok_int(g_FlsCalled2, 0);
    ok_int(g_FlsCalled3, 0);

    g_FlsData2 = (PVOID)0x9876112;
    ok(pFlsSetValue(dwIndex2, g_FlsData2), "FlsSetValue(%lu, %p) failed\n", dwIndex2, g_FlsData2);

    ok_fls(dwIndex1, g_FlsData1, &FlsCallback1);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);


    ok_int(g_FlsCalled1, 0);
    ok_int(g_FlsCalled2, 0);
    ok_int(g_FlsCalled3, 0);

    g_FlsData3 = (PVOID)0x98762;
    ok(pFlsSetValue(dwIndex3, g_FlsData3), "FlsSetValue(%lu, %p) failed\n", dwIndex3, g_FlsData3);

    ok_fls(dwIndex1, g_FlsData1, &FlsCallback1);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);

    ok_int(g_FlsCalled1, 0);
    ok_int(g_FlsCalled2, 0);
    ok_int(g_FlsCalled3, 0);

    ok(pFlsFree(dwIndex1) == TRUE, "FlsFree(%lu) failed\n", dwIndex1);
    g_FlsData1 = NULL;

    ok_fls(dwIndex1, g_FlsData1, NULL);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);

    ok_int(g_FlsCalled1, 1);
    ok_int(g_FlsCalled2, 0);
    ok_int(g_FlsCalled3, 0);

    g_FlsExcept3 = TRUE;
    _SEH2_TRY
    {
        bRet = pFlsFree(dwIndex3);
        dwErr = GetLastError();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        bRet = 12345;
        dwErr = 0xdeaddead;
    }
    _SEH2_END;
    ok(pRtlIsCriticalSectionLockedByThread(NtCurrentPeb()->FastPebLock) == FALSE, "Expected no lock on PEB\n");

    ok(bRet == 12345, "FlsFree(%lu) should have failed, got %u\n", dwIndex3, bRet);
    ok(dwErr == 0xdeaddead, "Expected GetLastError() to be 0xdeaddead, was %lx\n", dwErr);

    ok_fls(dwIndex1, g_FlsData1, NULL);
    ok_fls(dwIndex2, g_FlsData2, &FlsCallback2);
    ok_fls(dwIndex3, g_FlsData3, &FlsCallback3);

    ok_int(g_FlsCalled1, 1);
    ok_int(g_FlsCalled2, 0);
    ok_int(g_FlsCalled3, 1);
}
