#include <windows.h>
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	HDC a = CreateCompatibleDC(0), b = CreateCompatibleDC(0);
	ok &= a && b && GetTextAlign(a) == 0 && GetBkMode(a) == OPAQUE;
	ok &= GetTextColor(a) == RGB(0, 0, 0) &&
	      GetBkColor(a) == RGB(255, 255, 255);
	ok &= SetTextAlign(a, TA_BASELINE) == 0 &&
	      GetTextAlign(a) == TA_BASELINE;
	ok &= SetTextAlign(a, TA_CENTER | TA_BOTTOM) == TA_BASELINE;
	ok &= GetTextAlign(b) == 0;
	ok &= SetTextAlign(a, 0x80000000u) == GDI_ERROR &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= SetTextColor(a, RGB(4, 5, 6)) == 0 &&
	      GetTextColor(a) == RGB(4, 5, 6);
	ok &= SetBkColor(a, 7) == RGB(255, 255, 255) && GetBkColor(a) == 7;
	ok &= SetBkMode(a, TRANSPARENT) == OPAQUE &&
	      GetBkMode(a) == TRANSPARENT;
	ok &= SetTextCharacterExtra(a, 3) == 0 && GetTextCharacterExtra(a) == 3;
	ok &= SetTextCharacterExtra(a, -1) == 3 &&
	      GetTextCharacterExtra(b) == 0;
	ok &= DeleteDC(a) && GetTextAlign(a) == GDI_ERROR &&
	      GetLastError() == ERROR_INVALID_HANDLE;
	ok &= DeleteDC(b);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "dc state ok\n" : "dc state FAIL\n", ok ? 12 : 14,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
