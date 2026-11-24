/*
 * tests/monitor_test.c - test user32.dll monitor functions
 * Tests: MonitorFromWindow, MonitorFromPoint, MonitorFromRect, GetMonitorInfoW
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/monitor_test.exe tests/monitor_test.c -luser32 -lkernel32
 * 
 * Note: EnumDisplayMonitors callback testing is unregistered due to calling convention
 * complexity in the freestanding bridge implementation. See docs/UNIMPLEMENTED_IMPORTS.txt.
 */
#include <windows.h>
#include <stdint.h>

/* Primary monitor handle (matches bridge/monitor.inc) */
#define PRIMARY_MONITOR ((HMONITOR)(uintptr_t)0x12340042)

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
    
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) fail = 1;

    /* Get function pointers */
    HMONITOR (*from_point)(POINT, DWORD) = (void*)GetProcAddress(user32, "MonitorFromPoint");
    HMONITOR (*from_rect)(LPCRECT, DWORD) = (void*)GetProcAddress(user32, "MonitorFromRect");
    HMONITOR (*from_window)(HWND, DWORD) = (void*)GetProcAddress(user32, "MonitorFromWindow");
    BOOL (*get_info)(HMONITOR, LPMONITORINFO) = (void*)GetProcAddress(user32, "GetMonitorInfoW");
    
    if (!from_point) fail = 1;
    if (!from_rect) fail = 1;
    if (!from_window) fail = 1;
    if (!get_info) fail = 1;
    
    /* Test MonitorFromPoint with DEFAULTTONULL flags */
    POINT pt_in = { 100, 100 };
    POINT pt_out = { 3000, 3000 };
    
    HMONITOR h1 = from_point(pt_in, 0);  /* DEFAULTTONULL */
    if (!h1) { const char *msg = "FAIL: pt_in NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
    
    HMONITOR h2 = from_point(pt_out, 0);  /* DEFAULTTONULL */
    if (h2 != NULL) { const char *msg = "FAIL: pt_out not NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
    
    /* Test MonitorFromPoint with DEFAULTTOPRIMARY */
    HMONITOR h3 = from_point(pt_out, MONITOR_DEFAULTTOPRIMARY);
    if (!h3) { const char *msg = "FAIL: pt_out primary NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
    
    /* Test MonitorFromPoint with DEFAULTTONEAREST */
    HMONITOR h4 = from_point(pt_out, MONITOR_DEFAULTTONEAREST);
    if (!h4) { const char *msg = "FAIL: pt_out nearest NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

    /* Test MonitorFromRect with default flags */
    RECT rt_in = { 0, 0, 1920, 1080 };
    RECT rt_out = { 3000, 3000, 4000, 5000 };
    
    HMONITOR h5 = from_rect(&rt_in, MONITOR_DEFAULTTONULL);
    if (!h5) { const char *msg = "FAIL: rt_in NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
    
    HMONITOR h6 = from_rect(&rt_out, MONITOR_DEFAULTTONULL);
    if (h6 != NULL) { const char *msg = "FAIL: rt_out not NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

    /* Test MonitorFromRect with DEFAULTTOPRIMARY */
    HMONITOR h7 = from_rect(&rt_out, MONITOR_DEFAULTTOPRIMARY);
    if (!h7) { const char *msg = "FAIL: rt_out primary NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

    /* Test MonitorFromWindow with NULL hwnd */
    HMONITOR h8 = from_window(NULL, MONITOR_DEFAULTTONULL);
    if (h8 != NULL) { const char *msg = "FAIL: null DEFAULTTONULL not NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }
    
    HMONITOR h9 = from_window(NULL, MONITOR_DEFAULTTOPRIMARY);
    if (!h9) { const char *msg = "FAIL: null DEFAULTTOPRIMARY NULL\n"; WriteFile(out, msg, my_strlen(msg), &written, NULL); fail=1; }

    /* GetMonitorInfoW returns TRUE with positive dimensions */
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!get_info(PRIMARY_MONITOR, &mi)) fail = 1;
    if (mi.dwFlags != MONITORINFOF_PRIMARY) fail = 1;
    if (mi.rcMonitor.right - mi.rcMonitor.left != 1920) fail = 1;
    if (mi.rcMonitor.bottom - mi.rcMonitor.top != 1080) fail = 1;

    if (fail) ExitProcess(1);
    
    const char *ok_msg = "monitor_test ok\n";
    WriteFile(out, ok_msg, my_strlen(ok_msg), &written, NULL);
    ExitProcess(0);
}