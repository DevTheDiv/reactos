/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental modern-PCI virtio GPU probe. No user submissions or display DDI.
 */
#include <ntddk.h>
#include <initguid.h>
#include <wdmguid.h>
#include <stdarg.h>
#include <drivers/vgpu_diag.h>
#include "osdep.h"
#include "virtio_pci.h"
#include "VirtIO.h"
#include "virtio_pci_common.h"
#include "kdebugprint.h"

#define VGPU_TAG 'pgVV'
#define VGPU_MAX_BAR (1024 * 1024)
#define VGPU_MAX_DMA (256 * 1024)
#define VGPU_INTX_DISABLE 0x400
#define VGPU_GET_DISPLAY_INFO 0x0100
#define VGPU_RESP_DISPLAY_INFO 0x1101
#define VGPU_FLAG_FENCE 1

typedef struct _GPU_HEADER
{
    ULONG Type, Flags;
    ULONGLONG Fence;
    ULONG Context;
    UCHAR Ring, Padding[3];
} GPU_HEADER;

typedef struct _GPU_RESPONSE
{
    GPU_HEADER Header;
    VGPU_DIAG_SCANOUT Scanouts[VGPU_DIAG_MAX_SCANOUTS];
} GPU_RESPONSE;
C_ASSERT(sizeof(GPU_HEADER) == 24);
C_ASSERT(sizeof(GPU_RESPONSE) == 408);
C_ASSERT(sizeof(VGPU_DIAG_INFO) == 432);

typedef struct _GPU_BAR
{
    PHYSICAL_ADDRESS Physical;
    ULONG Length;
    PUCHAR Virtual;
} GPU_BAR;

typedef struct _GPU_EXTENSION
{
    PDEVICE_OBJECT Self, Lower;
    IO_REMOVE_LOCK RemoveLock;
    KSPIN_LOCK InfoLock;
    VGPU_DIAG_INFO Info;
    BUS_INTERFACE_STANDARD Bus;
    PCI_COMMON_CONFIG Pci;
    GPU_BAR Bars[PCI_TYPE0_ADDRESSES];
    VirtIODevice Virtio;
    PVOID Packet;
    USHORT OriginalCommand;
    BOOLEAN CommandChanged;
    BOOLEAN DmaQuarantined;
} GPU_EXTENSION;

static LONG DevicePresent;
int virtioDebugLevel = 0;
int bDebugPrint = 1;
static void GpuLibPrint(const char *Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    vDbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, Format, Args);
    va_end(Args);
}
tDebugPrintFunc VirtioDebugPrintProc = GpuLibPrint;

static VOID GpuSleep(void *Context, unsigned int Milliseconds)
{
    LARGE_INTEGER Delay;
    UNREFERENCED_PARAMETER(Context);
    Delay.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}
static u8 GpuRead8(ULONG_PTR Address) { return READ_REGISTER_UCHAR((PUCHAR)Address); }
static u16 GpuRead16(ULONG_PTR Address) { return READ_REGISTER_USHORT((PUSHORT)Address); }
static u32 GpuRead32(ULONG_PTR Address) { return READ_REGISTER_ULONG((PULONG)Address); }
static void GpuWrite8(ULONG_PTR Address, u8 Value) { WRITE_REGISTER_UCHAR((PUCHAR)Address, Value); }
static void GpuWrite16(ULONG_PTR Address, u16 Value) { WRITE_REGISTER_USHORT((PUSHORT)Address, Value); }
static void GpuWrite32(ULONG_PTR Address, u32 Value) { WRITE_REGISTER_ULONG((PULONG)Address, Value); }

