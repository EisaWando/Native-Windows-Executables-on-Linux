/*
 * tests/fake_handle_test.c - test that shim pointers are not treated as module handles
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/fake_handle_test.exe tests/fake_handle_test.c -lkernel32
 */
#include <windows.h>
#include <stdint.h>

static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* GetProcAddress returns a function pointer (shim) - verify it's not a fake module handle */
typedef void (*shim_fn)(void);

void __cdecl mainCRTStartup(void)
{
    int fail = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;

    /* Get a shim pointer from GetProcAddress */
    shim_fn fn = (shim_fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessId");
    if (!fn) {
        const char *msg = "FAIL: GetProcAddress returned NULL\n";
        WriteFile(out, msg, my_strlen(msg), &written, NULL);
        fail = 1;
    } else {
        /* Verify the shim pointer is not in the fake module handle range */
        uintptr_t ptr = (uintptr_t)fn;
        #define FAKE_HMODULE_BASE 0x6ff00000ULL
        #define FAKE_HMODULE_LIMIT (FAKE_HMODULE_BASE + 0x100000ULL)
        
        if (ptr >= FAKE_HMODULE_BASE && ptr < FAKE_HMODULE_LIMIT) {
            const char *msg = "FAIL: Shim pointer falls in fake module handle range\n";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            fail = 1;
        }
        
        /* Also test that is_fake_module_handle would return false for this pointer */
        if (ptr >= 0x6ff00000ULL && ptr < 0x70000000ULL) {
            const char *msg = "FAIL: Shim pointer overlaps with fake module range\n";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            fail = 1;
        }
    }

    /* Test that fake module handles are in the correct range */
    HANDLE h1 = LoadLibraryExW(L"kernel32.dll", NULL, 0);
    HANDLE h2 = LoadLibraryExW(L"kernel32.dll", NULL, 0);
    if (h1 == NULL || h2 == NULL || h1 != h2) {
        const char *msg = "FAIL: kernel32.dll handle invalid\n";
        WriteFile(out, msg, my_strlen(msg), &written, NULL);
        fail = 1;
    } else {
        uintptr_t h1_val = (uintptr_t)h1;
        if (h1_val < 0x6ff00000ULL || h1_val >= 0x70000000ULL) {
            const char *msg = "FAIL: fake module handle not in expected range [0x6ff00000, 0x70000000)\n";
            WriteFile(out, msg, my_strlen(msg), &written, NULL);
            fail = 1;
        }
    }

    /* Test that invalid DLLs return NULL and don't produce fake handles */
    HANDLE h = LoadLibraryExW(L"nonexistent.dll", NULL, 0);
    if (h != NULL) {
        const char *msg = "FAIL: LoadLibraryExW(nonexistent) returned non-NULL\n";
        WriteFile(out, msg, my_strlen(msg), &written, NULL);
        fail = 1;
    }
    DWORD err = GetLastError();
    if (err != 126) {
        const char *msg = "FAIL: GetLastError != 126 for nonexistent.dll\n";
        WriteFile(out, msg, my_strlen(msg), &written, NULL);
        fail = 1;
    }

    if (fail) ExitProcess(1);
    
    const char *ok_msg = "fake_handle_test ok\n";
    WriteFile(out, ok_msg, my_strlen(ok_msg), &written, NULL);
    ExitProcess(0);
}