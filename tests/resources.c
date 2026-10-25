#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0a00
#include <windows.h>
static unsigned names, command;
static BOOL CALLBACK enumerate(HMODULE h, LPCWSTR t, LPWSTR n, LONG_PTR p)
{
	(void)h;
	(void)t;
	(void)p;
	if (IS_INTRESOURCE(n) && (ULONG_PTR)n == 101)
		names++;
	return TRUE;
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_COMMAND) {
		command = LOWORD(w);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}
void mainCRTStartup(void)
{
	int ok = 1;
	HMODULE module = GetModuleHandleW(0);
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_MENU);
	ok &= resource != 0 && SizeofResource(module, resource) > 10;
	ok &= LockResource(LoadResource(module, resource)) != 0;
	ok &= FindResourceA(module, MAKEINTRESOURCEA(101),
			    MAKEINTRESOURCEA(4)) == resource;
	ok &= FindResourceExW(module, RT_MENU, MAKEINTRESOURCEW(101), 0x409) ==
	      resource;
	ok &= EnumResourceNamesW(module, RT_MENU, enumerate, 0) && names == 1;
	WCHAR text[64];
	const WCHAR *direct = 0;
	char ansi[64];
	ok &= LoadStringW(module, 17, text, 64) == 13 && text[0] == 'r' &&
	      text[13] == 0;
	ok &= LoadStringW(module, 17, (LPWSTR)&direct, 0) == 13 &&
	      direct[0] == 'r';
	ok &= LoadStringA(module, 17, ansi, 64) == 13 && ansi[13] == 0;
	ok &= LoadStringW(module, 17, text, 4) == 3 && text[3] == 0;
	HMENU menu = LoadMenuW(module, MAKEINTRESOURCEW(101));
	HMENU file = GetSubMenu(menu, 0);
	ok &= menu && file && GetMenuItemCount(menu) == 2 &&
	      GetMenuItemCount(file) == 3;
	ok &= GetMenuItemID(file, 0) == 40001;
	MENUITEMINFOW info = { 0 };
	info.cbSize = sizeof(info);
	info.fMask = MIIM_STRING | MIIM_ID;
	info.dwTypeData = text;
	info.cch = 64;
	ok &= GetMenuItemInfoW(file, 0, TRUE, &info) && info.wID == 40001 &&
	      text[0] == '&';
	ok &= CheckMenuItem(menu, 40001, MF_BYCOMMAND | MF_CHECKED) !=
	      (DWORD)-1;
	ok &= (GetMenuState(menu, 40001, MF_BYCOMMAND) & MF_CHECKED) != 0;
	ok &= EnableMenuItem(menu, 40001, MF_BYCOMMAND | MF_GRAYED) !=
	      (DWORD)-1;
	ok &= (GetMenuState(menu, 40001, MF_BYCOMMAND) & MF_GRAYED) != 0;
	ok &= RemoveMenu(file, 1, MF_BYPOSITION) && GetMenuItemCount(file) == 2;
	ok &= AppendMenuW(file, MF_STRING, 40004, L"Extra") &&
	      GetMenuItemCount(file) == 3;
	WNDCLASSEXW cls = { 0 };
	cls.cbSize = sizeof(cls);
	cls.lpfnWndProc = proc;
	cls.hInstance = module;
	cls.lpszClassName = L"resource-test";
	ok &= RegisterClassExW(&cls) != 0;
	HWND window = CreateWindowExW(0, cls.lpszClassName, L"menu hidden", 0,
				      0, 0, 320, 200, 0, menu, module, 0);
	RECT rect;
	ok &= window && GetMenu(window) == menu &&
	      GetClientRect(window, &rect) && rect.bottom == 176;
	RECT adjust = { 0, 0, 320, 176 };
	ok &= AdjustWindowRect(&adjust, 0, TRUE) &&
	      adjust.bottom - adjust.top == 200;
	HACCEL accel = LoadAcceleratorsW(module, MAKEINTRESOURCEW(102));
	ok &= accel != 0;
	/* Explicit table also validates resource-independent accelerator lifetime. */
	ACCEL entry = { FVIRTKEY, 'X', 40003 };
	HACCEL plain = CreateAcceleratorTableW(&entry, 1);
	MSG msg = { 0 };
	msg.hwnd = window;
	msg.message = WM_KEYDOWN;
	msg.wParam = 'X';
	ok &= plain && TranslateAcceleratorW(window, plain, &msg) &&
	      command == 40003;
	ok &= DestroyAcceleratorTable(plain) && DestroyAcceleratorTable(accel);
	ok &= DestroyWindow(window) && !IsWindow(window);
	ok &= UnregisterClassW(cls.lpszClassName, module);
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "resources ok\n" : "resources FAIL\n", ok ? 13 : 15,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
