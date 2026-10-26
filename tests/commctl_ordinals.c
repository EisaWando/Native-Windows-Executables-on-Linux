#define UNICODE
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
__declspec(dllimport) void WINAPI InitControlsOrdinal(void);
__declspec(dllimport) HRESULT WINAPI LoadScaledOrdinal(HINSTANCE, PCWSTR, int,
						       int, HICON *);
__declspec(dllimport) BOOL WINAPI SetSubclassOrdinal(HWND, SUBCLASSPROC,
						     UINT_PTR, DWORD_PTR);
__declspec(dllimport) BOOL WINAPI GetSubclassOrdinal(HWND, SUBCLASSPROC,
						     UINT_PTR, DWORD_PTR *);
__declspec(dllimport) BOOL WINAPI RemoveSubclassOrdinal(HWND, SUBCLASSPROC,
							UINT_PTR);
__declspec(dllimport) LRESULT WINAPI DefSubclassOrdinal(HWND, UINT, WPARAM,
							LPARAM);
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b)
{
	return m == WM_APP ? 1 : DefWindowProcW(w, m, a, b);
}
static LRESULT CALLBACK subclass(HWND w, UINT m, WPARAM a, LPARAM b,
				 UINT_PTR id, DWORD_PTR data)
{
	(void)id;
	return m == WM_APP ? DefSubclassOrdinal(w, m, a, b) + data :
			     DefSubclassOrdinal(w, m, a, b);
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	HMODULE module = GetModuleHandleW(0);
	InitControlsOrdinal();
	INITCOMMONCONTROLSEX init = { sizeof(init), ICC_WIN95_CLASSES };
	ok &= InitCommonControlsEx(&init);
	WNDCLASSEXW c = { 0 };
	c.cbSize = sizeof(c);
	c.hInstance = module;
	c.lpfnWndProc = proc;
	c.lpszClassName = L"OrdinalTests";
	ok &= RegisterClassExW(&c) != 0;
	HWND w = CreateWindowExW(0, c.lpszClassName, L"hidden", 0, 0, 0, 0, 0,
				 0, 0, module, 0);
	HWND toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, 0, WS_CHILD, 0, 0,
				       0, 0, w, (HMENU)1, module, 0);
	DWORD_PTR ref = 0;
	ok &= w && toolbar && SetSubclassOrdinal(w, subclass, 7, 13) &&
	      GetSubclassOrdinal(w, subclass, 7, &ref) && ref == 13;
	ok &= GetSubclassOrdinal(w, subclass, 7, 0) &&
	      SendMessageW(w, WM_APP, 0, 0) == 14;
	ok &= RemoveSubclassOrdinal(w, subclass, 7) &&
	      !GetSubclassOrdinal(w, subclass, 7, &ref) &&
	      SendMessageW(w, WM_APP, 0, 0) == 1;
	HICON icon = (HICON)1;
	ok &= LoadScaledOrdinal(module, MAKEINTRESOURCEW(400), 2, 2, &icon) ==
		      S_OK &&
	      icon;
	HIMAGELIST list = ImageList_Create(2, 2, ILC_COLOR32 | ILC_MASK, 0, 1);
	ok &= list && ImageList_ReplaceIcon(list, -1, icon) == 0;
	BITMAPINFO info = { 0 };
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = 2;
	info.bmiHeader.biHeight = -2;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	DWORD *pixels = 0;
	HBITMAP bitmap = CreateDIBSection(0, &info, DIB_RGB_COLORS,
					  (void **)&pixels, 0, 0);
	HDC dc = CreateCompatibleDC(0);
	HGDIOBJ old = SelectObject(dc, bitmap);
	if (pixels)
		for (int i = 0; i < 4; i++)
			pixels[i] = 0x102030;
	ok &= bitmap && dc && old &&
	      ImageList_Draw(list, 0, dc, 0, 0, ILD_TRANSPARENT);
	ok &= pixels && (pixels[0] & 0xffffff) == 0xff0000 &&
	      (pixels[1] & 0xffffff) == 0x089018 &&
	      (pixels[2] & 0xffffff) == 0x102030 &&
	      (pixels[3] & 0xffffff) == 0xffffff;
	ok &= DestroyIcon(icon) && ImageList_Destroy(list);
	icon = (HICON)1;
	ok &= FAILED(LoadScaledOrdinal(module, MAKEINTRESOURCEW(999), 2, 2,
				       &icon)) &&
	      !icon;
	icon = (HICON)1;
	ok &= FAILED(LoadScaledOrdinal(module, MAKEINTRESOURCEW(400), 0, 2,
				       &icon)) &&
	      !icon;
	ok &= FAILED(
		LoadIconWithScaleDown(module, MAKEINTRESOURCEW(400), 2, 2, 0));
	ok &= LoadIconWithScaleDown(module, MAKEINTRESOURCEW(400), 3, 1,
				    &icon) == S_OK &&
	      icon && DestroyIcon(icon);
	ok &= SelectObject(dc, old) == bitmap && DeleteDC(dc) &&
	      DeleteObject(bitmap);
	ok &= DestroyWindow(w) && UnregisterClassW(c.lpszClassName, module);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "commctl ordinals ok\n" : "commctl ordinals FAIL\n",
		  ok ? 20 : 22, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
