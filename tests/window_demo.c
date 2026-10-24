#include <windows.h>
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_PAINT) {
		PAINTSTRUCT paint;
		RECT area;
		HDC dc = BeginPaint(hwnd, &paint);
		GetClientRect(hwnd, &area);
		HBRUSH background = CreateSolidBrush(RGB(24, 32, 48)),
		       accent = CreateSolidBrush(RGB(55, 145, 240));
		FillRect(dc, &area, background);
		RECT box = { 24, 24, area.right - 24, 90 };
		FillRect(dc, &box, accent);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 255, 255));
		HFONT font = CreateFontW(-24, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0,
					 0, L"sans");
		HGDIOBJ old = SelectObject(dc, font);
		TextOutW(dc, 40, 42, L"Native Windows app on Linux", 27);
		SelectObject(dc, old);
		DeleteObject(font);
		TextOutW(dc, 24, 120, L"Original USER32 + GDI / X11 + FreeType",
			 36);
		TextOutW(dc, 24, 150,
			 L"Resize this window. Press Escape to close.", 41);
		DeleteObject(background);
		DeleteObject(accent);
		EndPaint(hwnd, &paint);
		return 0;
	}
	if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
		DestroyWindow(hwnd);
		return 0;
	}
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
void mainCRTStartup(void)
{
	HINSTANCE instance = GetModuleHandleW(0);
	static WNDCLASSW cls;
	cls.lpfnWndProc = window_proc;
	cls.hInstance = instance;
	cls.lpszClassName = L"NativePEDemo";
	if (!RegisterClassW(&cls))
		ExitProcess(1);
	HWND hwnd = CreateWindowExW(0, cls.lpszClassName,
				    L"binfmt_pe — original Windows graphics",
				    WS_OVERLAPPEDWINDOW, 80, 80, 560, 240, 0, 0,
				    instance, 0);
	if (!hwnd)
		ExitProcess(2);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	MSG msg;
	int result;
	while ((result = GetMessageW(&msg, 0, 0, 0)) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	ExitProcess(result < 0 ? 3 : 0);
}
