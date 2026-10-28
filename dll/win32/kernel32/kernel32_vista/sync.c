#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

VOID
WINAPI
AcquireSRWLockExclusive(PSRWLOCK Lock)
{
    RtlAcquireSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
AcquireSRWLockShared(PSRWLOCK Lock)
{
    RtlAcquireSRWLockShared((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlInitializeConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}

VOID
WINAPI
InitializeSRWLock(PSRWLOCK Lock)
{
    RtlInitializeSRWLock((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
ReleaseSRWLockExclusive(PSRWLOCK Lock)
{
    RtlReleaseSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID
WINAPI
ReleaseSRWLockShared(PSRWLOCK Lock)
{
    RtlReleaseSRWLockShared((PRTL_SRWLOCK)Lock);
}

FORCEINLINE
PLARGE_INTEGER
GetNtTimeout(PLARGE_INTEGER Time, DWORD Timeout)
{
    if (Timeout == INFINITE) return NULL;
    Time->QuadPart = (ULONGLONG)Timeout * -10000;
    return Time;
}

BOOL
WINAPI
SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection, DWORD Timeout)
{
    NTSTATUS Status;
    LARGE_INTEGER Time;

    Status = RtlSleepConditionVariableCS(ConditionVariable, (PRTL_CRITICAL_SECTION)CriticalSection, GetNtTimeout(&Time, Timeout));
    if (!NT_SUCCESS(Status) || Status == STATUS_TIMEOUT)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK Lock, DWORD Timeout, ULONG Flags)
{
    NTSTATUS Status;
    LARGE_INTEGER Time;

    Status = RtlSleepConditionVariableSRW(ConditionVariable, Lock, GetNtTimeout(&Time, Timeout), Flags);
    if (!NT_SUCCESS(Status) || Status == STATUS_TIMEOUT)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

VOID
WINAPI
WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlWakeAllConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}

VOID
WINAPI
WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    RtlWakeConditionVariable((PRTL_CONDITION_VARIABLE)ConditionVariable);
}


/*
* @implemented
*/
BOOL WINAPI InitializeCriticalSectionEx(OUT LPCRITICAL_SECTION lpCriticalSection,
                                        IN DWORD dwSpinCount,
                                        IN DWORD flags)
{
    NTSTATUS Status;

    /* FIXME: Flags ignored */

    /* Initialize the critical section */
    Status = RtlInitializeCriticalSectionAndSpinCount(
        (PRTL_CRITICAL_SECTION)lpCriticalSection,
        dwSpinCount);
    if (!NT_SUCCESS(Status))
    {
        /* Set failure code */
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    /* Success */
    return TRUE;
}

#define NUM_SYNC_BUCKETS 64
static RTL_CRITICAL_SECTION g_SyncLocks[NUM_SYNC_BUCKETS];
static RTL_CONDITION_VARIABLE g_SyncCVs[NUM_SYNC_BUCKETS];
static volatile LONG g_SyncInit = 0;

static VOID EnsureSyncInit(VOID)
{
    if (InterlockedCompareExchange(&g_SyncInit, 1, 0) == 0)
    {
        ULONG i;
        for (i = 0; i < NUM_SYNC_BUCKETS; i++)
        {
            RtlInitializeCriticalSection(&g_SyncLocks[i]);
            RtlInitializeConditionVariable(&g_SyncCVs[i]);
        }
        InterlockedExchange(&g_SyncInit, 2);
    }
    else
    {
        while (g_SyncInit != 2)
            SwitchToThread();
    }
}

static inline ULONG_PTR SyncHash(const void *addr)
{
    return (((ULONG_PTR)addr >> 3) ^ ((ULONG_PTR)addr >> 9)) % NUM_SYNC_BUCKETS;
}

BOOL WINAPI WaitOnAddress(volatile VOID *Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds)
{
    ULONG_PTR bucket;
    LARGE_INTEGER Time;
    NTSTATUS Status;

    if (AddressSize != 1 && AddressSize != 2 && AddressSize != 4 && AddressSize != 8)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    EnsureSyncInit();
    bucket = SyncHash((const void*)Address);

    RtlEnterCriticalSection(&g_SyncLocks[bucket]);

    if (memcmp((const void*)Address, CompareAddress, AddressSize) != 0)
    {
        RtlLeaveCriticalSection(&g_SyncLocks[bucket]);
        return TRUE;
    }

    Status = RtlSleepConditionVariableCS(&g_SyncCVs[bucket], &g_SyncLocks[bucket], GetNtTimeout(&Time, dwMilliseconds));
    RtlLeaveCriticalSection(&g_SyncLocks[bucket]);

    if (Status == STATUS_TIMEOUT)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    else if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

VOID WINAPI WakeByAddressAll(PVOID Address)
{
    ULONG_PTR bucket;
    EnsureSyncInit();
    bucket = SyncHash((const void*)Address);
    RtlWakeAllConditionVariable(&g_SyncCVs[bucket]);
}

VOID WINAPI WakeByAddressSingle(PVOID Address)
{
    ULONG_PTR bucket;
    EnsureSyncInit();
    bucket = SyncHash((const void*)Address);
    RtlWakeConditionVariable(&g_SyncCVs[bucket]);
}

