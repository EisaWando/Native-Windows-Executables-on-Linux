#define UNICODE
#include <windows.h>
static WCHAR long_text[512];
static void failed(unsigned line)
{
	unsigned error = GetLastError();
	char text[64] = "font check ";
	unsigned pos = 11;
	char digits[12];
	unsigned n = 0;
	do {
		digits[n++] = '0' + line % 10;
		line /= 10;
	} while (line);
	while (n)
		text[pos++] = digits[--n];
	text[pos++] = ' ';
	do {
		digits[n++] = '0' + error % 10;
		error /= 10;
	} while (error);
	while (n)
		text[pos++] = digits[--n];
	text[pos++] = '\n';
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, pos, &written, 0);
}
#define CHECK(expr)                       \
	do {                              \
		if (!(expr)) {            \
			ok = 0;           \
			failed(__LINE__); \
		}                         \
	} while (0)

void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	HDC dc = CreateCompatibleDC(0);
	HFONT small = CreateFontW(-12, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET,
				  0, 0, 0, 0, L"sans");
	HFONT big = CreateFontW(-24, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0,
				0, 0, 0, L"sans");
	HGDIOBJ old = SelectObject(dc, small);
	TEXTMETRICW m, n;
	TEXTMETRICA a;
	SIZE wi, ww, ascii, utf, one, long_size;
	CHECK(dc && small && big && old && GetTextMetricsW(dc, &m));
	CHECK(m.tmHeight > 0 && m.tmHeight == m.tmAscent + m.tmDescent &&
	      m.tmAveCharWidth > 0);
	CHECK(GetTextMetricsA(dc, &a) && a.tmHeight == m.tmHeight);
	CHECK(GetTextExtentPoint32W(dc, L"ii", 2, &wi) &&
	      GetTextExtentPoint32W(dc, L"WW", 2, &ww) && ww.cx > wi.cx);
	CHECK(GetTextExtentPoint32A(dc, "WW", 2, &ascii) && ascii.cx == ww.cx &&
	      ascii.cy == ww.cy);
	CHECK(GetTextExtentPoint32A(dc, "\xc3\xa9", 2, &utf) &&
	      GetTextExtentPoint32W(dc, L"\x00e9", 1, &ascii) &&
	      utf.cx == ascii.cx);
	CHECK(GetTextExtentPoint32W(dc, L"I", 1, &one));
	for (unsigned i = 0; i < 512; i++)
		long_text[i] = 'I';
	CHECK(GetTextExtentPoint32W(dc, long_text, 512, &long_size) &&
	      long_size.cx == 512 * one.cx);
	CHECK(SelectObject(dc, big) == small && GetTextMetricsW(dc, &n) &&
	      n.tmHeight > m.tmHeight);
	CHECK(GetTextExtentPoint32W(dc, L"WW", 2, &ascii) && ascii.cx > ww.cx);
	CHECK(!GetTextMetricsW((HDC)4, &m) &&
	      GetLastError() == ERROR_INVALID_HANDLE);
	CHECK(!GetTextExtentPoint32W(dc, L"x", -1, &ascii) &&
	      GetLastError() == ERROR_INVALID_PARAMETER);
	CHECK(SelectObject(dc, old) == big && DeleteObject(small) &&
	      DeleteObject(big) && DeleteDC(dc));
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "font metrics ok\n" : "font metrics FAIL\n",
		  ok ? 16 : 18, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
