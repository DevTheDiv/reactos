/* INCLUDES *****************************************************************/

#include <rtl_vista.h>

#undef NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

static RTL_SRWLOCK WaitAddressLock = RTL_SRWLOCK_INIT;

typedef struct _WAIT_ADDR_BLOCK
{
    LIST_ENTRY Entry;
    const volatile VOID *Address;
    PVOID CompareAddress;
    SIZE_T AddressSize;
    HANDLE WakeEvent;
} WAIT_ADDR_BLOCK, *PWAIT_ADDR_BLOCK;

static LIST_ENTRY WaitBlockList = { &WaitBlockList, &WaitBlockList };

/* Are two wait-address blocks equal in address+size? */
FORCEINLINE
BOOLEAN
WaitAddrMatch(const volatile VOID *A, const volatile VOID *B, SIZE_T Size)
{
    return A == B;
}

/* EXPORTED FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
RtlWaitOnAddress(
    _In_ const volatile VOID *Address,
    _In_ PVOID CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    WAIT_ADDR_BLOCK Block;
    NTSTATUS Status;

    if (AddressSize != 1 && AddressSize != 2 && AddressSize != 4 && AddressSize != 8)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Block, sizeof(Block));
    Block.Address = Address;
    Block.CompareAddress = CompareAddress;
    Block.AddressSize = AddressSize;

    /* Create the wake event up front: an auto-reset event "remembers" a wake
       that races with our registration, so no wakeup can ever be lost. */
    Status = NtCreateEvent(&Block.WakeEvent,
                           EVENT_ALL_ACCESS,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Register under the lock, verifying the value while registered so a
       wake racing with insertion cannot be lost. */
    RtlAcquireSRWLockExclusive(&WaitAddressLock);

    InsertTailList(&WaitBlockList, &Block.Entry);

    RtlReleaseSRWLockExclusive(&WaitAddressLock);

    /* Wait (the value may have changed before we registered: re-check) */
    for (;;)
    {
        BOOLEAN Changed = FALSE;

        RtlAcquireSRWLockExclusive(&WaitAddressLock);
        /* Re-read value vs compare */
        switch (AddressSize)
        {
        case 1: Changed = (*(volatile UCHAR *)Address != *(volatile UCHAR *)CompareAddress); break;
        case 2: Changed = (*(volatile USHORT *)Address != *(volatile USHORT *)CompareAddress); break;
        case 4: Changed = (*(volatile ULONG *)Address != *(volatile ULONG *)CompareAddress); break;
        case 8: Changed = (*(volatile ULONGLONG *)Address != *(volatile ULONGLONG *)CompareAddress); break;
        }
        RtlReleaseSRWLockExclusive(&WaitAddressLock);

        if (Changed)
        {
            /* Value already changed - a wake happened or would be spurious.
               Remove and return as if woken. */
            break;
        }

        Status = NtWaitForSingleObject(Block.WakeEvent, FALSE, Timeout);
        if (Status != STATUS_TIMEOUT)
            break;
        if (Timeout && Timeout->QuadPart != 0)
            break; /* timed out */
    }

    /* Unregister */
    RtlAcquireSRWLockExclusive(&WaitAddressLock);
    RemoveEntryList(&Block.Entry);
    RtlReleaseSRWLockExclusive(&WaitAddressLock);
    NtClose(Block.WakeEvent);

    return Status;
}

static VOID
WakeMatching(const volatile VOID *Address, BOOLEAN Single)
{
    PLIST_ENTRY Entry;
    PWAIT_ADDR_BLOCK Block;

    RtlAcquireSRWLockExclusive(&WaitAddressLock);
    Entry = WaitBlockList.Flink;
    while (Entry != &WaitBlockList)
    {
        Block = CONTAINING_RECORD(Entry, WAIT_ADDR_BLOCK, Entry);
        if (Block->Address == Address)
        {
            (void)NtSetEvent(Block->WakeEvent, NULL);
            if (Single)
                break;
        }
        Entry = Entry->Flink;
    }
    RtlReleaseSRWLockExclusive(&WaitAddressLock);
}

VOID
NTAPI
RtlWakeAddressAll(
    _In_ const volatile VOID *Address)
{
    WakeMatching(Address, FALSE);
}

VOID
NTAPI
RtlWakeAddressSingle(
    _In_ const volatile VOID *Address)
{
    WakeMatching(Address, TRUE);
}

/* EOF */
