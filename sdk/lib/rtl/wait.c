/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Rtl user wait functions
 * FILE:              lib/rtl/wait.c
 * PROGRAMERS:
 *                    Alex Ionescu (alex@relsoft.net)
 *                    Eric Kohl
 *                    KJK::Hyperion
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTLP_WAIT
{
    HANDLE Object;
    BOOLEAN CallbackInProgress;
    HANDLE CancelEvent;
    LONG DeleteCount;
    HANDLE CompletionEvent;
    ULONG Flags;
    WAITORTIMERCALLBACKFUNC Callback;
    PVOID Context;
    ULONG Milliseconds;
} RTLP_WAIT, *PRTLP_WAIT;

/* PRIVATE FUNCTIONS *******************************************************/

static inline PLARGE_INTEGER get_nt_timeout( PLARGE_INTEGER pTime, ULONG timeout )
{
    if (timeout == INFINITE) return NULL;
    pTime->QuadPart = (ULONGLONG)timeout * -10000;
    return pTime;
}

/* The worker and deregistering thread each own one reference. Publish the
 * completion handle before dropping the latter reference. Whichever thread
 * finishes last signals it, including when a one-shot callback has already
 * completed before deregistration begins. */
static BOOLEAN
RtlpReleaseWait(PRTLP_WAIT Wait)
{
    HANDLE CompletionEvent;

    if (InterlockedIncrement(&Wait->DeleteCount) != 2)
        return FALSE;

    CompletionEvent = Wait->CompletionEvent;
    NtClose(Wait->CancelEvent);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Wait);
    if (CompletionEvent)
        NtSetEvent(CompletionEvent, NULL);
    return TRUE;
}

static VOID
NTAPI
Wait_thread_proc(LPVOID Arg)
{
    PRTLP_WAIT Wait = (PRTLP_WAIT) Arg;
    NTSTATUS Status;
    BOOLEAN alertable = (Wait->Flags & WT_EXECUTEINIOTHREAD) != 0;
    HANDLE handles[2] = { Wait->CancelEvent, Wait->Object };
    LARGE_INTEGER timeout;

//    TRACE("\n");

    while (TRUE)
    {
        Status = NtWaitForMultipleObjects( 2,
                                           handles,
                                           WaitAny,
                                           alertable,
                                           get_nt_timeout( &timeout, Wait->Milliseconds ) );

        if (Status == STATUS_WAIT_1 || Status == STATUS_TIMEOUT)
        {
            BOOLEAN TimerOrWaitFired;

            if (Status == STATUS_WAIT_1)
            {
   //             TRACE( "object %p signaled, calling callback %p with context %p\n",
   //                 Wait->Object, Wait->Callback,
   //                 Wait->Context );
                TimerOrWaitFired = FALSE;
            }
            else
            {
    //            TRACE( "wait for object %p timed out, calling callback %p with context %p\n",
    //                Wait->Object, Wait->Callback,
    //                Wait->Context );
                TimerOrWaitFired = TRUE;
            }
            Wait->CallbackInProgress = TRUE;
            Wait->Callback( Wait->Context, TimerOrWaitFired );
            Wait->CallbackInProgress = FALSE;

            if (Wait->Flags & WT_EXECUTEONLYONCE)
                break;
        }
        else if (Status != STATUS_USER_APC)
            break;
    }

    RtlpReleaseWait(Wait);
}


/* FUNCTIONS ***************************************************************/


/***********************************************************************
 *              RtlRegisterWait
 *
 * Registers a wait for a handle to become signaled.
 *
 * PARAMS
 *  NewWaitObject [I] Handle to the new wait object. Use RtlDeregisterWait() to free it.
 *  Object   [I] Object to wait to become signaled.
 *  Callback [I] Callback function to execute when the wait times out or the handle is signaled.
 *  Context  [I] Context to pass to the callback function when it is executed.
 *  Milliseconds [I] Number of milliseconds to wait before timing out.
 *  Flags    [I] Flags. See notes.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: Any NTSTATUS code.
 *
 * NOTES
 *  Flags can be one or more of the following:
 *|WT_EXECUTEDEFAULT - Executes the work item in a non-I/O worker thread.
 *|WT_EXECUTEINIOTHREAD - Executes the work item in an I/O worker thread.
 *|WT_EXECUTEINPERSISTENTTHREAD - Executes the work item in a thread that is persistent.
 *|WT_EXECUTELONGFUNCTION - Hints that the execution can take a long time.
 *|WT_TRANSFER_IMPERSONATION - Executes the function with the current access token.
 */
