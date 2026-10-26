/* tests/dc_saved.c — unit test for SaveDC/RestoreDC, ViewportOrg, WindowOrg, MapMode, ClipBox */
#include <windows.h>

static void failed(unsigned line)
{
    char buf[64];
    int n = 0;
    const char *msg = "FAIL dc_saved.c line ";
    for (; msg[n]; n++) buf[n] = msg[n];
    unsigned v = line;
    char rev[12]; int r = 0;
    do { rev[r++] = '0' + v % 10; v /= 10; } while (v);
    while (r > 0) buf[n++] = rev[--r];
    buf[n++] = '\n';
    DWORD w; WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, n, &w, NULL);
    ExitProcess(1);
}
#define CHK(x) do { if (!(x)) failed(__LINE__); } while (0)

void mainCRTStartup(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    CHK(dc != NULL);
    HBITMAP bm = CreateCompatibleBitmap(dc, 100, 100);
    CHK(bm != NULL);
    HGDIOBJ old_bm = SelectObject(dc, bm);
    CHK(old_bm != NULL);

    /* MapMode default & changes */
    CHK(GetMapMode(dc) == MM_TEXT);
    CHK(SetMapMode(dc, MM_LOMETRIC) == MM_TEXT);
    CHK(GetMapMode(dc) == MM_LOMETRIC);
    CHK(SetMapMode(dc, MM_TEXT) == MM_LOMETRIC);

    /* Viewport / Window origin */
    POINT pt;
    CHK(GetViewportOrgEx(dc, &pt) && pt.x == 0 && pt.y == 0);
    CHK(SetViewportOrgEx(dc, 10, 20, &pt) && pt.x == 0 && pt.y == 0);
    CHK(GetViewportOrgEx(dc, &pt) && pt.x == 10 && pt.y == 20);
    CHK(OffsetViewportOrgEx(dc, 5, -5, &pt) && pt.x == 10 && pt.y == 20);
    CHK(GetViewportOrgEx(dc, &pt) && pt.x == 15 && pt.y == 15);

    CHK(GetWindowOrgEx(dc, &pt) && pt.x == 0 && pt.y == 0);
    CHK(SetWindowOrgEx(dc, 30, 40, &pt) && pt.x == 0 && pt.y == 0);
    CHK(GetWindowOrgEx(dc, &pt) && pt.x == 30 && pt.y == 40);

    /* ClipBox */
    RECT rc;
    CHK(GetClipBox(dc, &rc) != ERROR);
    CHK(IntersectClipRect(dc, 5, 5, 50, 50) != ERROR);
    CHK(GetClipBox(dc, &rc) != ERROR);
    CHK(rc.left == 5 && rc.top == 5 && rc.right == 50 && rc.bottom == 50);

    /* SaveDC */
    int s1 = SaveDC(dc);
    CHK(s1 > 0);

    /* Modify state in level 1 */
    SetViewportOrgEx(dc, 100, 200, NULL);
    SetTextColor(dc, RGB(1, 2, 3));
    int s2 = SaveDC(dc);
    CHK(s2 > s1);

    /* Modify state in level 2 */
    SetViewportOrgEx(dc, 500, 600, NULL);
    SetTextColor(dc, RGB(9, 9, 9));

    /* Restore relative (-1): should restore level 1 */
    CHK(RestoreDC(dc, -1));
    CHK(GetViewportOrgEx(dc, &pt) && pt.x == 100 && pt.y == 200);
    CHK(GetTextColor(dc) == RGB(1, 2, 3));

    /* Restore absolute (s1): should restore baseline */
    CHK(RestoreDC(dc, s1));
    CHK(GetViewportOrgEx(dc, &pt) && pt.x == 15 && pt.y == 15);

    /* Regions */
    HRGN rgn = CreateRectRgn(1, 2, 3, 4);
    CHK(rgn != NULL);
    CHK(SetRectRgn(rgn, 10, 20, 30, 40));
    RECT rc_rgn = { 5, 6, 7, 8 };
    HRGN rgn2 = CreateRectRgnIndirect(&rc_rgn);
    CHK(rgn2 != NULL);
    CHK(SelectClipRgn(dc, rgn) != ERROR);
    CHK(SelectClipRgn(dc, NULL) != ERROR);
    DeleteObject(rgn);
    DeleteObject(rgn2);

    SelectObject(dc, old_bm);
    DeleteObject(bm);
    DeleteDC(dc);

    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "dc saved ok\n", 12, &written, NULL);
    ExitProcess(0);
}
