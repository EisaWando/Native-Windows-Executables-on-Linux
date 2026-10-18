/*
 * tests/hello.c - minimal Win32 hello-world for binfmt_pe testing.
 * Imports only: GetStdHandle, WriteFile, ExitProcess.
 * Entry: mainCRTStartup (no CRT; we are the CRT).
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/hello.exe tests/hello.c -lkernel32
 */
#include <windows.h>

static const char msg[] = "Hello from PE!\r\n";

void __cdecl mainCRTStartup(void)
{
    HANDLE h;
    DWORD written;

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(h, msg, sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);
}