NTSTATUS
NTAPI
RtlRegisterWait(PHANDLE NewWaitObject,
                HANDLE Object,
                WAITORTIMERCALLBACKFUNC Callback,
                PVOID Context,
                ULONG Milliseconds,
                ULONG Flags)
{
    PRTLP_WAIT Wait;
    NTSTATUS Status;

    //TRACE( "(%p, %p, %p, %p, %d, 0x%x)\n", NewWaitObject, Object, Callback, Context, Milliseconds, Flags );

    Wait = RtlAllocateHeap( RtlGetProcessHeap(), 0, sizeof(RTLP_WAIT) );
    if (!Wait)
        return STATUS_NO_MEMORY;

    Wait->Object = Object;
    Wait->Callback = Callback;
    Wait->Context = Context;
    Wait->Milliseconds = Milliseconds;
    Wait->Flags = Flags;
    Wait->CallbackInProgress = FALSE;
    Wait->DeleteCount = 0;
    Wait->CompletionEvent = NULL;

    Status = NtCreateEvent( &Wait->CancelEvent,
                             EVENT_ALL_ACCESS,
                             NULL,
                             NotificationEvent,
                             FALSE );

    if (Status != STATUS_SUCCESS)
    {
        RtlFreeHeap( RtlGetProcessHeap(), 0, Wait );
        return Status;
    }

    Flags = Flags & (WT_EXECUTEINIOTHREAD | WT_EXECUTEINPERSISTENTTHREAD |
                     WT_EXECUTELONGFUNCTION | WT_TRANSFER_IMPERSONATION);

    Status = RtlQueueWorkItem( Wait_thread_proc,
                               Wait,
                               Flags );

    if (Status != STATUS_SUCCESS)
    {
        NtClose( Wait->CancelEvent );
        RtlFreeHeap( RtlGetProcessHeap(), 0, Wait );
        return Status;
    }

    *NewWaitObject = Wait;
    return Status;
}

/***********************************************************************
 *              RtlDeregisterWaitEx
 *
 * Cancels a wait operation and frees the resources associated with calling
 * RtlRegisterWait().
 *
 * PARAMS
 *  WaitObject [I] Handle to the wait object to free.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: Any NTSTATUS code.
 */
NTSTATUS
NTAPI
RtlDeregisterWaitEx(HANDLE WaitHandle,
                    HANDLE CompletionEvent)
{
    PRTLP_WAIT Wait = (PRTLP_WAIT) WaitHandle;
    BOOLEAN Synchronous = (CompletionEvent == INVALID_HANDLE_VALUE);
    BOOLEAN CallbackPending;
    NTSTATUS Status;

    if (!Wait)
        return STATUS_INVALID_HANDLE;

    if (Synchronous)
    {
        Status = NtCreateEvent(&CompletionEvent, EVENT_ALL_ACCESS, NULL,
                               NotificationEvent, FALSE);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    InterlockedExchangePointer(&Wait->CompletionEvent, CompletionEvent);
    NtSetEvent(Wait->CancelEvent, NULL);
    CallbackPending = Wait->CallbackInProgress;
    Status = RtlpReleaseWait(Wait) ? STATUS_SUCCESS : STATUS_PENDING;
    if (!CompletionEvent && !CallbackPending)
        Status = STATUS_SUCCESS;

    /* Wait must not be accessed after dropping our reference. */
    if (Synchronous)
    {
        Status = NtWaitForSingleObject(CompletionEvent, FALSE, NULL);
        NtClose(CompletionEvent);
    }
    return Status;
}

/***********************************************************************
 *              RtlDeregisterWait
 *
 * Cancels a wait operation and frees the resources associated with calling
 * RtlRegisterWait().
 *
 * PARAMS
 *  WaitObject [I] Handle to the wait object to free.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: Any NTSTATUS code.
 */
NTSTATUS
NTAPI
RtlDeregisterWait(HANDLE WaitHandle)
{
    return RtlDeregisterWaitEx(WaitHandle, NULL);
}

/* EOF */
