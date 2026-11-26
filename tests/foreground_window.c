/*
 * tests/foreground_window.c - test SetForegroundWindow/GetForegroundWindow
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/foreground_window.exe tests/foreground_window.c -luser32 -lkernel32
 */
#include <windows.h>
#include <stdint.h>

static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void __cdecl mainCRTStartup(void)
{
    int fail = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) fail = 1;

    HWND (*set_fg)(HWND) = (void*)GetProcAddress(user32, "SetForegroundWindow");
    HWND (*get_fg)(void) = (void*)GetProcAddress(user32, "GetForegroundWindow");
    DWORD (*get_last_error)(void) = (void*)GetProcAddress(user32, "GetLastError");
    
    if (!set_fg) fail = 1;
    if (!get_fg) fail = 1;
    if (!get_last_error) fail = 1;

    /* Register a window class */
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"FGTestClass";
    if (!RegisterClassExW(&cls)) { const char *msg = "FAIL: RegisterClassExW failed\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

    if (!fail) {
        /* Test 1: Invalid hwnd should return NULL and set ERROR_INVALID_WINDOW_HANDLE */
        SetLastError(0);
        HWND r = set_fg((HWND)0x12345678);
        DWORD err = get_last_error();
        if (r != NULL) { const char *msg = "FAIL: SetForegroundWindow invalid hwnd returned non-NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
        if (err != 1400) { const char *msg = "FAIL: GetLastError != ERROR_INVALID_WINDOW_HANDLE (1400)\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

        /* Test 2: NULL hwnd should return NULL */
        SetLastError(0);
        r = set_fg(NULL);
        err = get_last_error();
        if (r != NULL) { const char *msg = "FAIL: SetForegroundWindow NULL hwnd returned non-NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

        /* Test 3: GetForegroundWindow initially returns NULL (no foreground window) */
        HWND fg = get_fg();
        if (fg != NULL) { const char *msg = "FAIL: GetForegroundWindow initially non-NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

        /* Test 4: Create a window and set it as foreground */
        HWND hwnd = CreateWindowExW(0, L"FGTestClass", L"Test", WS_OVERLAPPEDWINDOW,
                                    100, 100, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!hwnd) { const char *msg = "FAIL: CreateWindowExW failed\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
        else {
            /* Debug: check if window is valid */
            BOOL iswin = IsWindow(hwnd);
            if (!iswin) { const char *msg = "FAIL: IsWindow returned FALSE\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
            
            /* Test: SetForegroundWindow returns previous foreground (NULL initially) */
            HWND r2 = set_fg(hwnd);
            if (r2 != NULL) { const char *msg = "FAIL: SetForegroundWindow first call didn't return NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
            
            /* GetForegroundWindow should return the window we just set */
            fg = get_fg();
            if (fg != hwnd) { const char *msg = "FAIL: GetForegroundWindow didn't return set window\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
            
            /* Setting again should return the previous window (the one we just set) */
            HWND prev = set_fg(hwnd);
            if (prev != hwnd) { const char *msg = "FAIL: SetForegroundWindow same window didn't return it\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
            
            /* Setting a different window should return the current one */
            HWND hwnd2 = CreateWindowExW(0, L"FGTestClass", L"Test2", WS_OVERLAPPEDWINDOW,
                                        200, 200, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
            if (hwnd2) {
                HWND prev2 = set_fg(hwnd2);
                if (prev2 != hwnd) { const char *msg = "FAIL: SetForegroundWindow different window didn't return previous\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
                fg = get_fg();
                if (fg != hwnd2) { const char *msg = "FAIL: GetForegroundWindow didn't return second window\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
                DestroyWindow(hwnd2);
            }
            DestroyWindow(hwnd);
        }
    }

    if (fail) ExitProcess(1);
    
    const char *ok_msg = "foreground_window ok\n";
    WriteFile(out, ok_msg, my_strlen(ok_msg), &written, NULL);
    ExitProcess(0);
}