static void *GpuAllocPages(void *Context, size_t Size)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Address;
    UNREFERENCED_PARAMETER(Context);
    if (!Size || Size > VGPU_MAX_DMA) return NULL;
    Low.QuadPart = Boundary.QuadPart = 0;
    High.QuadPart = MAXULONG;
    Address = MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Boundary, MmNonCached);
    if (Address) RtlZeroMemory(Address, Size);
    return Address;
}
static void GpuFreePages(void *Context, void *Address)
{
    UNREFERENCED_PARAMETER(Context);
    MmFreeContiguousMemory(Address);
}
static ULONGLONG GpuPhysical(void *Context, void *Address)
{
    UNREFERENCED_PARAMETER(Context);
    return MmGetPhysicalAddress(Address).QuadPart;
}
static void *GpuAllocPool(void *Context, size_t Size)
{
    UNREFERENCED_PARAMETER(Context);
    if (!Size || Size > VGPU_MAX_DMA) return NULL;
    return ExAllocatePoolWithTag(NonPagedPool, Size, VGPU_TAG);
}
static void GpuFreePool(void *Context, void *Address)
{
    UNREFERENCED_PARAMETER(Context);
    ExFreePoolWithTag(Address, VGPU_TAG);
}
static int GpuConfig(void *Context, int Offset, void *Value, ULONG Size)
{
    GPU_EXTENSION *Ext = Context;
    if (Offset < 0 || (ULONG)Offset > sizeof(Ext->Pci) - Size) return -1;
    RtlCopyMemory(Value, (PUCHAR)&Ext->Pci + Offset, Size);
    return 0;
}
static int GpuConfig8(void *Context, int Offset, u8 *Value) { return GpuConfig(Context, Offset, Value, 1); }
static int GpuConfig16(void *Context, int Offset, u16 *Value) { return GpuConfig(Context, Offset, Value, 2); }
static int GpuConfig32(void *Context, int Offset, u32 *Value) { return GpuConfig(Context, Offset, Value, 4); }
static size_t GpuBarLength(void *Context, int Bar)
{
    GPU_EXTENSION *Ext = Context;
    return (Bar >= 0 && Bar < PCI_TYPE0_ADDRESSES) ? Ext->Bars[Bar].Length : 0;
}
static void *GpuMapBar(void *Context, int Bar, size_t Offset, size_t Length)
{
    GPU_EXTENSION *Ext = Context;
    GPU_BAR *Resource;
    if (Bar < 0 || Bar >= PCI_TYPE0_ADDRESSES || !Length) return NULL;
    Resource = &Ext->Bars[Bar];
    if (!Resource->Length || Resource->Length > VGPU_MAX_BAR ||
        Offset > Resource->Length || Length > Resource->Length - Offset ||
        Resource->Physical.HighPart) return NULL;
    if (!Resource->Virtual)
        Resource->Virtual = MmMapIoSpace(Resource->Physical, Resource->Length, MmNonCached);
    return Resource->Virtual ? Resource->Virtual + Offset : NULL;
}
static u16 GpuVector(void *Context, int Queue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Queue);
    return VIRTIO_MSI_NO_VECTOR;
}
static const VirtIOSystemOps GpuOps = {
    GpuRead8, GpuRead16, GpuRead32, GpuWrite8, GpuWrite16, GpuWrite32,
    GpuAllocPages, GpuFreePages, GpuPhysical, GpuAllocPool, GpuFreePool,
    GpuConfig8, GpuConfig16, GpuConfig32, GpuBarLength, GpuMapBar, GpuVector, GpuSleep
};

static NTSTATUS NTAPI GpuSignal(PDEVICE_OBJECT Device, PIRP Irp, PVOID Context)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}
static NTSTATUS GpuForwardWait(GPU_EXTENSION *Ext, PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, GpuSignal, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Ext->Lower, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return Irp->IoStatus.Status;
}

