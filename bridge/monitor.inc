/* Monitor API implementation for user32.dll multimon support.
 * Minimal headless implementation: returns a synthetic primary monitor.
 */

/* Standard Windows types */
typedef int32_t INT;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef void *HWND;
typedef void *HMONITOR;
typedef uint16_t WCHAR;
#define FALSE 0
#define TRUE 1

/* RECT structure */
typedef struct tagRECT {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} RECT, *LPRECT, * const LPCRECT;

/* POINT structure */
typedef struct tagPOINT {
    int32_t x;
    int32_t y;
} POINT, *LPPOINT;

/* MONITORINFO structure */
typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

/* MONITORINFOEXW structure (with device name) */
#define CCHDEVICENAME 32
typedef struct tagMONITORINFOEXW {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
    WCHAR szDevice[CCHDEVICENAME];
} MONITORINFOEXW, *LPMONITORINFOEXW;

/* Monitor flags */
#define MONITORINFOF_PRIMARY 0x00000001

/* Primary monitor handle (synthetic value matching multimon.h) */
#define PRIMARY_MONITOR_HANDLE ((HMONITOR)(uintptr_t)0x12340042)

/* Screen dimensions (same as GetSystemMetrics SM_CXSCREEN=1920, SM_CYSCREEN=1080) */
#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

/* MonitorFromPoint - returns primary monitor handle */
__attribute__((ms_abi)) HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    (void)pt;
    (void)dwFlags;
    return PRIMARY_MONITOR_HANDLE;
}

/* MonitorFromRect - returns primary monitor handle */
__attribute__((ms_abi)) HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags)
{
    (void)lprc;
    (void)dwFlags;
    return PRIMARY_MONITOR_HANDLE;
}

/* MonitorFromWindow - returns primary monitor handle for any valid window */
__attribute__((ms_abi)) HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags)
{
    (void)hwnd;
    (void)dwFlags;
    return PRIMARY_MONITOR_HANDLE;
}

/* GetMonitorInfoW - fills in monitor info for primary monitor */
__attribute__((ms_abi)) BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    if (!lpmi) {
        return FALSE;
    }
    if (hMonitor != PRIMARY_MONITOR_HANDLE) {
        return FALSE;
    }
    /* Handle both MONITORINFO and MONITORINFOEXW based on cbSize */
    if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
        LPMONITORINFOEXW lpmiex = (LPMONITORINFOEXW)lpmi;
        lpmiex->cbSize = sizeof(MONITORINFOEXW);
        lpmiex->rcMonitor.left = 0;
        lpmiex->rcMonitor.top = 0;
        lpmiex->rcMonitor.right = SCREEN_WIDTH;
        lpmiex->rcMonitor.bottom = SCREEN_HEIGHT;
        lpmiex->rcWork.left = 0;
        lpmiex->rcWork.top = 0;
        lpmiex->rcWork.right = SCREEN_WIDTH;
        lpmiex->rcWork.bottom = SCREEN_HEIGHT;
        lpmiex->dwFlags = MONITORINFOF_PRIMARY;
        /* Device name: \\.\DISPLAY1 (10 wide chars including null) */
        static const uint16_t display_name[] = { 0x005c, 0x002e, 0x005c, 0x0044, 0x0049, 0x0053, 0x0050, 0x004c, 0x0041, 0x0059, 0x0031, 0 };
        memcpy(lpmiex->szDevice, display_name, sizeof(display_name));
    } else {
        lpmi->cbSize = sizeof(MONITORINFO);
        lpmi->rcMonitor.left = 0;
        lpmi->rcMonitor.top = 0;
        lpmi->rcMonitor.right = SCREEN_WIDTH;
        lpmi->rcMonitor.bottom = SCREEN_HEIGHT;
        lpmi->rcWork.left = 0;
        lpmi->rcWork.top = 0;
        lpmi->rcWork.right = SCREEN_WIDTH;
        lpmi->rcWork.bottom = SCREEN_HEIGHT;
        lpmi->dwFlags = MONITORINFOF_PRIMARY;
    }
    return TRUE;
}