#define UNICODE
#include <windows.h>
#include <uxtheme.h>
static unsigned changes;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b)
{
	if (m == WM_THEMECHANGED)
		changes++;
	return DefWindowProcW(w, m, a, b);
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	WNDCLASSEXW c = { 0 };
	c.cbSize = sizeof(c);
	c.lpfnWndProc = proc;
	c.hInstance = GetModuleHandleW(0);
	c.lpszClassName = L"theme-test";
	ok &= RegisterClassExW(&c) != 0;
	HWND w = CreateWindowExW(0, c.lpszClassName, L"hidden", 0, 0, 0, 0, 0,
				 0, 0, c.hInstance, 0);
	WCHAR app[] = { 'E', 'x', 'p', 'l', 'o', 'r', 'e', 'r', 0 };
	ok &= w && SetWindowTheme(w, app, L"") == S_OK && changes == 1;
	app[0] = 'X';
	ok &= !IsThemeActive() && !IsAppThemed() &&
	      !OpenThemeData(w, L"Button") && !GetWindowTheme(w);
	ok &= FAILED(CloseThemeData(0));
	ok &= SetWindowTheme(w, 0, 0) == S_OK && changes == 2;
	ok &= DestroyWindow(w) && FAILED(SetWindowTheme(w, 0, 0));
	ok &= UnregisterClassW(c.lpszClassName, c.hInstance);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "theme ok\n" : "theme FAIL\n", ok ? 9 : 11, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
