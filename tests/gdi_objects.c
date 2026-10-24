/* Headless object contracts only: no windows, drawing or display required. */
#define UNICODE
#include <windows.h>
/* GCC lowers large aggregate initializers to memset even without the CRT. */
void *memset(void *dest, int value, size_t bytes)
{
    unsigned char *p = dest;
    for (size_t i = 0; i < bytes; i++) p[i] = value;
    return dest;
}
static int failures;
#define CHECK(c) do { if (!(c)) failures++; } while (0)
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM p, LPARAM l)
{ (void)w; (void)m; (void)p; (void)l; return 0; }
static HBRUSH handles[4096];
void mainCRTStartup(void)
{
    DWORD written;
    HBRUSH brush = CreateSolidBrush(RGB(0x20, 0x40, 0x60));
    CHECK(brush && !((ULONG_PTR)brush & 3) && (ULONG_PTR)brush >= 0x50000000 && (ULONG_PTR)brush < 0x60000000);
    LOGBRUSH lb;
    CHECK(GetObjectW(brush, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID && lb.lbColor == RGB(0x20,0x40,0x60));
    CHECK(GetObjectW(brush, 0, 0) == sizeof(lb));
    CHECK(GetObjectType(brush) == OBJ_BRUSH);
    CHECK(!DeleteObject((HGDIOBJ)0x44) && GetLastError() == 6);
    CHECK(!DeleteObject((HGDIOBJ)((ULONG_PTR)brush+1)) && GetLastError() == 6);
    HBRUSH stale = CreateSolidBrush(0); CHECK(DeleteObject(stale));
    HBRUSH fresh = CreateSolidBrush(1); CHECK(fresh && fresh != stale);
    CHECK(!GetObjectType(stale) && GetLastError() == 6); CHECK(DeleteObject(fresh));
    HPEN pen = CreatePen(PS_DASH, 2, RGB(1,2,3)); LOGPEN lp;
    CHECK(GetObjectW(pen, sizeof(lp), &lp) == sizeof(lp) && lp.lopnWidth.x == 2 && lp.lopnColor == RGB(1,2,3));
    LOGFONTW lf = {0}, got;
    lf.lfHeight = -16; lf.lfWeight = FW_BOLD; lf.lfFaceName[0] = 'T';
    HFONT font = CreateFontIndirectW(&lf);
    CHECK(GetObjectW(font, sizeof(got), &got) == sizeof(got) && got.lfHeight == -16 && got.lfFaceName[0] == 'T');
    HFONT direct = CreateFontW(-18, 2, 3, 4, 500, 1, 0, 0, 1, 0, 0, 0, 0, L"Test");
    CHECK(GetObjectW(direct, sizeof(got), &got) == sizeof(got) && got.lfHeight == -18 && got.lfWeight == 500 && got.lfItalic == 1);
    WORD palette_count = 0;
    HGDIOBJ palette = GetStockObject(DEFAULT_PALETTE);
    CHECK(GetObjectType(palette) == OBJ_PAL && GetObjectW(palette, sizeof(palette_count), &palette_count) == 2 && palette_count == 20);
    HGDIOBJ stock = GetStockObject(WHITE_BRUSH); CHECK(stock == GetStockObject(WHITE_BRUSH));
    CHECK(DeleteObject(stock) && GetObjectType(stock) == OBJ_BRUSH);
    HDC dc = CreateCompatibleDC(0); CHECK(dc && GetObjectType(dc) == OBJ_MEMDC);
    HGDIOBJ old = SelectObject(dc, brush); CHECK(old && GetCurrentObject(dc, OBJ_BRUSH) == brush);
    CHECK(!DeleteObject(brush)); CHECK(SelectObject(dc, old) == brush);
    CHECK(!SelectObject((HDC)brush, pen) && GetLastError() == 6);
    CHECK(!DeleteObject(dc) && GetLastError() == 6);
    CHECK(GetDeviceCaps(dc, LOGPIXELSX) == 96 && GetDeviceCaps(dc, LOGPIXELSY) == 96);
    CHECK(GetDeviceCaps(dc, HORZRES) == 1920 && GetDeviceCaps(dc, VERTRES) == 1080 && GetDeviceCaps(dc, BITSPIXEL) == 32);
    CHECK(GetSystemMetrics(SM_CXSCREEN) == 1920 && GetSystemMetrics(SM_CYSCREEN) == 1080);
    CHECK(!GetDeviceCaps((HDC)1, LOGPIXELSX) && GetLastError() == 6);
    CHECK(SelectObject(dc, font)); CHECK(DeleteDC(dc));
    CHECK(!DeleteDC(dc) && GetLastError() == 6);
    CHECK(DeleteObject(font) && DeleteObject(direct) && DeleteObject(pen) && DeleteObject(brush));
    HDC screen = GetDC(0); CHECK(screen && GetObjectType(screen) == OBJ_DC);
    CHECK(!DeleteDC(screen) && GetLastError() == 6);
    CHECK(!GetDC((HWND)1) && GetLastError() == 6);
    CHECK(ReleaseDC(0, screen) == 1 && !ReleaseDC(0, screen) && GetLastError() == 6);
    NONCLIENTMETRICSW ncm = {0}; ncm.cbSize = sizeof(ncm);
    CHECK(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) && ncm.lfMessageFont.lfHeight == -12);
    RECT work; CHECK(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) && work.right == 1920 && work.bottom == 1040);
    HIGHCONTRASTW hc = {sizeof(hc), 0, 0};
    CHECK(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && !(hc.dwFlags & HCF_HIGHCONTRASTON));
    CHECK(!SystemParametersInfoW(0xffff, 0, &work, 0) && GetLastError() == ERROR_NOT_SUPPORTED);
    CHECK(GetSysColor(COLOR_WINDOW) == RGB(255,255,255));
    HCURSOR cursor = LoadCursorW(0, IDC_ARROW); HICON icon = LoadIconW(0, IDI_APPLICATION);
    CHECK(cursor && icon && cursor != (HCURSOR)icon && cursor == LoadCursorW(0, IDC_ARROW));
    CHECK(!DeleteObject(cursor) && GetLastError() == 6);
    HICON private_icon = LoadImageW(0, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    CHECK(private_icon && DestroyIcon(private_icon)); CHECK(!DestroyIcon(private_icon) && GetLastError() == 6);
    HBITMAP bitmap = LoadImageW(0, MAKEINTRESOURCEW(1), IMAGE_BITMAP, 10, 20, 0); BITMAP bm;
    CHECK(bitmap && GetObjectW(bitmap, sizeof(bm), &bm) == sizeof(bm) && bm.bmWidth == 10 && bm.bmHeight == 20);
    dc = CreateCompatibleDC(0); old = SelectObject(dc, bitmap); CHECK(old && GetObjectType(old) == OBJ_BITMAP);
    CHECK(!DeleteObject(bitmap)); CHECK(DeleteDC(dc)); CHECK(DeleteObject(bitmap));
    WNDCLASSEXW cls = {0}, saved = {0}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = proc;
    cls.style = CS_DBLCLKS; cls.cbClsExtra = 16; cls.cbWndExtra = 8;
    cls.hCursor = cursor; cls.hIcon = icon; cls.lpszClassName = L"HeadlessTest";
    ATOM atom = RegisterClassExW(&cls); CHECK(atom);
    CHECK(!RegisterClassExW(&cls) && GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    saved.cbSize = sizeof(saved); CHECK(GetClassInfoExW(0, MAKEINTATOM(atom), &saved));
    CHECK(saved.lpfnWndProc == proc && saved.style == CS_DBLCLKS && saved.cbClsExtra == 16 && saved.cbWndExtra == 8);
    saved.cbSize = sizeof(saved);
    CHECK(GetClassInfoExW(0, L"headlesstest", &saved));
    CHECK(UnregisterClassW(cls.lpszClassName, 0));
    CHECK(!GetClassInfoExW(0, cls.lpszClassName, &saved) && GetLastError() == ERROR_CLASS_DOES_NOT_EXIST);
    /* Hold a DC and a selected object across several table doublings. */
    dc = CreateCompatibleDC(0); brush = CreateSolidBrush(0x123456);
    old = SelectObject(dc, brush); CHECK(old);
    int count = 0;
    while (count < 4096 && (handles[count] = CreateSolidBrush(count))) count++;
    CHECK(count == 4096);
    CHECK(GetCurrentObject(dc, OBJ_BRUSH) == brush);
    CHECK(GetObjectW(brush, sizeof(lb), &lb) == sizeof(lb) && lb.lbColor == 0x123456);
    for (int i = 0; i < count; i++) {
        CHECK(GetObjectW(handles[i], sizeof(lb), &lb) == sizeof(lb) && lb.lbColor == (DWORD)i);
        CHECK(DeleteObject(handles[i]));
    }
    CHECK(SelectObject(dc, old) == brush && DeleteDC(dc) && DeleteObject(brush));
    brush = CreateSolidBrush(0); CHECK(brush && DeleteObject(brush));
    /* Repeated DC teardown must release its private default bitmap. */
    for (int i = 0; i < 1100; i++) { dc = CreateCompatibleDC(0); CHECK(dc && DeleteDC(dc)); }
    const char *msg = failures ? "gdi FAIL\n" : "gdi ok\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, failures ? 9 : 7, &written, 0);
    ExitProcess(failures ? 1 : 0);
}
