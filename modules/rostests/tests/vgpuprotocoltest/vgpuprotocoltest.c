/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include "../../../../drivers/virtio/vgpudiag/protocol.h"

#define CHECK(Value, Expected) do { ++Count; if ((Value) != (Expected)) { \
    printf("FAIL: response case %u at line %d\n", Count, __LINE__); return 1; } } while (0)
int main(void)
{
    GPU_HEADER Reply = {0};
    ULONGLONG Fence = 0x1234567887654321ULL;
    unsigned int Count = 0;
    CHECK(sizeof(GPU_HEADER), 24);
    CHECK(sizeof(GPU_CAPSET_REQUEST), 32);
    CHECK(sizeof(GPU_CAPSET_RESPONSE), 40);
    CHECK(sizeof(GPU_CONTEXT_REQUEST), 96);
    CHECK(sizeof(GPU_BLOB_REQUEST), 56);
    CHECK(sizeof(GPU_MAP_REQUEST), 40);
    CHECK(sizeof(GPU_MAP_RESPONSE), 32);
    CHECK(VgpuWindowFits(4096, 0, 4096), 1);
    CHECK(VgpuWindowFits(4096, 4095, 1), 1);
    CHECK(VgpuWindowFits(4096, 4096, 1), 0);
    CHECK(VgpuWindowFits(4096, 0, 0), 0);
    CHECK(VgpuWindowFits(4096, 1, 4096), 0);
    CHECK(VgpuWindowFits(~0ULL, ~0ULL - 4096, 4096), 1);
    CHECK(VgpuWindowFits(~0ULL, ~0ULL - 4096, 4097), 0);
    CHECK(VgpuWindowFits(4096, ~0ULL - 1, 4), 0);
    CHECK(VgpuCheckReply(NULL, 0, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    Reply.Type = VGPU_RESP_CAPSET_INFO;
    Reply.Flags = VGPU_FLAG_FENCE;
    Reply.Fence = Fence;
    CHECK(VgpuCheckReply(&Reply, 40, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_OK);
    CHECK(VgpuCheckReply(&Reply, 23, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    CHECK(VgpuCheckReply(&Reply, 39, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    CHECK(VgpuCheckReply(&Reply, 41, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    CHECK(VgpuCheckReply(&Reply, 40, 40, VGPU_RESP_CAPSET, Fence), VGPU_REPLY_MALFORMED);
    CHECK(VgpuCheckReply(&Reply, 40, 40, VGPU_RESP_CAPSET_INFO, (ULONG)Fence), VGPU_REPLY_MALFORMED);
    Reply.Flags = 0;
    CHECK(VgpuCheckReply(&Reply, 40, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    Reply.Flags = VGPU_FLAG_FENCE;
    Reply.Type = 0x1200;
    CHECK(VgpuCheckReply(&Reply, 24, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_DEVICE_ERROR);
    CHECK(VgpuCheckReply(&Reply, 40, 40, VGPU_RESP_CAPSET_INFO, Fence), VGPU_REPLY_MALFORMED);
    Reply.Type = 0x1205;
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, Fence), VGPU_REPLY_DEVICE_ERROR);
    Reply.Type = 0x1206;
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, Fence), VGPU_REPLY_MALFORMED);
    Reply.Type = VGPU_RESP_NODATA;
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, Fence), VGPU_REPLY_OK);
    Reply.Flags = 0;
    Reply.Fence = 0;
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, 0), VGPU_REPLY_OK);
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, Fence), VGPU_REPLY_MALFORMED);
    Reply.Flags = VGPU_FLAG_FENCE;
    CHECK(VgpuCheckReply(&Reply, 24, 24, VGPU_RESP_NODATA, 0), VGPU_REPLY_MALFORMED);
    printf("PASS: %u protocol layout/response cases (length, type, 64-bit fence, flags, device errors)\n", Count);
    return 0;
}
