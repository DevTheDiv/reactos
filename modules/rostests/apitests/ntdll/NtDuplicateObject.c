/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtDuplicateObject
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

#define OBJ_PROTECT_CLOSE 0x01

static VOID TestAnonymousSectionDacl(BOOLEAN NullDacl)
{
    SECURITY_DESCRIPTOR Descriptor;
    ACL Dacl;
    OBJECT_ATTRIBUTES Attributes;
    LARGE_INTEGER Size;
    HANDLE Section = NULL, ReadOnly = NULL, Duplicate = NULL;
    NTSTATUS Status;

    Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    Status = RtlCreateAcl(&Dacl, sizeof(Dacl), ACL_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE,
                                         NullDacl ? NULL : &Dacl, FALSE);
    ok_hex(Status, STATUS_SUCCESS);
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, &Descriptor);
    Size.QuadPart = 0x10000;
    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, &Attributes, &Size,
                              PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = NtDuplicateObject(NtCurrentProcess(), Section, NtCurrentProcess(),
                                &ReadOnly, SECTION_QUERY | SECTION_MAP_READ, 0, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        /* Reducing rights does not require a new DACL access check. */
        Status = NtDuplicateObject(NtCurrentProcess(), ReadOnly, NtCurrentProcess(),
                                    &Duplicate, SECTION_MAP_READ, 0, 0);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) NtClose(Duplicate);

        /* An empty DACL must deny rights absent from the source handle;
         * a NULL DACL permits them. The section has no object name. */
        Duplicate = NULL;
        Status = NtDuplicateObject(NtCurrentProcess(), ReadOnly, NtCurrentProcess(),
                                    &Duplicate, SECTION_MAP_WRITE, 0, 0);
        ok_hex(Status, NullDacl ? STATUS_SUCCESS : STATUS_ACCESS_DENIED);
        if (NT_SUCCESS(Status)) NtClose(Duplicate);
        NtClose(ReadOnly);
    }

    /* The creator's original write rights remain valid even with an empty DACL. */
    Status = NtDuplicateObject(NtCurrentProcess(), Section, NtCurrentProcess(),
                                &Duplicate, SECTION_MAP_WRITE, 0, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) NtClose(Duplicate);
    NtClose(Section);
}

START_TEST(NtDuplicateObject)
{
    NTSTATUS Status;
    HANDLE Handle;

    TestAnonymousSectionDacl(FALSE);
    TestAnonymousSectionDacl(TRUE);

    Handle = NULL;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               NtCurrentProcess(),
                               NtCurrentProcess(),
                               &Handle,
                               GENERIC_ALL,
                               OBJ_PROTECT_CLOSE,
                               0);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Handle != NULL && Handle != NtCurrentProcess(),
        "Handle = %p\n", Handle);
    Status = NtClose(Handle);
    ok_hex(Status, STATUS_HANDLE_NOT_CLOSABLE);
}
