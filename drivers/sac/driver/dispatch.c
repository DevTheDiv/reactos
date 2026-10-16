/*
 * PROJECT:     ReactOS Drivers
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * FILE:        drivers/sac/driver/dispatch.c
 * PURPOSE:     Driver for the Server Administration Console (SAC) for EMS
 * PROGRAMMERS: ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "sacdrv.h"

/* GLOBALS ********************************************************************/

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
DispatchCreate(IN PSAC_DEVICE_EXTENSION DeviceExtension,
               IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    /* Channel creation happens via IOCTL_SAC_CHANNEL_CREATE once opened */
    Irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DispatchClose(IN PSAC_DEVICE_EXTENSION DeviceExtension,
              IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    UNREFERENCED_PARAMETER(DeviceExtension);

    /* Tear down any channel(s) this handle owns */
    ChanMgrCloseChannelsWithFileObject(IrpSp->FileObject);

    Irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DispatchDeviceControl(IN PDEVICE_OBJECT DeviceObject,
                      IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PSAC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    ULONG InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;
    KIRQL OldIrql;

    Irp->IoStatus.Information = 0;

    /* This one completes later, from DoCmdCommand -- handle it separately */
    if (IoControlCode == IOCTL_SAC_WAIT_CMD_REQUEST)
    {
        KeAcquireSpinLock(&DeviceExtension->Lock, &OldIrql);
        if (DeviceExtension->PendingCmdIrp)
        {
            /* Only one helper can wait at a time */
            KeReleaseSpinLock(&DeviceExtension->Lock, OldIrql);
            Irp->IoStatus.Status = STATUS_DEVICE_BUSY;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_DEVICE_BUSY;
        }

        IoMarkIrpPending(Irp);
        DeviceExtension->PendingCmdIrp = Irp;
        KeReleaseSpinLock(&DeviceExtension->Lock, OldIrql);
        return STATUS_PENDING;
    }

    switch (IoControlCode)
    {
        case IOCTL_SAC_CHANNEL_CREATE:
        {
            PSAC_IOCTL_CHANNEL_CREATE CreateRequest;
            SAC_CHANNEL_ATTRIBUTES Attributes;
            PSAC_CHANNEL Channel;

            if ((InputBufferLength < sizeof(SAC_IOCTL_CHANNEL_CREATE)) ||
                (IrpSp->FileObject->FsContext != NULL))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            CreateRequest = (PSAC_IOCTL_CHANNEL_CREATE)SystemBuffer;
            CreateRequest->NameBuffer[SAC_IOCTL_NAME_SIZE] = UNICODE_NULL;
            CreateRequest->DescriptionBuffer[SAC_IOCTL_DESCRIPTION_SIZE] = UNICODE_NULL;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.ChannelType = Cmd;
            RtlCopyMemory(Attributes.NameBuffer,
                          CreateRequest->NameBuffer,
                          sizeof(Attributes.NameBuffer));
            RtlCopyMemory(Attributes.DescriptionBuffer,
                          CreateRequest->DescriptionBuffer,
                          sizeof(Attributes.DescriptionBuffer));

            Status = ChanMgrCreateChannel(&Channel, &Attributes);
            if (NT_SUCCESS(Status))
            {
                Channel->FileObject = IrpSp->FileObject;
                IrpSp->FileObject->FsContext = Channel;
            }
            break;
        }

        case IOCTL_SAC_CHANNEL_READ:
        {
            PSAC_CHANNEL Channel = (PSAC_CHANNEL)IrpSp->FileObject->FsContext;
            ULONG BytesRead = 0;

            if (!Channel)
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }

            Status = (OutputBufferLength > 0)
                      ? Channel->ChannelInputRead(Channel, (PCHAR)SystemBuffer, OutputBufferLength, &BytesRead)
                      : STATUS_SUCCESS;
            Irp->IoStatus.Information = BytesRead;
            break;
        }

        case IOCTL_SAC_CHANNEL_WRITE:
        {
            PSAC_CHANNEL Channel = (PSAC_CHANNEL)IrpSp->FileObject->FsContext;

            if (!Channel)
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }

            Status = (InputBufferLength > 0)
                      ? Channel->ChannelOutputWrite(Channel, (PCHAR)SystemBuffer, InputBufferLength)
                      : STATUS_SUCCESS;
            break;
        }

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
DispatchShutdownControl(IN PDEVICE_OBJECT DeviceObject,
                        IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
Dispatch(IN PDEVICE_OBJECT DeviceObject,
         IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PSAC_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    switch (IrpSp->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = DispatchCreate(DeviceExtension, Irp);
            break;

        case IRP_MJ_CLOSE:
            Status = DispatchClose(DeviceExtension, Irp);
            break;

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

BOOLEAN
NTAPI
SerialBufferIsFull(VOID)
{
    /* The consumer clears slots it reads, so the ring is full when the next
       producer slot is already occupied */
    return (SerialPortBuffer[(SerialPortProducerIndex + 1) &
                             (SAC_SERIAL_PORT_BUFFER_SIZE - 1)] != ANSI_NULL);
}

VOID
NTAPI
TimerDpcRoutine(IN PKDPC Dpc,
                IN PVOID DeferredContext,
                IN PVOID SystemArgument1,
                IN PVOID SystemArgument2)
{
    HEADLESS_RSP_GET_BYTE ByteValue;
    SIZE_T ValueSize;
    BOOLEAN GotChar;
    NTSTATUS Status;
    PSAC_DEVICE_EXTENSION SacExtension;

    SacExtension = DeferredContext;

    /* Mark this DPC as in-flight, so a shutdown that cancels the timer can
       wait for an already-running DPC to finish (fbtpnp HandleStopDevice
       pattern) */
    KeClearEvent(&SacExtension->DpcCompletionEvent);

    /* Set defaults and loop for new characters */
    GotChar = FALSE;
    ValueSize = sizeof(ByteValue);
    do
    {
        /* Ask the kernel for a byte */
        Status = HeadlessDispatch(HeadlessCmdGetByte,
                                  NULL,
                                  0,
                                  &ByteValue,
                                  &ValueSize);

        /* Break out if there's nothing interesting */
        if (!NT_SUCCESS(Status)) break;
        if (!ByteValue.Value) break;

        /* Update the serial port buffer, unless it is already full */
        if (SerialBufferIsFull())
        {
            GotChar = TRUE;
            break;
        }

        SerialPortBuffer[SerialPortProducerIndex] = ByteValue.Value;
        GotChar = TRUE;

        /* Update the index, let it roll-over if needed */
        _InterlockedExchange(&SerialPortProducerIndex,
                             (SerialPortProducerIndex + 1) &
                             (SAC_SERIAL_PORT_BUFFER_SIZE - 1));
    } while (ByteValue.Value);

    /* Did we get anything */
    if (GotChar)
    {
        /* Signal the worker thread that there is work to do */
        KeSetEvent(&SacExtension->Event, SacExtension->PriorityBoost, FALSE);
    }

    /* This DPC has run to completion */
    KeSetEvent(&SacExtension->DpcCompletionEvent, 0, FALSE);
}

VOID
NTAPI
UnloadHandler(IN PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT DeviceObject, NextDevice;
    SAC_DBG(SAC_DBG_ENTRY_EXIT, "SAC UnloadHandler: Entering.\n");

    /* Go over every device part of the driver */
    DeviceObject = DriverObject->DeviceObject;
    while (DeviceObject)
    {
        /* Free and delete the information about this device */
        NextDevice = DeviceObject->NextDevice;
        FreeDeviceData(DeviceObject);
        IoDeleteDevice(DeviceObject);

        /* Move on to the next one */
        DeviceObject = NextDevice;
    }

    /* Free the driver data and exit */
    FreeGlobalData();
    SAC_DBG(SAC_DBG_ENTRY_EXIT, "SAC UnloadHandler: Exiting.\n");
}
