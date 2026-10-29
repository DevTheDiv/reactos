/* SPDX-License-Identifier: GPL-2.0-or-later
 * Virtio GPU wire layouts, little endian on the supported i386 guest.
 * Protocol reference: Linux include/uapi/linux/virtio_gpu.h and Virtio 1.2.
 */
#pragma once
#define VGPU_GET_DISPLAY_INFO 0x0100
#define VGPU_GET_CAPSET_INFO 0x0108
#define VGPU_GET_CAPSET 0x0109
#define VGPU_CTX_CREATE 0x0200
#define VGPU_CTX_DESTROY 0x0201
#define VGPU_RESP_NODATA 0x1100
#define VGPU_RESP_DISPLAY_INFO 0x1101
#define VGPU_RESP_CAPSET_INFO 0x1102
#define VGPU_RESP_CAPSET 0x1103
#define VGPU_FLAG_FENCE 1
#define VGPU_FEATURE_VIRGL (1ULL << 0)
#define VGPU_FEATURE_BLOB (1ULL << 3)
#define VGPU_FEATURE_CONTEXT_INIT (1ULL << 4)
#define VGPU_VENUS_FEATURES (VGPU_FEATURE_VIRGL | VGPU_FEATURE_BLOB | VGPU_FEATURE_CONTEXT_INIT)
#define VGPU_CAPSET_VENUS 4
#define VGPU_RESPONSE_OFFSET 128
#define VGPU_MAX_VENUS_DATA 2048
#define VGPU_CONTEXT_CYCLES 8

typedef struct _GPU_HEADER
{
    ULONG Type, Flags;
    ULONGLONG Fence;
    ULONG Context;
    UCHAR Ring, Padding[3];
} GPU_HEADER;
typedef struct _GPU_CAPSET_REQUEST
{
    GPU_HEADER Header;
    ULONG IndexOrId, Version;
} GPU_CAPSET_REQUEST;
typedef struct _GPU_CAPSET_RESPONSE
{
    GPU_HEADER Header;
    ULONG Id, MaxVersion, MaxSize, Padding;
} GPU_CAPSET_RESPONSE;
typedef struct _GPU_CONTEXT_REQUEST
{
    GPU_HEADER Header;
    ULONG NameLength, Init;
    CHAR Name[64];
} GPU_CONTEXT_REQUEST;

#define VGPU_REPLY_OK 0
#define VGPU_REPLY_DEVICE_ERROR 1
#define VGPU_REPLY_MALFORMED 2
/* Shared with the native regression: never inspect a short header. */
static __inline int
VgpuCheckReply(const GPU_HEADER *Reply, ULONG Used, ULONG ExpectedBytes,
               ULONG ExpectedType, ULONGLONG Fence)
{
    if (Used < sizeof(*Reply) || Reply->Flags != VGPU_FLAG_FENCE || Reply->Fence != Fence)
        return VGPU_REPLY_MALFORMED;
    if (Reply->Type >= 0x1200 && Reply->Type <= 0x1205 && Used == sizeof(*Reply))
        return VGPU_REPLY_DEVICE_ERROR;
    if (Reply->Type != ExpectedType || Used != ExpectedBytes)
        return VGPU_REPLY_MALFORMED;
    return VGPU_REPLY_OK;
}
