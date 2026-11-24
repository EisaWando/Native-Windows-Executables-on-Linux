/*
 * tests/user32_dll.c - test user32.dll and gdi32.dll are builtin modules
 * Tests: GetModuleHandleW returns non-NULL for user32.dll and gdi32.dll
 *        GetProcAddress(user32,"GetSystemMetrics") returns valid address
 *        GetProcAddress(kernel32,"GetSystemMetrics") returns NULL (not in kernel32)
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/user32_dll.exe tests/user32_dll.c -lkernel32
 */
#include <windows.h>

void __cdecl mainCRTStartup(void)
{
    int fail = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;

    /* Test 1: GetModuleHandleW for user32.dll should return non-NULL */
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) fail = 1;

    /* Test 2: GetModuleHandleW for gdi32.dll should return non-NULL */
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!gdi32) fail = 1;

    /* Test 3: GetModuleHandleW for kernel32.dll should return non-NULL
       (it IS a builtin module) */
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) fail = 1;

    /* Test 4: user32.dll and gdi32.dll handles must be different */
    if (user32 == gdi32) fail = 1;

    /* Test 5: GetProcAddress(user32,"GetSystemMetrics") should return non-NULL */
    int (*metrics_user32)(int) = (int(*)(int))GetProcAddress(user32, "GetSystemMetrics");
    if (!metrics_user32) fail = 1;

    /* Test 6: GetProcAddress(kernel32,"GetSystemMetrics") should return NULL
       (GetSystemMetrics is not in kernel32.dll) */
    int (*metrics_kernel32)(int) = (int(*)(int))GetProcAddress(kernel32, "GetSystemMetrics");
    if (metrics_kernel32) fail = 1;

    WriteFile(out, fail ? "user32_dll FAIL\r\n" : "user32_dll ok\r\n",
              fail ? sizeof("user32_dll FAIL\r\n") - 1 : sizeof("user32_dll ok\r\n") - 1, &written, NULL);
    ExitProcess(fail ? 1 : 0);
}