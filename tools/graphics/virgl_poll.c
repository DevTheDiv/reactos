/* SPDX-License-Identifier: MIT
 * Experimental Windows-host diagnostic adapter. Other exports are PE forwarders.
 * Avoid eventfd-dependent fencing in the pinned Windows renderer. QEMU's normal
 * renderer polling remains active. This is not a production scheduling fix.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

static FARPROC Resolve(const char *Name)
{
    HMODULE Module = GetModuleHandleW(L"libvirglrenderer-original.dll");
    if (!Module) Module = LoadLibraryW(L"libvirglrenderer-original.dll");
    return Module ? GetProcAddress(Module, Name) : NULL;
}

__declspec(dllexport) int
virgl_renderer_init(void *Cookie, int Flags, void *Callbacks)
{
    typedef int (*INIT)(void *, int, void *);
    INIT Init = (INIT)Resolve("virgl_renderer_init");
    /* virglrenderer API flags: THREAD_SYNC=1<<1, ASYNC_FENCE_CB=1<<8. */
    int PollFlags = Flags & ~((1 << 1) | (1 << 8));
    if (!Init) return -1;
    fprintf(stderr, "vgpu-poll: renderer flags=%x -> %x\n", Flags, PollFlags);
    return Init(Cookie, PollFlags, Callbacks);
}

__declspec(dllexport) int
virgl_renderer_context_create_with_flags(uint32_t Id, uint32_t Flags,
                                         uint32_t Length, const char *Name)
{
    typedef int (*CREATE)(uint32_t, uint32_t, uint32_t, const char *);
    CREATE Create = (CREATE)Resolve("virgl_renderer_context_create_with_flags");
    int Status = Create ? Create(Id, Flags, Length, Name) : -1;
    fprintf(stderr, "vgpu-poll: context=%u capset=%u renderer_status=%d\n", Id, Flags, Status);
    return Status;
}
