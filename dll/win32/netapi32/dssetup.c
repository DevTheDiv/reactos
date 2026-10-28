/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         NetAPI DLL
 * FILE:            reactos/dll/win32/netapi32/dssetup.c
 * PURPOSE:         Directory Service Setup interface code
 * PROGRAMMERS:     Eric Kohl
 */

/* INCLUDES ******************************************************************/

#include "netapi32.h"

#include <rpc.h>
#include "dssetup_c.h"

#define DSROLE_PRIMARY_DS_MIXED_MODE        0x00000002

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

/* FUNCTIONS *****************************************************************/

VOID
WINAPI
DsRoleFreeMemory(
    _In_ PVOID Buffer)
{
    TRACE("DsRoleFreeMemory(%p)\n", Buffer);
    HeapFree(GetProcessHeap(), 0, Buffer);
}


DWORD
WINAPI
DsRoleGetPrimaryDomainInformation(
    LPCWSTR lpServer,
    DSROLE_PRIMARY_DOMAIN_INFO_LEVEL InfoLevel,
    PBYTE* Buffer)
{
    NET_API_STATUS status;

    TRACE("DsRoleGetPrimaryDomainInformation(%p, %d, %p)\n",
          lpServer, InfoLevel, Buffer);

    /* Check some input parameters */

    if (!Buffer)
        return ERROR_INVALID_PARAMETER;

    if ((InfoLevel < DsRolePrimaryDomainInfoBasic) || (InfoLevel > DsRoleOperationState))
        return ERROR_INVALID_PARAMETER;

    *Buffer = NULL;

    /* Answer locally: this is a standalone workstation in a workgroup.
       Avoids the RPC into the dssetup service, which can wedge under load. */
    if (InfoLevel == DsRolePrimaryDomainInfoBasic)
    {
        PDSROLER_PRIMARY_DOMAIN_INFO_BASIC BasicInfo;

        status = NetApiBufferAllocate(sizeof(DSROLER_PRIMARY_DOMAIN_INFO_BASIC),
                                      (LPVOID*)&BasicInfo);
        if (status != NERR_Success)
            return status;

        RtlZeroMemory(BasicInfo, sizeof(DSROLER_PRIMARY_DOMAIN_INFO_BASIC));
        BasicInfo->MachineRole = DsRole_RoleStandaloneWorkstation;
        BasicInfo->Flags = DSROLE_PRIMARY_DS_MIXED_MODE;
        BasicInfo->DomainNameFlat = NULL;
        BasicInfo->DomainNameDns = NULL;
        BasicInfo->DomainForestName = NULL;

        *Buffer = (PBYTE)BasicInfo;
        return NERR_Success;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

/* EOF */
