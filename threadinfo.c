#include <stdio.h>
#include <windows.h>
#include <winternl.h>

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

typedef LONG KPRIORITY;

/* Match ReactOS ndk/extypes.h SYSTEM_THREAD_INFORMATION */
typedef struct _ROSI_SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    ULONG ClientIdUniqueProcess;
    ULONG ClientIdUniqueThread;
    KPRIORITY Priority;
    LONG BasePriority;
    ULONG ContextSwitches;
    ULONG ThreadState;
    ULONG WaitReason;
} ROSI_SYS_THREAD_INFO;

typedef struct _ROSI_SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR PageDirectoryBase;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    /* SYSTEM_THREAD_INFORMATION Threads[1]; follows */
} ROSI_SYS_PROCESS_INFO;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered: survive hangs */
    ULONG bufSize = 1024 * 1024;
    PVOID buf = malloc(bufSize);
    ULONG len = 0;
    NTSTATUS status;
    int retries = 0;

    for (;;)
    {
        status = NtQuerySystemInformation(SystemProcessInformation, buf, bufSize, &len);
        if (status == STATUS_SUCCESS) break;
        if (status != STATUS_INFO_LENGTH_MISMATCH && !NT_SUCCESS(status))
        {
            printf("NtQuerySystemInformation failed 0x%08lx\n", (ULONG)status);
            free(buf);
            return 1;
        }
        if (++retries > 10)
        {
            printf("too many retries\n");
            free(buf);
            return 1;
        }
        bufSize = len + 4096;
        free(buf);
        buf = malloc(bufSize);
    }

    PUCHAR Current = (PUCHAR)buf;
    for (;;)
    {
        ROSI_SYS_PROCESS_INFO *spi = (ROSI_SYS_PROCESS_INFO *)Current;
        char nm[64]; nm[0] = 0;
        if (spi->ImageName.Length && spi->ImageName.Buffer)
            WideCharToMultiByte(CP_UTF8, 0, spi->ImageName.Buffer,
                                spi->ImageName.Length / 2, nm, sizeof(nm)-1, NULL, NULL);

        printf("PROC pid=%p name=%s threads=%lu\n",
               (void*)spi->UniqueProcessId, nm, spi->NumberOfThreads);
        fflush(stdout);

        /* Threads immediately follow the fixed part (no padding: ULONG alignment) */
        ROSI_SYS_THREAD_INFO *th = (ROSI_SYS_THREAD_INFO *)((PUCHAR)spi + sizeof(ROSI_SYS_PROCESS_INFO));

        if (spi->NumberOfThreads >= 15 || strstr(nm, "chrome"))
        {
            printf("=== DETAIL pid=%p name=%s threads=%lu ===\n",
                   (void*)spi->UniqueProcessId, nm, spi->NumberOfThreads);
            for (ULONG i = 0; i < spi->NumberOfThreads; i++)
            {
                printf("  TRY T%04lx start=%p state=%lu wait=%lu\n",
                       th[i].ClientIdUniqueThread, th[i].StartAddress,
                       th[i].ThreadState, th[i].WaitReason);
                fflush(stdout);
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                            THREAD_QUERY_INFORMATION, FALSE,
                                            th[i].ClientIdUniqueThread);
                ULONG eip = 0, esp = 0, ok2 = 0;
                if (hThread)
                {
                    printf("      opened, suspending...\n");
                    fflush(stdout);
                    if (SuspendThread(hThread) != (DWORD)-1)
                    {
                        printf("      suspended, getting context...\n");
                        fflush(stdout);
                        CONTEXT ctx;
                        ZeroMemory(&ctx, sizeof(ctx));
                        ctx.ContextFlags = CONTEXT_FULL;
                        if (GetThreadContext(hThread, &ctx))
                        {
                            eip = ctx.Eip;
                            esp = ctx.Esp;
                            ok2 = 1;
                            printf("      regs eax=%p ebx=%p ecx=%p edx=%p esi=%p edi=%p ebp=%p\n",
                                   (PVOID)ctx.Eax, (PVOID)ctx.Ebx, (PVOID)ctx.Ecx,
                                   (PVOID)ctx.Edx, (PVOID)ctx.Esi, (PVOID)ctx.Edi,
                                   (PVOID)ctx.Ebp);
                            /* mini stack scan */
                            printf("      stack:");
                            for (ULONG w = 0; w < 16; w++)
                            {
                                ULONG val = 0;
                                SIZE_T rd = 0;
                                if (ReadProcessMemory(GetCurrentProcess(),
                                                      (PVOID)(ctx.Esp + w * 4),
                                                      &val, 4, &rd) && rd == 4)
                                    printf(" %08lx", (unsigned long)val);
                            }
                            printf("\n");
                        }
                        ResumeThread(hThread);
                        printf("      resumed, eip=%p\n", (PVOID)(ULONG_PTR)eip);
                    }
                    else
                    {
                        printf("      SuspendThread FAILED\n");
                    }
                    CloseHandle(hThread);
                }
                else
                {
                    printf("      OpenThread FAILED\n");
                }
                fflush(stdout);
            }
            fflush(stdout);
        }

        if (!spi->NextEntryOffset) break;
        Current += spi->NextEntryOffset;
    }
    free(buf);
    return 0;
}