static NTSTATUS GpuGetBus(GPU_EXTENSION *Ext)
{
    PIRP Irp;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    Irp = IoAllocateIrp(Ext->Lower->StackSize, FALSE);
    if (!Irp) return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    RtlZeroMemory(Stack, sizeof(*Stack));
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(Ext->Bus);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Ext->Bus;
    IoSetCompletionRoutine(Irp, GpuSignal, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Ext->Lower, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS GpuValidateCaps(GPU_EXTENSION *Ext)
{
    UCHAR Seen[256] = {0};
    PUCHAR Config = (PUCHAR)&Ext->Pci;
    ULONG Offset = Ext->Pci.u.type0.CapabilitiesPtr, Count = 0;
    while (Offset)
    {
        if (Offset < 0x40 || Offset > 0xfc || (Offset & 3) || Seen[Offset] || ++Count > 48)
            return STATUS_DEVICE_DATA_ERROR;
        Seen[Offset] = 1;
        if (Config[Offset] == PCI_CAPABILITY_ID_VENDOR_SPECIFIC &&
            (Config[Offset + 2] < 16 || Config[Offset + 2] > 256 - Offset))
            return STATUS_DEVICE_DATA_ERROR;
        Offset = Config[Offset + 1];
    }
    return STATUS_SUCCESS;
}

static NTSTATUS GpuResources(GPU_EXTENSION *Ext, PCM_RESOURCE_LIST Raw, PCM_RESOURCE_LIST Translated)
{
    ULONG i, Count;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR R, T;
    int Bar;
    if (!Raw || !Translated || Raw->Count != 1 || Translated->Count != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Count = Raw->List[0].PartialResourceList.Count;
    if (Count != Translated->List[0].PartialResourceList.Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (i = 0; i < Count; ++i)
    {
        R = &Raw->List[0].PartialResourceList.PartialDescriptors[i];
        T = &Translated->List[0].PartialResourceList.PartialDescriptors[i];
        if (R->Type != CmResourceTypeMemory || T->Type != CmResourceTypeMemory) continue;
        Bar = virtio_get_bar_index((PPCI_COMMON_HEADER)&Ext->Pci, R->u.Memory.Start);
        if (Bar < 0 || Bar >= PCI_TYPE0_ADDRESSES) continue;
        if (Ext->Bars[Bar].Length) return STATUS_DEVICE_CONFIGURATION_ERROR;
        Ext->Bars[Bar].Physical = T->u.Memory.Start;
        Ext->Bars[Bar].Length = T->u.Memory.Length;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS GpuReset(GPU_EXTENSION *Ext)
{
    ULONGLONG Deadline = KeQueryInterruptTime() + 10000000;
    WRITE_REGISTER_UCHAR((PUCHAR)&Ext->Virtio.common->device_status, 0);
    do
    {
        if (!READ_REGISTER_UCHAR((PUCHAR)&Ext->Virtio.common->device_status))
            return STATUS_SUCCESS;
        GpuSleep(Ext, 1);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS GpuProbe(GPU_EXTENSION *Ext, PIO_STACK_LOCATION Stack)
{
    NTSTATUS Status, ResetStatus = STATUS_SUCCESS;
    VGPU_DIAG_INFO Info = {0};
    VirtIODevice *Device = &Ext->Virtio;
    struct virtqueue *Queue = NULL;
    struct scatterlist Buffers[2];
    GPU_HEADER *Request;
    GPU_RESPONSE *Response;
    ULONGLONG Features, Deadline;
    USHORT Command, Entries;
    ULONG RingSize, HeapSize, i;
    unsigned int Used = 0;
    PVOID Token = NULL;
    KIRQL Irql;

    if (Ext->DmaQuarantined) return STATUS_DEVICE_HARDWARE_ERROR;

    Info.Size = sizeof(Info);
    Info.Version = VGPU_DIAG_VERSION;
    Status = GpuGetBus(Ext);
    if (!NT_SUCCESS(Status)) goto Done;
    Info.Stage = VGPU_STAGE_BUS;
    if (!Ext->Bus.GetBusData || !Ext->Bus.SetBusData ||
        Ext->Bus.GetBusData(Ext->Bus.Context, PCI_WHICHSPACE_CONFIG, &Ext->Pci,
                            0, sizeof(Ext->Pci)) != sizeof(Ext->Pci))
    { Status = STATUS_DEVICE_CONFIGURATION_ERROR; goto Done; }
    if (Ext->Pci.VendorID != 0x1af4 || Ext->Pci.DeviceID != 0x1050 ||
        (Ext->Pci.HeaderType & 0x7f) != PCI_DEVICE_TYPE || !Ext->Pci.RevisionID)
    { Status = STATUS_NOT_SUPPORTED; goto Done; }
    Status = GpuValidateCaps(Ext);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = GpuResources(Ext, Stack->Parameters.StartDevice.AllocatedResources,
                          Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
    if (!NT_SUCCESS(Status)) goto Done;

    Ext->OriginalCommand = Ext->Pci.Command;
    Command = Ext->OriginalCommand | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER | VGPU_INTX_DISABLE;
    if (Ext->Bus.SetBusData(Ext->Bus.Context, PCI_WHICHSPACE_CONFIG, &Command,
                            FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(Command)) != sizeof(Command))
    { Status = STATUS_DEVICE_CONFIGURATION_ERROR; goto Done; }
    Ext->CommandChanged = TRUE;

    /* Transport-only setup: the public initializer performs an unbounded reset.
     * Own the status/reset sequence here so diagnostic timeouts stay bounded. */
    RtlZeroMemory(Device, sizeof(*Device));
    Device->system = &GpuOps;
    Device->DeviceContext = Ext;
    Device->info = Device->inline_info;
    Device->maxQueues = RTL_NUMBER_OF(Device->inline_info);
    Status = vio_modern_initialize(Device);
    if (!NT_SUCCESS(Status)) goto Done;
    Info.Stage = VGPU_STAGE_TRANSPORT;
    Status = GpuReset(Ext);
    if (!NT_SUCCESS(Status)) goto Done;
    virtio_add_status(Device, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
    Features = virtio_get_features(Device);
    Info.HostFeaturesLow = (ULONG)Features;
    Info.HostFeaturesHigh = (ULONG)(Features >> 32);
    if (!(Features & (1ULL << VIRTIO_F_VERSION_1)))
    { Status = STATUS_NOT_SUPPORTED; goto Done; }
    Features = 1ULL << VIRTIO_F_VERSION_1;
    Status = virtio_set_features(Device, Features);
    if (!NT_SUCCESS(Status)) goto Done;
    Info.GuestFeaturesHigh = (ULONG)(Features >> 32);
    Info.Stage = VGPU_STAGE_FEATURES;

    Status = virtio_query_queue_allocation(Device, 0, &Entries, &RingSize, &HeapSize);
    if (!NT_SUCCESS(Status)) goto Done;
    if (Entries < 2 || Entries > 1024 || RingSize > VGPU_MAX_DMA || HeapSize > VGPU_MAX_DMA)
    { Status = STATUS_DEVICE_CONFIGURATION_ERROR; goto Done; }
    Status = virtio_find_queue(Device, 0, &Queue);
    if (!NT_SUCCESS(Status)) goto Done;
    virtqueue_disable_cb(Queue);
    Info.Stage = VGPU_STAGE_QUEUE;
    Ext->Packet = GpuAllocPages(Ext, PAGE_SIZE);
    if (!Ext->Packet) { Status = STATUS_INSUFFICIENT_RESOURCES; goto Done; }
    RtlZeroMemory(Ext->Packet, PAGE_SIZE);
    Request = Ext->Packet;
    Response = (GPU_RESPONSE *)((PUCHAR)Ext->Packet + 64);
    Request->Type = VGPU_GET_DISPLAY_INFO;
    Request->Flags = VGPU_FLAG_FENCE;
    Request->Fence = 1;
    Buffers[0].physAddr = MmGetPhysicalAddress(Request);
    Buffers[0].length = sizeof(*Request);
    Buffers[1].physAddr = MmGetPhysicalAddress(Response);
    Buffers[1].length = sizeof(*Response);
    if (virtqueue_add_buf(Queue, Buffers, 1, 1, Ext, NULL, 0) < 0)
    { Status = STATUS_INSUFFICIENT_RESOURCES; goto Done; }
    virtio_device_ready(Device);
    Info.Stage = VGPU_STAGE_COMMAND;
    KeMemoryBarrier();
    virtqueue_kick(Queue);
    Deadline = KeQueryInterruptTime() + 50000000;
    do
    {
        Token = virtqueue_get_buf(Queue, &Used);
        if (Token) break;
        GpuSleep(Ext, 1);
    } while (KeQueryInterruptTime() < Deadline);
    if (!Token) { Status = STATUS_IO_TIMEOUT; goto Done; }
    KeMemoryBarrier();
    if (Token != Ext || Used != sizeof(*Response) ||
        Response->Header.Type != VGPU_RESP_DISPLAY_INFO ||
        !(Response->Header.Flags & VGPU_FLAG_FENCE) || Response->Header.Fence != 1)
    { Status = STATUS_DEVICE_PROTOCOL_ERROR; goto Done; }
    for (i = 0; i < VGPU_DIAG_MAX_SCANOUTS; ++i)
    {
        if (Response->Scanouts[i].Enabled > 1 ||
            (Response->Scanouts[i].Enabled &&
             (!Response->Scanouts[i].Width || !Response->Scanouts[i].Height)))
        { Status = STATUS_DEVICE_PROTOCOL_ERROR; goto Done; }
        Info.Scanouts[i] = Response->Scanouts[i];
        if (Response->Scanouts[i].Enabled) ++Info.EnabledScanouts;
    }
    Info.CompletedRequests = 1;
    Info.Stage = VGPU_STAGE_COMPLETE;
    Status = STATUS_SUCCESS;

Done:
    if (Device->common)
    {
        ResetStatus = GpuReset(Ext);
        if (!NT_SUCCESS(ResetStatus))
        {
            /* Never recycle memory the device may still DMA into. This is a
             * diagnostic failure; retain these bounded allocations until reboot. */
            Info.Flags |= VGPU_DIAG_DMA_QUARANTINED;
            Ext->DmaQuarantined = TRUE;
            Status = ResetStatus;
        }
        else
        {
            if (Queue) virtio_delete_queues(Device);
            if (Ext->Packet) GpuFreePages(Ext, Ext->Packet);
            virtio_device_shutdown(Device);
        }
    }
    for (i = 0; i < RTL_NUMBER_OF(Ext->Bars); ++i)
    {
        if (Ext->Bars[i].Virtual)
            MmUnmapIoSpace(Ext->Bars[i].Virtual, Ext->Bars[i].Length);
    }
    Command = Ext->OriginalCommand;
    if (Ext->DmaQuarantined)
        Command = (Command & ~PCI_ENABLE_BUS_MASTER) | VGPU_INTX_DISABLE;
    if (Ext->CommandChanged && Ext->Bus.SetBusData)
        Ext->Bus.SetBusData(Ext->Bus.Context, PCI_WHICHSPACE_CONFIG,
                            &Command, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(Command));
    if (Ext->Bus.InterfaceDereference) Ext->Bus.InterfaceDereference(Ext->Bus.Context);
    RtlZeroMemory(&Ext->Bus, sizeof(Ext->Bus));
    RtlZeroMemory(Ext->Bars, sizeof(Ext->Bars));
    RtlZeroMemory(Device, sizeof(*Device));
    Ext->Packet = NULL;
    Ext->CommandChanged = FALSE;
    Info.Status = Status;
    Info.ResetStatus = ResetStatus;
    KeAcquireSpinLock(&Ext->InfoLock, &Irql);
    Ext->Info = Info;
    KeReleaseSpinLock(&Ext->InfoLock, Irql);
    DbgPrint("vgpudiag: stage=%lu status=%08lx reset=%08lx completed=%lu scanouts=%lu flags=%lx\n",
             Info.Stage, Info.Status, Info.ResetStatus, Info.CompletedRequests, Info.EnabledScanouts, Info.Flags);
    return Status;
}

static NTSTATUS GpuComplete(PIRP Irp, NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
static NTSTATUS NTAPI GpuRelease(PDEVICE_OBJECT Device, PIRP Irp, PVOID Context)
{
    GPU_EXTENSION *Ext = Context;
    UNREFERENCED_PARAMETER(Device);
    if (Irp->PendingReturned) IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&Ext->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}
static NTSTATUS NTAPI GpuDispatch(PDEVICE_OBJECT Device, PIRP Irp)
{
    GPU_EXTENSION *Ext = Device->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    KIRQL Irql;
    UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\VgpuDiag");
    Irp->IoStatus.Information = 0;
    Status = IoAcquireRemoveLock(&Ext->RemoveLock, Irp);
    if (!NT_SUCCESS(Status)) return GpuComplete(Irp, Status);
    if (Stack->MajorFunction == IRP_MJ_PNP)
    {
        switch (Stack->MinorFunction)
        {
            case IRP_MN_START_DEVICE:
                Status = GpuForwardWait(Ext, Irp);
                if (NT_SUCCESS(Status)) Status = GpuProbe(Ext, Stack);
                IoReleaseRemoveLock(&Ext->RemoveLock, Irp);
                return GpuComplete(Irp, Status);
            case IRP_MN_REMOVE_DEVICE:
                IoReleaseRemoveLockAndWait(&Ext->RemoveLock, Irp);
                Status = GpuForwardWait(Ext, Irp);
                IoDeleteSymbolicLink(&Link);
                IoDetachDevice(Ext->Lower);
                GpuComplete(Irp, Status);
                IoDeleteDevice(Device);
                InterlockedExchange(&DevicePresent, 0);
                return Status;
            case IRP_MN_QUERY_STOP_DEVICE:
            case IRP_MN_QUERY_REMOVE_DEVICE:
                Irp->IoStatus.Status = STATUS_SUCCESS;
                break;
        }
    }
    else if (Stack->MajorFunction == IRP_MJ_CREATE ||
             Stack->MajorFunction == IRP_MJ_CLEANUP || Stack->MajorFunction == IRP_MJ_CLOSE)
    {
        IoReleaseRemoveLock(&Ext->RemoveLock, Irp);
        return GpuComplete(Irp, STATUS_SUCCESS);
    }
    else if (Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_VGPU_QUERY_DIAGNOSTICS)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(Ext->Info))
            {
                KeAcquireSpinLock(&Ext->InfoLock, &Irql);
                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Ext->Info, sizeof(Ext->Info));
                KeReleaseSpinLock(&Ext->InfoLock, Irql);
                Irp->IoStatus.Information = sizeof(Ext->Info);
                Status = STATUS_SUCCESS;
            }
        }
        IoReleaseRemoveLock(&Ext->RemoveLock, Irp);
        return GpuComplete(Irp, Status);
    }
    else if (Stack->MajorFunction != IRP_MJ_POWER && Stack->MajorFunction != IRP_MJ_SYSTEM_CONTROL)
    {
        IoReleaseRemoveLock(&Ext->RemoveLock, Irp);
        return GpuComplete(Irp, STATUS_INVALID_DEVICE_REQUEST);
    }
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, GpuRelease, Ext, TRUE, TRUE, TRUE);
    if (Stack->MajorFunction == IRP_MJ_POWER)
    {
        PoStartNextPowerIrp(Irp);
        return PoCallDriver(Ext->Lower, Irp);
    }
    return IoCallDriver(Ext->Lower, Irp);
}

static NTSTATUS NTAPI GpuAddDevice(PDRIVER_OBJECT Driver, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT Device;
    GPU_EXTENSION *Ext;
    NTSTATUS Status;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\VgpuDiag");
    UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\VgpuDiag");
    if (InterlockedCompareExchange(&DevicePresent, 1, 0)) return STATUS_DEVICE_BUSY;
    Status = IoCreateDevice(Driver, sizeof(*Ext), &Name, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Device);
    if (!NT_SUCCESS(Status)) goto Failed;
    Ext = Device->DeviceExtension;
    RtlZeroMemory(Ext, sizeof(*Ext));
    Ext->Self = Device;
    IoInitializeRemoveLock(&Ext->RemoveLock, VGPU_TAG, 0, 0);
    KeInitializeSpinLock(&Ext->InfoLock);
    Ext->Info.Size = sizeof(Ext->Info);
    Ext->Info.Version = VGPU_DIAG_VERSION;
    Ext->Info.Status = STATUS_DEVICE_NOT_READY;
    Ext->Lower = IoAttachDeviceToDeviceStack(Device, Pdo);
    if (!Ext->Lower) { Status = STATUS_NO_SUCH_DEVICE; IoDeleteDevice(Device); goto Failed; }
    Status = IoCreateSymbolicLink(&Link, &Name);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(Ext->Lower);
        IoDeleteDevice(Device);
        goto Failed;
    }
    Device->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    Device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
Failed:
    InterlockedExchange(&DevicePresent, 0);
    return Status;
}
static VOID NTAPI GpuUnload(PDRIVER_OBJECT Driver)
{
    UNREFERENCED_PARAMETER(Driver);
}
NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT Driver, PUNICODE_STRING RegistryPath)
{
    ULONG i;
    UNREFERENCED_PARAMETER(RegistryPath);
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) Driver->MajorFunction[i] = GpuDispatch;
    Driver->DriverExtension->AddDevice = GpuAddDevice;
    Driver->DriverUnload = GpuUnload;
    return STATUS_SUCCESS;
}
