#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdarg.h>
static int equalw(const WCHAR *a, const WCHAR *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}
static int equala(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}
static int via_w(WCHAR *out, const WCHAR *format, ...)
{
	va_list args;
	va_start(args, format);
	int n = wvsprintfW(out, format, args);
	va_end(args);
	return n;
}
static int via_a(char *out, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int n = wvsprintfA(out, format, args);
	va_end(args);
	return n;
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written, edition = 0;
	WCHAR w[512];
	char a[1024];
	ok &= GetProductInfo(10, 0, 0, 0, &edition) &&
	      edition == PRODUCT_PROFESSIONAL;
	ok &= GetProductInfo(6, 0, 0, 0, &edition) &&
	      edition == PRODUCT_BUSINESS;
	ok &= !GetProductInfo(5, 0, 0, 0, &edition) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !GetProductInfo(10, 0, 0, 0, 0);
	ok &= wsprintfW(w, L"%d/%08X/%I64d/%s/%S", -7, 42,
			(-9223372036854775807LL - 1), L"wide", "ansi") > 0 &&
	      equalw(w, L"-7/0000002A/-9223372036854775808/wide/ansi");
	ok &= wsprintfA(a, "%#08x %.3s %-4d %%", 42, "abcdef", 7) > 0 &&
	      equala(a, "0x00002a abc 7    %");
	ok &= via_w(w, L"%d,%d,%d,%d,%d,%d,%d,%d", 1, 2, 3, 4, 5, 6, 7, 8) ==
		      15 &&
	      equalw(w, L"1,2,3,4,5,6,7,8");
	ok &= via_a(a, "%ls:%hd:%u", L"wide", -2, 0xffffffffu) > 0 &&
	      equala(a, "wide:-2:4294967295");
	ok &= wsprintfW(w, L"%S", "\xc3\xa9") == 1 && w[0] == 0xe9 && !w[1];
	ok &= wsprintfA(a, "%S", L"\x00e9") == 2 &&
	      (unsigned char)a[0] == 0xc3 && (unsigned char)a[1] == 0xa9;
	ok &= wsprintfW(w, L"%%/%c/%C", 'a', 'b') == 5 && equalw(w, L"%/a/b");
	ok &= wsprintfA(a, "%s", (char *)0) == 6 && equala(a, "(null)");
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "format ok\n" : "format FAIL\n", ok ? 10 : 12, &written,
		  0);
	ExitProcess(ok ? 0 : 1);
}
