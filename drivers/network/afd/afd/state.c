/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/net/afd/afd/state.c
 * PURPOSE:          AFD Flow Control & Socket Readiness State Machine
 * PROGRAMMER:       ReactOS Development Team
 */

#include "afd.h"

/*
 * AfdTransitionFlowState:
 * Canonical state engine for connection-oriented (TCP) socket flow control.
 *
 * LOCKING CONTRACT:
 * Caller MUST hold SocketAcquireStateLock(FCB) prior to calling this function.
 * This function NEVER acquires or releases FCB->Mutex internally.
 */
VOID
AfdTransitionFlowState(
    PAFD_FCB FCB,
    AFD_FLOW_EVENT Event,
    PVOID Context)
{
    ULONG ReenableMask = 0;
    UINT UnreadBytes;

    if (!FCB)
        return;

    /* Enforce caller-owned locking contract */
    ASSERT(FCB->CurrentThread == KeGetCurrentThread());

    /* Datagram sockets are handled separately via datagram queue paths */
    if (FCB->Flags & AFD_ENDPOINT_CONNECTIONLESS)
        return;

    /* -------------------------------------------------------------
     * STAGE 1: Event Ingestion & Mask Management
     * ------------------------------------------------------------- */
    switch (Event)
    {
        case AFD_EVT_EVENT_SELECT_SET:
        {
            PAFD_EVENT_SELECT_INFO EventSelectInfo = (PAFD_EVENT_SELECT_INFO)Context;
            if (EventSelectInfo)
            {
                /* Per Winsock specification, calling WSAEventSelect for a socket
                 * clears the disabled record for the registered events. */
                ReenableMask = EventSelectInfo->Events;
            }
            break;
        }

        case AFD_EVT_EVENTS_ENUMERATED:
        {
            /* Events returned to user-mode are disabled until re-enabled by a
             * subsequent function call (e.g. read/write/event select). */
            FCB->EventSelectDisabled |= (FCB->PollState & FCB->EventSelectTriggers);
            break;
        }

        case AFD_EVT_RECV_USER_POSTED:
        case AFD_EVT_RECV_USER_SATISFIED:
        {
            /* Reading data re-enables receive event notifications */
            ReenableMask |= AFD_EVENT_RECEIVE;
            break;
        }

        case AFD_EVT_SEND_USER_POSTED:
        case AFD_EVT_SEND_TDI_COMPLETE:
        {
            /* Send activity or ACK completion re-enables send event notifications */
            ReenableMask |= AFD_EVENT_SEND;
            break;
        }

        case AFD_EVT_RECV_TDI_COMPLETE:
        case AFD_EVT_RECV_CONNECTED:
        case AFD_EVT_RECV_REMOTE_FIN:
        case AFD_EVT_SEND_SHUTDOWN:
        default:
            break;
    }

    if (ReenableMask)
    {
        FCB->EventSelectDisabled &= ~ReenableMask;
    }

    /* -------------------------------------------------------------
     * STAGE 2: Receive Readiness & Close State Evaluation
     * ------------------------------------------------------------- */
    UnreadBytes = FCB->Recv.Content - FCB->Recv.BytesUsed;

    if (UnreadBytes > 0 && IsListEmpty(&FCB->PendingIrpList[FUNCTION_RECV]))
    {
        FCB->PollState |= AFD_EVENT_RECEIVE;
        FCB->PollStatus[FD_READ_BIT] = STATUS_SUCCESS;
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_RECEIVE;
    }

    /* Check for connection termination / EOF */
    if (FCB->TdiReceiveClosed && UnreadBytes == 0)
    {
        if (FCB->LastReceiveStatus == STATUS_SUCCESS)
        {
            FCB->PollState |= AFD_EVENT_DISCONNECT;
        }
        else
        {
            FCB->PollState |= AFD_EVENT_CLOSE;
        }
        FCB->PollStatus[FD_CLOSE_BIT] = FCB->LastReceiveStatus;
    }

    /* -------------------------------------------------------------
     * STAGE 3: Send Readiness Evaluation
     * ------------------------------------------------------------- */
    if (!FCB->SendClosed && (FCB->Send.Size - FCB->Send.BytesUsed > 0))
    {
        FCB->PollState |= AFD_EVENT_SEND;
        FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
    }

    /* -------------------------------------------------------------
     * STAGE 3.5: Receive Window Replenishment (TdiReceive dispatch)
     * -------------------------------------------------------------
     * If buffer has room and no receive IRP is currently outstanding,
     * issue TdiReceive to pull pending data from the transport/lwIP.
     */
    if (!FCB->TdiReceiveClosed && !FCB->ReceiveIrp.InFlightRequest)
    {
        RefillSocketBuffer(FCB);
    }

    DbgPrint("[AFD-STATE] FCB %p Evt %d Unread %u InFlight %p Poll 0x%x Trig 0x%x Dis 0x%x EvtObj %p\n",
             FCB,
             Event,
             FCB->Recv.Content - FCB->Recv.BytesUsed,
             FCB->ReceiveIrp.InFlightRequest,
             FCB->PollState,
             FCB->EventSelectTriggers,
             FCB->EventSelectDisabled,
             FCB->EventSelect);

    /* -------------------------------------------------------------
     * STAGE 4: Unified Dispatch (Poll Re-evaluation & Event Select)
     * ------------------------------------------------------------- */
    PollReeval(FCB->DeviceExt, FCB->FileObject);

    if (FCB->EventSelect &&
        (FCB->PollState & (FCB->EventSelectTriggers & ~FCB->EventSelectDisabled)))
    {
        AFD_DbgPrint(MID_TRACE,("Setting event %p (PollState 0x%x, Triggers 0x%x, Disabled 0x%x)\n",
                                FCB->EventSelect,
                                FCB->PollState,
                                FCB->EventSelectTriggers,
                                FCB->EventSelectDisabled));
        KeSetEvent(FCB->EventSelect, IO_NETWORK_INCREMENT, FALSE);
    }
}
