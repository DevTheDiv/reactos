/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the actual split-ring implementation with an emulated device.
 * No kernel driver is installed and no physical DMA is performed by this test.
 */
#ifndef VIRTIO_RING_SOURCE
#define VIRTIO_RING_SOURCE "../../../../sdk/lib/drivers/virtio/VirtIORing.c"
#endif
#include VIRTIO_RING_SOURCE
#include <stdio.h>
#include <stdlib.h>

int bDebugPrint = 0;
int virtioDebugLevel = 0;
tDebugPrintFunc VirtioDebugPrintProc = NULL;

/* Transport notification only. The ring code under test is not replaced. */
void virtqueue_notify(struct virtqueue *Queue) { Queue->notification_cb(Queue); }
/* Packed-ring entry points are not part of this split-ring test. */
unsigned int vring_control_block_size_packed(u16 Count)
{ UNREFERENCED_PARAMETER(Count); abort(); return 0; }
unsigned long vring_size_packed(unsigned int Count, unsigned long Alignment)
{ UNREFERENCED_PARAMETER(Count); UNREFERENCED_PARAMETER(Alignment); abort(); return 0; }
static void Notify(struct virtqueue *Queue) { UNREFERENCED_PARAMETER(Queue); }

#define CHECK(Condition, Message) do { if (!(Condition)) { \
    printf("FAIL: %s (line %d)\n", Message, __LINE__); Result = 1; goto Done; } } while (0)

static int Exercise(unsigned int Count, ULONG Alignment, UCHAR Poison, BOOLEAN Events)
{
    ULONG RingBytes = vring_size_split(Count, Alignment);
    ULONG ControlBytes = vring_control_block_size((u16)Count, FALSE);
    PUCHAR Allocation = malloc(RingBytes + Alignment + 32);
    PUCHAR Pages = Allocation ? (PUCHAR)ALIGN_UP_BY((ULONG_PTR)Allocation, Alignment) : NULL;
    PUCHAR Control = malloc(ControlBytes + 32);
    struct virtqueue *Queue;
    struct virtqueue_split *Split;
    VirtIODevice Device;
    struct scatterlist Buffers[2];
    ULONG Tokens[128], Heads[128];
    unsigned int i, j, Batch = Count / 2, Completed = 0, Length;
    PVOID Token;
    int Result = 0;

    if (!Allocation || !Control) { free(Allocation); free(Control); return 1; }
    RtlFillMemory(Pages, RingBytes, Poison);
    RtlFillMemory(Pages + RingBytes, 32, 0x7c);
    RtlFillMemory(Control, ControlBytes, Poison);
    RtlFillMemory(Control + ControlBytes, 32, 0x7c);
    RtlZeroMemory(&Device, sizeof(Device));
    Device.addr = Alignment == PAGE_SIZE ? 1 : 0;
    Device.event_suppression_enabled = Events;
    Queue = vring_new_virtqueue_split(0, Count, Alignment, &Device, Pages, Notify, Control);
    CHECK(Queue != NULL, "queue construction");
    CHECK(!virtqueue_has_buf(Queue), "fresh poisoned queue reports a phantom completion");
    CHECK(virtqueue_get_buf(Queue, &Length) == NULL, "fresh queue returned a token");
    Split = splitvq(Queue);
    CHECK(Split->vring.avail->idx == 0 && Split->vring.used->idx == 0, "fresh ring indices");
    Buffers[0].physAddr.QuadPart = 0x100000;
    Buffers[0].length = 24;
    Buffers[1].physAddr.QuadPart = 0x101000;
    Buffers[1].length = 408;
    while (Completed < 66000)
    {
        for (i = 0; i < Batch; ++i)
        {
            Tokens[i] = Completed + i;
            CHECK(virtqueue_add_buf(Queue, Buffers, 1, 1, &Tokens[i], NULL, 0) == 0,
                  "submit descriptor chain");
            Heads[i] = Split->vring.avail->ring[DESC_INDEX(Count, (u16)(Split->vring.avail->idx - 1))];
            CHECK(Heads[i] < Count, "published head range");
        }
        CHECK(virtqueue_add_buf(Queue, Buffers, 1, 1, &Tokens[0], NULL, 0) < 0,
              "full queue must reject another chain");
        /* Complete in reverse submission order, as a device is permitted to do. */
        for (i = 0; i < Batch; ++i)
        {
            ULONG Slot = DESC_INDEX(Count, Split->vring.used->idx);
            Split->vring.used->ring[Slot].id = Heads[Batch - 1 - i];
            Split->vring.used->ring[Slot].len = 408;
            KeMemoryBarrier();
            ++Split->vring.used->idx;
            Token = virtqueue_get_buf(Queue, &Length);
            CHECK(Token == &Tokens[Batch - 1 - i] && Length == 408,
                  "completion head-to-token mapping or response length");
        }
        CHECK(!virtqueue_has_buf(Queue), "drained ring still reports completion");
        CHECK(Split->num_unused == Count, "descriptor reclamation");
        Completed += Batch;
    }
    CHECK(virtqueue_detach_unused_buf(Queue) == NULL, "drained ring has outstanding token");
    virtqueue_shutdown(Queue);
    CHECK(!virtqueue_has_buf(Queue), "shutdown/reinitialize retains completion");
    for (j = 0; j < 32; ++j)
        CHECK(Pages[RingBytes + j] == 0x7c && Control[ControlBytes + j] == 0x7c,
              "ring/control allocation bounds");
    printf("PASS: entries=%u align=%lu poison=%02x events=%u completions=%u (16-bit wrap)\n",
           Count, Alignment, Poison, Events, Completed);
Done:
    free(Allocation);
    free(Control);
    return Result;
}

int main(void)
{
    int Result;
    C_ASSERT(sizeof(struct vring_desc) == 16);
    C_ASSERT(sizeof(struct vring_used_elem) == 8);
    setvbuf(stdout, NULL, _IONBF, 0);
    Result = Exercise(8, 64, 0xa5, FALSE);
    if (Result) return Result;
    Result |= Exercise(8, 64, 0xff, TRUE);
    Result |= Exercise(256, PAGE_SIZE, 0x5a, FALSE);
    Result |= Exercise(256, PAGE_SIZE, 0x01, TRUE);
    return Result;
}
