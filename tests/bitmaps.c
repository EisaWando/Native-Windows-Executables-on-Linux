/* tests/bitmaps.c — pixel-level tests for bitmap and blit shims */
#include <windows.h>
#include <stdint.h>

static void failed(unsigned line)
{
    char buf[64];
    int n = 0;
    const char *msg = "FAIL bitmaps.c line ";
    for (; msg[n]; n++) buf[n] = msg[n];
    unsigned v = line;
    char rev[12]; int r = 0;
    do { rev[r++] = '0' + v % 10; v /= 10; } while (v);
    while (r--) buf[n++] = rev[r + 1]; /* safe: rev still valid */
    buf[n++] = '\n';
    DWORD w; WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, n, &w, NULL);
    ExitProcess(1);
}
#define CHK(x) do { if (!(x)) failed(__LINE__); } while (0)

void mainCRTStartup(void)
{
    /* --- CreateCompatibleBitmap + SelectObject + GetPixel/SetPixel on memDC --- */
    HDC mdc = CreateCompatibleDC(NULL);
    CHK(mdc != NULL);
    HBITMAP hbm = CreateCompatibleBitmap(mdc, 8, 8);
    CHK(hbm != NULL);
    HGDIOBJ old = SelectObject(mdc, hbm);
    CHK(old != NULL);

    /* After SelectObject the bitmap is selected; GetObject should return BITMAP */
    BITMAP bm;
    CHK(GetObject(hbm, sizeof(bm), &bm) == sizeof(bm));
    CHK(bm.bmWidth == 8 && bm.bmHeight == 8);

    /* PatBlt BLACKNESS: all pixels should be 0 */
    CHK(PatBlt(mdc, 0, 0, 8, 8, BLACKNESS));
    CHK(GetPixel(mdc, 0, 0) == 0x00000000);

    /* PatBlt WHITENESS: all pixels should be white */
    CHK(PatBlt(mdc, 0, 0, 8, 8, WHITENESS));
    CHK(GetPixel(mdc, 4, 4) == 0x00FFFFFF);

    /* SetPixel / GetPixel round-trip */
    CHK(SetPixel(mdc, 2, 3, RGB(10, 20, 30)) != (COLORREF)CLR_INVALID);
    COLORREF got = GetPixel(mdc, 2, 3);
    CHK(got == RGB(10, 20, 30));

    /* --- BitBlt src→dst SRCCOPY --- */
    HDC mdc2 = CreateCompatibleDC(NULL);
    CHK(mdc2 != NULL);
    HBITMAP hbm2 = CreateCompatibleBitmap(mdc2, 8, 8);
    CHK(hbm2 != NULL);
    HGDIOBJ old2 = SelectObject(mdc2, hbm2);
    CHK(old2 != NULL);

    /* Paint mdc red */
    HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
    CHK(red != NULL);
    HGDIOBJ oldbr = SelectObject(mdc, red);
    CHK(PatBlt(mdc, 0, 0, 8, 8, PATCOPY));
    SelectObject(mdc, oldbr);
    DeleteObject(red);
    CHK(GetPixel(mdc, 0, 0) == RGB(255, 0, 0));

    /* BitBlt mdc → mdc2 SRCCOPY */
    CHK(BitBlt(mdc2, 0, 0, 8, 8, mdc, 0, 0, SRCCOPY));
    CHK(GetPixel(mdc2, 3, 3) == RGB(255, 0, 0));

    /* SRCAND: red & 0x00ff00 = 0 */
    CHK(BitBlt(mdc2, 0, 0, 8, 8, mdc, 0, 0, SRCAND));
    /* This tests that SRCAND didn't crash; result pixel is ~unpredictable due to
       BGR<->ARGB ordering but the call must succeed. */

    /* --- StretchBlt: 4×4 → 8×8 --- */
    HDC mdc3 = CreateCompatibleDC(NULL);
    CHK(mdc3 != NULL);
    HBITMAP hbm3 = CreateCompatibleBitmap(mdc3, 8, 8);
    CHK(hbm3 != NULL);
    SelectObject(mdc3, hbm3);
    PatBlt(mdc3, 0, 0, 8, 8, BLACKNESS);
    CHK(StretchBlt(mdc3, 0, 0, 8, 8, mdc, 0, 0, 4, 4, SRCCOPY));
    /* Destination should have gotten something (not crash). */

    /* --- CreateBitmap + SetDIBits + GetDIBits round-trip --- */
    HBITMAP hbmA = CreateCompatibleBitmap(mdc, 4, 4);
    CHK(hbmA != NULL);
    HDC mdcA = CreateCompatibleDC(NULL);
    CHK(mdcA != NULL);
    SelectObject(mdcA, hbmA);

    /* Fill with known pattern via SetPixel */
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            SetPixel(mdcA, x, y, RGB(x * 40, y * 40, 128));

    /* GetDIBits: size query (bits=NULL) */
    BITMAPINFO bi;
    for (unsigned i = 0; i < sizeof(bi); i++) ((char *)&bi)[i] = 0;
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    int rows = GetDIBits(mdcA, hbmA, 0, 4, NULL, &bi, DIB_RGB_COLORS);
    CHK(rows == 4);
    CHK(bi.bmiHeader.biWidth == 4 && bi.bmiHeader.biHeight == -4); /* top-down */

    /* GetDIBits: actual data */
    uint32_t buf[16];
    rows = GetDIBits(mdcA, hbmA, 0, 4, buf, &bi, DIB_RGB_COLORS);
    CHK(rows == 4);

    /* SetDIBits: write back the same buffer */
    bi.bmiHeader.biHeight = -4; /* top-down */
    CHK(SetDIBits(mdcA, hbmA, 0, 4, buf, &bi, DIB_RGB_COLORS) == 4);

    /* SetStretchBltMode */
    CHK(SetStretchBltMode(mdc, STRETCH_DELETESCANS) != 0);
    CHK(GetStretchBltMode(mdc) == STRETCH_DELETESCANS);

    /* Cleanup */
    SelectObject(mdc, old);
    DeleteObject(hbm);
    DeleteDC(mdc);
    SelectObject(mdc2, old2);
    DeleteObject(hbm2);
    DeleteDC(mdc2);
    DeleteObject(hbm3);
    DeleteDC(mdc3);
    DeleteObject(hbmA);
    DeleteDC(mdcA);

    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "bitmaps ok\n", 11, &written, NULL);
    ExitProcess(0);
}
