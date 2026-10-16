/*
 * PROJECT:     ReactOS Drivers
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Public IOCTL interface between the SAC driver (\DosDevices\SAC)
 *              and a usermode "cmd" channel helper. Safe to include from both
 *              kernel and usermode code -- no ntifs.h/ntddk.h dependency.
 */

#ifndef _SACIOCTL_H_
#define _SACIOCTL_H_

#define SAC_IOCTL_NAME_SIZE        64
#define SAC_IOCTL_DESCRIPTION_SIZE 256

//
// Creates a new SAC channel of type "Cmd" (plain byte passthrough, no VT100
// processing) owned by the calling process. Input is SAC_IOCTL_CHANNEL_CREATE,
// there is no output.
//
#define IOCTL_SAC_CHANNEL_CREATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// Reads bytes the user typed into this channel (while it is the active
// channel) since the last read -- i.e. data to forward to a child process'
// stdin. No input buffer; output is raw bytes, return length is however much
// was actually available (can be 0).
//
#define IOCTL_SAC_CHANNEL_READ \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// Writes bytes into this channel's output (e.g. a child process' stdout/
// stderr) for display whenever this channel is/becomes the active one.
// Input is the raw bytes to write; no output.
//
#define IOCTL_SAC_CHANNEL_WRITE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// Blocks (STATUS_PENDING) until the SAC console's "cmd" command is typed by
// the user, at which point it completes. Only one caller may have this
// request outstanding at a time. No input/output.
//
#define IOCTL_SAC_WAIT_CMD_REQUEST \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _SAC_IOCTL_CHANNEL_CREATE
{
    WCHAR NameBuffer[SAC_IOCTL_NAME_SIZE + 1];
    WCHAR DescriptionBuffer[SAC_IOCTL_DESCRIPTION_SIZE + 1];
} SAC_IOCTL_CHANNEL_CREATE, *PSAC_IOCTL_CHANNEL_CREATE;

#endif /* _SACIOCTL_H_ */
