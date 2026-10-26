/* tests/text_extents.c — unit test for GetTextExtentExPointW/A */
#define UNICODE
#include <windows.h>

static void failed(unsigned line)
{
    char buf[64];
    int n = 0;
    const char *msg = "FAIL text_extents.c line ";
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
    HDC dc = CreateCompatibleDC(0);
    CHK(dc != NULL);
    HFONT font = CreateFontW(-16, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"sans");
    CHK(font != NULL);
    HGDIOBJ old = SelectObject(dc, font);
    CHK(old != NULL);

    /* 1. Compare with GetTextExtentPoint32W */
    SIZE sz32;
    CHK(GetTextExtentPoint32W(dc, L"Hello World", 11, &sz32));
    CHK(sz32.cx > 0 && sz32.cy > 0);

    SIZE szEx;
    int dx[11];
    int fit = -1;
    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, 100000, &fit, dx, &szEx));
    CHK(szEx.cx == sz32.cx);
    CHK(szEx.cy == sz32.cy);
    CHK(fit == 11);
    CHK(dx[10] == sz32.cx);

    /* Check monotonically increasing character widths */
    for (int i = 0; i < 10; i++)
        CHK(dx[i] <= dx[i + 1]);

    /* First char matches GetTextExtentPoint32W for 1 char */
    SIZE sz1;
    CHK(GetTextExtentPoint32W(dc, L"H", 1, &sz1));
    CHK(dx[0] == sz1.cx);

    /* 2. nMaxExtent and lpnFit logic */
    int fit_limit = -1;
    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, dx[4], &fit_limit, NULL, &szEx));
    CHK(fit_limit == 5);

    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, dx[0] - 1, &fit_limit, NULL, &szEx));
    CHK(fit_limit == 0);

    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, -10, &fit_limit, NULL, &szEx));
    CHK(fit_limit == 0);

    /* 3. NULL lpnFit: nMaxExtent ignored */
    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, 5, NULL, dx, &szEx));
    CHK(szEx.cx == sz32.cx);
    CHK(dx[10] == sz32.cx);

    /* 4. Both lpnFit and alpDx NULL */
    SIZE szNull;
    CHK(GetTextExtentExPointW(dc, L"Hello World", 11, 0, NULL, NULL, &szNull));
    CHK(szNull.cx == sz32.cx && szNull.cy == sz32.cy);

    /* 5. Empty string */
    SIZE sz0;
    int fit0 = -1;
    CHK(GetTextExtentExPointW(dc, L"", 0, 100, &fit0, NULL, &sz0));
    CHK(sz0.cx == 0);
    CHK(fit0 == 0);
    CHK(GetTextExtentExPointW(dc, NULL, 0, 100, &fit0, NULL, &sz0));
    CHK(sz0.cx == 0);
    CHK(fit0 == 0);

    /* 6. Error handling */
    CHK(!GetTextExtentExPointW(dc, L"test", -1, 100, &fit, dx, &szEx));
    CHK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHK(!GetTextExtentExPointW(dc, NULL, 5, 100, &fit, dx, &szEx));
    CHK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHK(!GetTextExtentExPointW(dc, L"test", 4, 100, &fit, dx, NULL));
    CHK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHK(!GetTextExtentExPointW((HDC)0xdead, L"test", 4, 100, &fit, dx, &szEx));
    CHK(GetLastError() == ERROR_INVALID_HANDLE);

    /* 7. ANSI variant */
    SIZE szA;
    int dxA[11];
    int fitA = -1;
    CHK(GetTextExtentExPointA(dc, "Hello World", 11, 100000, &fitA, dxA, &szA));
    CHK(szA.cx == sz32.cx);
    CHK(szA.cy == sz32.cy);
    CHK(fitA == 11);
    for (int i = 0; i < 11; i++)
        CHK(dxA[i] == dx[i]);

    CHK(GetTextExtentExPointA(dc, "", 0, 100, &fitA, NULL, &szA));
    CHK(szA.cx == 0);
    CHK(fitA == 0);

    /* Cleanup */
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);

    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "text extents ok\n", 16, &written, NULL);
    ExitProcess(0);
}
