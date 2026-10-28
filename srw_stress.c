/* SRWLock contention stress - runtime-linked (kernel32.dll GetProcAddress) */
#include <stdio.h>
#include <windows.h>

typedef void (WINAPI *PFN_AcquireSRWLockExclusive)(PSRWLOCK);
typedef void (WINAPI *PFN_ReleaseSRWLockExclusive)(PSRWLOCK);

static PFN_AcquireSRWLockExclusive pAcquire;
static PFN_ReleaseSRWLockExclusive pRelease;

static SRWLOCK g_lock;  /* initialized via RtlInitializeSRWLock-equivalent = zero */
static volatile LONG g_counter = 0;
static volatile BOOLEAN g_stop = FALSE;

#define THREADS 8
#define ITERATIONS 50000

DWORD WINAPI Worker(LPVOID param)
{
    for (int i = 0; i < ITERATIONS && !g_stop; i++)
    {
        pAcquire(&g_lock);
        g_counter++;
        for (volatile int j = 0; j < 20; j++) { }
        pRelease(&g_lock);
    }
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pAcquire = (PFN_AcquireSRWLockExclusive)GetProcAddress(k32, "AcquireSRWLockExclusive");
    pRelease = (PFN_ReleaseSRWLockExclusive)GetProcAddress(k32, "ReleaseSRWLockExclusive");
    if (!pAcquire || !pRelease)
    {
        printf("SRW APIs not found in kernel32 (%p/%p) - ReactOS build lacks them for i386 userland\n",
               (void*)pAcquire, (void*)pRelease);
        return 2;
    }
    ZeroMemory(&g_lock, sizeof(g_lock)); /* zeroed SRWLOCK = initialized */
    printf("SRW stress: %d threads x %d iterations\n", THREADS, ITERATIONS);

    HANDLE threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        threads[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);

    DWORD expected = (DWORD)THREADS * ITERATIONS;
    for (int sec = 1; sec <= 30; sec++)
    {
        Sleep(1000);
        LONG done = InterlockedCompareExchange(&g_counter, 0, 0);
        printf("t=%ds counter=%ld / %lu\n", sec, done, expected);
        if ((ULONG)done >= expected)
        {
            printf("COMPLETED - SRW OK (%.0f ops/sec avg)\n", (double)done / sec);
            break;
        }
    }
    LONG final = InterlockedCompareExchange(&g_counter, 0, 0);
    if ((ULONG)final < expected)
        printf("STALL DETECTED: counter=%ld < %lu (SRW race!)\n", final, expected);
    return 0;
}
