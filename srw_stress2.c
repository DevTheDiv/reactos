/* SRW stress v2: mixed shared/exclusive + long-held locks + syscall-under-lock pattern */
#include <stdio.h>
#include <windows.h>

typedef void (WINAPI *PFN_AcquireSRWLockShared)(PSRWLOCK);
typedef void (WINAPI *PFN_ReleaseSRWLockShared)(PSRWLOCK);
typedef void (WINAPI *PFN_AcquireSRWLockExclusive)(PSRWLOCK);
typedef void (WINAPI *PFN_ReleaseSRWLockExclusive)(PSRWLOCK);

static PFN_AcquireSRWLockShared pAcqS;
static PFN_ReleaseSRWLockShared pRelS;
static PFN_AcquireSRWLockExclusive pAcqX;
static PFN_ReleaseSRWLockExclusive pRelX;

static SRWLOCK g_lock;      /* the shared data lock */
static SRWLOCK g_ipc_lock;  /* simulates chrome's "IPC under lock" pattern */
static volatile LONG g_counter = 0;
static volatile BOOLEAN g_stop = FALSE;

/* Pattern A: shared readers that hold a bit and read */
DWORD WINAPI Reader(LPVOID param)
{
    while (!g_stop)
    {
        pAcqS(&g_lock);
        volatile LONG v = g_counter;
        for (volatile int j = 0; j < 50; j++) { }
        pRelS(&g_lock);
        Sleep(0);
    }
    return 0;
}

/* Pattern B: exclusive writer, SHORT hold (pure counter update) */
DWORD WINAPI WriterShort(LPVOID param)
{
    while (!g_stop)
    {
        pAcqX(&g_lock);
        g_counter++;
        pRelX(&g_lock);
        Sleep(0);
    }
    return 0;
}

/* Pattern C: THE KILLER - acquire exclusive, then WAIT ON A SYSCALL while held
   (this is what chrome does: holds base::Lock then calls blocking IPC/event wait).
   Hold for ~5ms each round via Sleep (non-alertable syscall wait under lock). */
DWORD WINAPI WriterWithSyscall(LPVOID param)
{
    while (!g_stop)
    {
        pAcqX(&g_lock);
        g_counter++;
        Sleep(5);   /* blocking syscall-ish wait while holding the lock */
        pRelX(&g_lock);
    }
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pAcqS = (PFN_AcquireSRWLockShared)GetProcAddress(k32, "AcquireSRWLockShared");
    pRelS = (PFN_ReleaseSRWLockShared)GetProcAddress(k32, "ReleaseSRWLockShared");
    pAcqX = (PFN_AcquireSRWLockExclusive)GetProcAddress(k32, "AcquireSRWLockExclusive");
    pRelX = (PFN_ReleaseSRWLockExclusive)GetProcAddress(k32, "ReleaseSRWLockExclusive");
    if (!pAcqS || !pRelS || !pAcqX || !pRelX)
    {
        printf("SRW shared APIs missing\n");
        return 2;
    }
    ZeroMemory(&g_lock, sizeof(g_lock));
    ZeroMemory(&g_ipc_lock, sizeof(g_ipc_lock));

    printf("SRW stress v2: 4 readers + 2 short writers + 2 syscall-holders\n");
    HANDLE threads[8];
    threads[0] = CreateThread(NULL, 0, Reader, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, Reader, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, Reader, NULL, 0, NULL);
    threads[3] = CreateThread(NULL, 0, Reader, NULL, 0, NULL);
    threads[4] = CreateThread(NULL, 0, WriterShort, NULL, 0, NULL);
    threads[5] = CreateThread(NULL, 0, WriterShort, NULL, 0, NULL);
    threads[6] = CreateThread(NULL, 0, WriterWithSyscall, NULL, 0, NULL);
    threads[7] = CreateThread(NULL, 0, WriterWithSyscall, NULL, 0, NULL);

    LONG last = 0;
    for (int sec = 1; sec <= 25; sec++)
    {
        Sleep(1000);
        LONG done = InterlockedCompareExchange(&g_counter, 0, 0);
        printf("t=%ds counter=%ld (delta=%ld)\n", sec, done, done - last);
        last = done;
    }
    g_stop = TRUE;
    /* let the syscall-holder threads finish their sleep to release */
    Sleep(6000);
    LONG final = InterlockedCompareExchange(&g_counter, 0, 0);
    printf("final counter=%ld, %s\n", final,
           final > 1000 ? "PROGRESSED OK" : "STALLED BADLY");
    return 0;
}
