/*
 * tests/monitor_test.c - test user32.dll monitor functions
 * Tests: MonitorFromWindow, MonitorFromPoint, MonitorFromRect, GetMonitorInfoW
 * Build:
 *   x86_64-w64-mingw32-gcc -nostdlib -o tests/monitor_test.exe tests/monitor_test.c -luser32 -lkernel32
 */
#include <windows.h>
#include <stdio.h>

void __cdecl mainCRTStartup(void)
{
    int fail = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;

    /* Test 1: GetModuleHandleW for user32.dll */
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) fail = 1;

    /* Test 2: GetProcAddress for monitor functions should return non-NULL */
    HMONITOR (*from_point)(POINT, DWORD) = (void*)GetProcAddress(user32, "MonitorFromPoint");
    HMONITOR (*from_rect)(LPCRECT, DWORD) = (void*)GetProcAddress(user32, "MonitorFromRect");
    HMONITOR (*from_window)(HWND, DWORD) = (void*)GetProcAddress(user32, "MonitorFromWindow");
    BOOL (*get_info)(HMONITOR, LPMONITORINFO) = (void*)GetProcAddress(user32, "GetMonitorInfoW");
    
    if (!from_point) fail = 1;
    if (!from_rect) fail = 1;
    if (!from_window) fail = 1;
    if (!get_info) fail = 1;

    /* Test 3: MonitorFromPoint returns non-NULL handle */
    POINT pt = { 100, 100 };
    HMONITOR hMon = from_point(pt, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon) fail = 1;

    /* Test 4: MonitorFromRect returns non-NULL handle */
    RECT rt = { 0, 0, 1920, 1080 };
    HMONITOR hMon2 = from_rect(&rt, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon2) fail = 1;

    /* Test 5: MonitorFromWindow returns non-NULL handle */
    HMONITOR hMon3 = from_window(NULL, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon3) fail = 1;

    /* Test 6: GetMonitorInfoW returns TRUE with positive dimensions */
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!get_info(hMon, &mi)) fail = 1;
    if (mi.rcMonitor.right <= mi.rcMonitor.left || mi.rcMonitor.bottom <= mi.rcMonitor.top) fail = 1;
    if (!(mi.dwFlags & MONITORINFOF_PRIMARY)) fail = 1;
    if (mi.rcMonitor.right - mi.rcMonitor.left != 1920) fail = 1;
    if (mi.rcMonitor.bottom - mi.rcMonitor.top != 1080) fail = 1;

    WriteFile(out, fail ? "monitor_test FAIL\r\n" : "monitor_test ok\r\n",
              fail ? sizeof("monitor_test FAIL\r\n") - 1 : sizeof("monitor_test ok\r\n") - 1, &written, NULL);
    ExitProcess(fail ? 1 : 0);
}