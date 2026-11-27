/*
 * tests/builtin_dlls.c - test that removed builtin DLLs return NULL
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/builtin_dlls.exe tests/builtin_dlls.c -lkernel32
 */
#include <windows.h>
#include <stdint.h>

static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void __cdecl mainCRTStartup(void)
{
    int fail = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;

    /* Test LoadLibraryExW for removed DLLs */
    const uint16_t *dlls[] = {
        L"shcore.dll",
        L"d2d1.dll",
        L"dwrite.dll",
        L"d3d11.dll",
        NULL
    };

    for (int i = 0; dlls[i]; i++) {
        HANDLE h = LoadLibraryExW(dlls[i], NULL, 0);
        if (h != NULL) {
            const char *msg = "FAIL: LoadLibraryExW returned non-NULL for ";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            WriteFile(out, (const char *)dlls[i], my_strlen((const char *)dlls[i]) * 2, &written, NULL);
            WriteFile(out, "\n", 1, &written, NULL);
            fail = 1;
        }
        DWORD err = GetLastError();
        if (err != 126) {
            const char *msg = "FAIL: GetLastError != 126 for ";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            WriteFile(out, (const char *)dlls[i], my_strlen((const char *)dlls[i]) * 2, &written, NULL);
            WriteFile(out, "\n", 1, &written, NULL);
            fail = 1;
        }
    }

    /* Test GetModuleHandleW for removed DLLs */
    for (int i = 0; dlls[i]; i++) {
        HANDLE h = GetModuleHandleW(dlls[i]);
        if (h != NULL) {
            const char *msg = "FAIL: GetModuleHandleW returned non-NULL for ";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            WriteFile(out, (const char *)dlls[i], my_strlen((const char *)dlls[i]) * 2, &written, NULL);
            WriteFile(out, "\n", 1, &written, NULL);
            fail = 1;
        }
    }

    /* Test that original builtin modules still work */
    HANDLE h1 = LoadLibraryExW(L"kernel32.dll", NULL, 0);
    if (!h1) { const char *msg = "FAIL: kernel32.dll not found\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail = 1; }
    HANDLE h2 = LoadLibraryExW(L"user32.dll", NULL, 0);
    if (!h2) { const char *msg = "FAIL: user32.dll not found\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail = 1; }
    HANDLE h3 = LoadLibraryExW(L"gdi32.dll", NULL, 0);
    if (!h3) { const char *msg = "FAIL: gdi32.dll not found\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail = 1; }
    HANDLE h4 = LoadLibraryExW(L"ntdll.dll", NULL, 0);
    if (!h4) { const char *msg = "FAIL: ntdll.dll not found\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail = 1; }
    HANDLE h5 = LoadLibraryExW(L"kernelbase.dll", NULL, 0);
    if (!h5) { const char *msg = "FAIL: kernelbase.dll not found\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail = 1; }

    if (fail) ExitProcess(1);
    
    const char *ok_msg = "builtin_dlls ok\n";
    WriteFile(out, ok_msg, my_strlen(ok_msg), &written, NULL);
    ExitProcess(0);
}