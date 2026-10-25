#define UNICODE
#define _WIN32_WINNT 0x0600
#include <windows.h>
static int equal(const WCHAR *a, const WCHAR *b)
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
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	SYSTEMTIME t = { 2024, 2, 0, 29, 23, 4, 5, 0 };
	WCHAR text[128];
	char ansi[128];
	int n = GetDateFormatW(LOCALE_USER_DEFAULT, 0, &t, 0, 0, 0);
	ok &= n == 10 &&
	      GetDateFormatW(LOCALE_USER_DEFAULT, 0, &t, 0, text, 128) == n &&
	      equal(text, L"2/29/2024");
	text[0] = '!';
	ok &= !GetDateFormatW(LOCALE_USER_DEFAULT, 0, &t, 0, text, 9) &&
	      GetLastError() == ERROR_INSUFFICIENT_BUFFER && text[0] == '!';
	ok &= GetDateFormatW(0x409, 0, &t, L"'Date:' yyyy-MM-dd", text, 128) >
		      0 &&
	      equal(text, L"Date: 2024-02-29");
	ok &= GetDateFormatEx(L"en-US", DATE_LONGDATE, &t, 0, text, 128, 0) > 0 &&
	      equal(text, L"Thursday, February 29, 2024");
	ok &= GetDateFormatA(0x409, 0, &t, "yyyy-MM-dd", ansi, 128) == 11 &&
	      equala(ansi, "2024-02-29");
	ok &= GetTimeFormatW(0x409, 0, &t, 0, text, 128) > 0 &&
	      equal(text, L"11:04:05 PM");
	ok &= GetTimeFormatW(0x409, TIME_FORCE24HOURFORMAT | TIME_NOTIMEMARKER,
			     &t, 0, text, 128) == 9 &&
	      equal(text, L"23:04:05");
	ok &= GetTimeFormatW(0x409, TIME_NOSECONDS, &t, 0, text, 128) > 0 &&
	      equal(text, L"11:04 PM");
	ok &= GetTimeFormatW(0x409, TIME_NOMINUTESORSECONDS, &t, 0, text, 128) >
		      0 &&
	      equal(text, L"11 PM");
	ok &= GetTimeFormatEx(L"en-US", 0, &t, L"HH 'o''clock'", text, 128) >
		      0 &&
	      equal(text, L"23 o'clock");
	ok &= GetTimeFormatA(0x409, 0, &t, "HH:mm:ss", ansi, 128) == 9 &&
	      equala(ansi, "23:04:05");
	t.wYear = 2023;
	ok &= !GetDateFormatW(0x409, 0, &t, 0, text, 128) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	t.wYear = 0;
	t.wMonth = 0;
	t.wDay = 0;
	ok &= GetTimeFormatW(0x409, 0, &t, L"HH:mm", text, 128) == 6 &&
	      equal(text, L"23:04");
	t.wHour = 24;
	ok &= !GetTimeFormatW(0x409, 0, &t, 0, text, 128) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !GetDateFormatEx(L"fr-FR", 0, 0, 0, text, 128, 0) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= GetDateFormatW(LOCALE_USER_DEFAULT, 0, 0, 0, 0, 0) > 0;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "date format ok\n" : "date format FAIL\n", ok ? 15 : 17,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
