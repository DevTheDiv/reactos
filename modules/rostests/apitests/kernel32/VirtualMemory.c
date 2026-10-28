/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "precomp.h"

START_TEST(VirtualMemory)
{
    static const DWORD InvalidProtections[] =
    {
        PAGE_READWRITE | PAGE_TARGETS_INVALID,
        PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID | 0x80000000,
        PAGE_EXECUTE_READWRITE | PAGE_READWRITE | PAGE_TARGETS_INVALID
    };
    PVOID Memory;
    DWORD OldProtection;
    MEMORY_BASIC_INFORMATION Info;
    unsigned int i;

    Memory = VirtualAllocEx(GetCurrentProcess(), NULL, 0x10000,
                            MEM_RESERVE, PAGE_NOACCESS);
    ok(Memory != NULL, "Reserve failed: %lu\n", GetLastError());
    if (!Memory) return;

    ok(VirtualAlloc(Memory, 0x1000, MEM_COMMIT,
                    PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID) == Memory,
       "Commit with CFG modifier failed: %lu\n", GetLastError());
    ok(VirtualQuery(Memory, &Info, sizeof(Info)) == sizeof(Info),
       "VirtualQuery failed\n");
    ok(Info.Protect == PAGE_EXECUTE_READWRITE,
       "Unexpected page protection: %#lx\n", Info.Protect);
    ok(VirtualProtectEx(GetCurrentProcess(), Memory, 0x1000,
                        PAGE_EXECUTE_READ | PAGE_TARGETS_NO_UPDATE,
                        &OldProtection),
       "Protect with CFG modifier failed: %lu\n", GetLastError());
    ok(OldProtection == PAGE_EXECUTE_READWRITE,
       "Unexpected previous protection: %#lx\n", OldProtection);

    for (i = 0; i < RTL_NUMBER_OF(InvalidProtections); ++i)
    {
        ok(VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT,
                        InvalidProtections[i]) == NULL,
           "Invalid allocation protection accepted: %#lx\n", InvalidProtections[i]);
        ok(!VirtualProtect(Memory, 0x1000, InvalidProtections[i], &OldProtection),
           "Invalid protection accepted: %#lx\n", InvalidProtections[i]);
    }
    ok(VirtualFree(Memory, 0, MEM_RELEASE), "VirtualFree failed\n");
}
