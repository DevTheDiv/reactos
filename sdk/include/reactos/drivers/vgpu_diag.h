/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#define IOCTL_VGPU_QUERY_DIAGNOSTICS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_READ_ACCESS)
#define VGPU_DIAG_VERSION 1
#define VGPU_DIAG_MAX_SCANOUTS 16
#define VGPU_DIAG_DMA_QUARANTINED 1

typedef struct _VGPU_DIAG_SCANOUT
{
    ULONG X, Y, Width, Height, Enabled, Flags;
} VGPU_DIAG_SCANOUT;

/* Experimental read-only diagnostics, not a rendering/submission ABI. */
typedef struct _VGPU_DIAG_INFO
{
    ULONG Size, Version, Stage, Status, ResetStatus, Flags;
    ULONG HostFeaturesLow, HostFeaturesHigh;
    ULONG GuestFeaturesLow, GuestFeaturesHigh;
    ULONG CompletedRequests, EnabledScanouts;
    VGPU_DIAG_SCANOUT Scanouts[VGPU_DIAG_MAX_SCANOUTS];
} VGPU_DIAG_INFO;

#define VGPU_STAGE_CREATED 0
#define VGPU_STAGE_BUS 1
#define VGPU_STAGE_TRANSPORT 2
#define VGPU_STAGE_FEATURES 3
#define VGPU_STAGE_QUEUE 4
#define VGPU_STAGE_COMMAND 5
#define VGPU_STAGE_COMPLETE 6
