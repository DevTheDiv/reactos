#include <stdio.h>
#include <windows.h>
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("IO probe start\n");
    char buf[65536];
    memset(buf, 'A', sizeof(buf));
    DWORD t0 = GetTickCount();
    for (int i = 0; i < 20; i++)
    {
        char name[64];
        sprintf(name, "C:\\js\\iotest\\file%d.bin", i);
        HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            printf("create %d failed %lu\n", i, GetLastError());
            continue;
        }
        DWORD written;
        BOOL ok = WriteFile(h, buf, sizeof(buf), &written, NULL);
        FlushFileBuffers(h);
        CloseHandle(h);
        printf("file %d: write=%d flush+close done, %lu ms elapsed\n",
               i, ok, GetTickCount() - t0);
    }
    printf("total %lu ms for 20 files x 64KB with flush\n", GetTickCount() - t0);
    return 0;
}
