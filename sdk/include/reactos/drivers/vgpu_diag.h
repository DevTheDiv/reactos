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

/* Separate versioned snapshot; the original 432-byte IOCTL stays unchanged. */
#define IOCTL_VGPU_QUERY_VENUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_READ_ACCESS)
#define VGPU_VENUS_VERSION 1
#define VGPU_MAX_CAPSETS 16
#define VGPU_VENUS_PREFIX_BYTES 64
#define VGPU_VENUS_NOT_PROBED 0
#define VGPU_VENUS_NO_3D 1
#define VGPU_VENUS_NO_CAPSET 2
#define VGPU_VENUS_MISSING_FEATURES 3
#define VGPU_VENUS_CAPSET_READY 4
#define VGPU_VENUS_CONTEXT_ACK 5
#define VGPU_VENUS_FAILED 6

typedef struct _VGPU_CAPSET_INFO
{
    ULONG Id, MaxVersion, MaxSize, DataBytes;
} VGPU_CAPSET_INFO;

typedef struct _VGPU_VENUS_INFO
{
    ULONG Size, Version, Gate, Status;
    ULONG ReportedCapsets, CapsetCount, CompletedRequests;
    ULONG LastCommand, LastResponse, ContextAckPairs;
    ULONG VenusDataBytes, VenusPrefixBytes;
    VGPU_CAPSET_INFO Capsets[VGPU_MAX_CAPSETS];
    UCHAR VenusPrefix[VGPU_VENUS_PREFIX_BYTES];
} VGPU_VENUS_INFO;

#define IOCTL_VGPU_QUERY_BLOB \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_READ_ACCESS)
#define VGPU_BLOB_VERSION 1
#define VGPU_BLOB_CYCLES 8
#define VGPU_BLOB_NOT_PROBED 0
#define VGPU_BLOB_UNSUPPORTED 1
#define VGPU_BLOB_WINDOW 2
#define VGPU_BLOB_CONTEXT 3
#define VGPU_BLOB_RESOURCE 4
#define VGPU_BLOB_MAPPED 5
#define VGPU_BLOB_PATTERN 6
#define VGPU_BLOB_COMPLETE 7

/* Boot-time kernel-only probe. No user mappings or resource handles. */
typedef struct _VGPU_BLOB_INFO
{
    ULONG Size, Version, Stage, Status;
    ULONG WindowBar, WindowBytesLow, WindowBytesHigh;
    ULONG CyclesCompleted, PatternBytes, MapCount, UnmapCount;
    ULONG ResourceCreates, ResourceUnrefs, LastCommand, LastResponse, CachePolicy;
    ULONG StaleIdRejections;
} VGPU_BLOB_INFO;

/*
 * Per-file live resource ownership. Only reachable once the boot-time blob
 * probe above has already proven the backend out; every op here is still a
 * bounded, kernel-mediated map/copy/unmap -- no raw VA is ever handed to
 * user mode, only bytes. Each open handle may own up to
 * VGPU_MAX_RESOURCES_PER_FILE resources; IRP_MJ_CLEANUP (handle close or
 * process exit) releases anything that handle still owns.
 */
#define IOCTL_VGPU_QUERY_RESOURCES \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x904, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VGPU_RESOURCE_CREATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x905, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_VGPU_RESOURCE_WRITE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x906, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_VGPU_RESOURCE_READ \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x907, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VGPU_RESOURCE_UNREF \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x908, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define VGPU_RESOURCE_VERSION 1
#define VGPU_MAX_RESOURCES 32
#define VGPU_MAX_RESOURCES_PER_FILE 8
#define VGPU_RESOURCE_BASE_ID 1000
#define VGPU_RESOURCE_BYTES 4096

typedef struct _VGPU_RESOURCE_INFO
{
    ULONG Size, Version, LiveReady;
    ULONG TotalCreates, TotalUnrefs, ActiveResources;
    ULONG LimitRejections, ForeignIdRejections, OverflowRejections, CleanupUnrefs;
} VGPU_RESOURCE_INFO;

typedef struct _VGPU_RESOURCE_WRITE_REQUEST
{
    ULONG ResourceId, Length;
    UCHAR Data[VGPU_RESOURCE_BYTES];
} VGPU_RESOURCE_WRITE_REQUEST, *PVGPU_RESOURCE_WRITE_REQUEST;

typedef struct _VGPU_RESOURCE_READ_REQUEST
{
    ULONG ResourceId, Length;
} VGPU_RESOURCE_READ_REQUEST, *PVGPU_RESOURCE_READ_REQUEST;

/* Aliases the same METHOD_BUFFERED buffer as VGPU_RESOURCE_READ_REQUEST above;
 * handlers must capture ResourceId/Length to locals before writing this. */
typedef struct _VGPU_RESOURCE_READ_RESPONSE
{
    ULONG Length;
    UCHAR Data[VGPU_RESOURCE_BYTES];
} VGPU_RESOURCE_READ_RESPONSE, *PVGPU_RESOURCE_READ_RESPONSE;
