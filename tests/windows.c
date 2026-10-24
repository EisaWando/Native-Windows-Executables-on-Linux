#include <windows.h>
static LONG failures;
static int creates, destroys, ncdestroys, paints;
static HBRUSH blue;
static HWND main_window, worker_window;
static HANDLE ready;
#define CHECK(x)                                         \
	do {                                             \
		if (!(x))                                \
			InterlockedIncrement(&failures); \
	} while (0)
static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE) {
		CREATESTRUCTW *cs = (void *)lp;
		if (cs->lpCreateParams == (void *)1)
			return FALSE;
		if (cs->lpCreateParams == (void *)3) {
			DestroyWindow(hwnd);
			return TRUE;
		}
		SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				  (LONG_PTR)cs->lpCreateParams);
		return TRUE;
	}
	if (msg == WM_CREATE) {
		creates++;
		if (GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 2)
			return -1;
		return 0;
	}
	if (msg == WM_APP + 1)
		return 1234;
	if (msg == WM_PAINT) {
		PAINTSTRUCT ps;
		RECT r;
		HDC dc = BeginPaint(hwnd, &ps);
		CHECK(dc != 0 && GetClientRect(hwnd, &r));
		CHECK(GetPixel(dc, 2, 2) == RGB(30, 70, 160));
		CHECK(FillRect(dc, &r, blue));
		CHECK(GetPixel(dc, 2, 2) == RGB(30, 70, 160));
		CHECK(SetPixel(dc, 3, 3, RGB(255, 40, 20)) == RGB(255, 40, 20));
		CHECK(GetPixel(dc, 3, 3) == RGB(255, 40, 20));
		CHECK(SetBkMode(dc, TRANSPARENT) == OPAQUE);
		SetTextColor(dc, RGB(255, 255, 255));
		CHECK(TextOutW(dc, 20, 20, L"Native Windows on Linux — café",
			       30));
		int ink = 0;
		for (int y = 20; y < 45; y += 2)
			for (int x = 20; x < 240; x += 2)
				if (GetPixel(dc, x, y) != RGB(30, 70, 160))
					ink++;
		CHECK(ink > 10);
		HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 220, 0));
		HGDIOBJ old = SelectObject(dc, pen);
		CHECK(MoveToEx(dc, 10, 80, 0) && LineTo(dc, 150, 80));
		CHECK(GetPixel(dc, 40, 80) == RGB(255, 220, 0));
		SelectObject(dc, old);
		DeleteObject(pen);
		CHECK(EndPaint(hwnd, &ps));
		paints++;
		PostMessageW(hwnd, WM_APP + 2, 0, 0);
		return 0;
	}
	if (msg == WM_DESTROY) {
		destroys++;
		if (hwnd == main_window || hwnd == worker_window)
			PostQuitMessage(0);
		return 0;
	}
	if (msg == WM_NCDESTROY) {
		ncdestroys++;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
static DWORD WINAPI worker(void *parameter)
{
	HINSTANCE instance = parameter;
	worker_window = CreateWindowExW(0, L"BridgeWindowTest", L"worker",
					WS_OVERLAPPEDWINDOW, 20, 20, 100, 100,
					0, 0, instance, 0);
	CHECK(worker_window != 0);
	SetEvent(ready);
	MSG msg;
	while (GetMessageW(&msg, 0, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}
void mainCRTStartup(void)
{
	HINSTANCE instance = GetModuleHandleW(0);
	static WNDCLASSEXW cls;
	cls.cbSize = sizeof(cls);
	cls.lpfnWndProc = proc;
	cls.hInstance = instance;
	cls.lpszClassName = L"BridgeWindowTest";
	cls.cbWndExtra = 8;
	blue = CreateSolidBrush(RGB(30, 70, 160));
	cls.hbrBackground = blue;
	CHECK(RegisterClassExW(&cls) != 0);
	CHECK(!CreateWindowExW(0, cls.lpszClassName, L"reject", 0, 0, 0, 100,
			       100, 0, 0, instance, (void *)1));
	CHECK(creates == 0 && destroys == 0 && ncdestroys == 1);
	CHECK(!CreateWindowExW(0, cls.lpszClassName, L"reject", 0, 0, 0, 100,
			       100, 0, 0, instance, (void *)2));
	CHECK(creates == 1 && destroys == 1 && ncdestroys == 2);
	CHECK(!CreateWindowExW(0, cls.lpszClassName, L"destroy during NC", 0, 0,
			       0, 100, 100, 0, 0, instance, (void *)3));
	CHECK(creates == 1 && destroys == 2 && ncdestroys == 3);
	main_window = CreateWindowExW(0, cls.lpszClassName,
				      L"Native PE drawing test",
				      WS_OVERLAPPEDWINDOW, 80, 80, 420, 200, 0,
				      0, instance, (void *)77);
	CHECK(main_window != 0 && IsWindow(main_window) &&
	      IsWindowUnicode(main_window));
	if (!main_window) {
		DWORD written;
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "windows FAIL\n", 13,
			  &written, 0);
		ExitProcess(1);
	}
	CHECK(GetWindowLongPtrW(main_window, GWLP_USERDATA) == 77);
	CHECK(SetWindowLongPtrW(main_window, 0, 0x12345678) == 0 &&
	      GetWindowLongPtrW(main_window, 0) == 0x12345678);
	CHECK(!UnregisterClassW(cls.lpszClassName, instance) &&
	      GetLastError() == ERROR_CLASS_HAS_WINDOWS);
	CHECK(SendMessageW(main_window, WM_APP + 1, 0, 0) == 1234);
	CHECK(SetWindowTextW(main_window, L"Native PE drawing verified"));
	WCHAR title[80];
	CHECK(GetWindowTextW(main_window, title, 80) == 26);
	CHECK(!ShowWindow(main_window, SW_SHOW));
	CHECK(IsWindowVisible(main_window));
	MSG msg;
	CHECK(PostMessageW(main_window, WM_APP + 3, 99, 100));
	CHECK(PeekMessageW(&msg, main_window, WM_APP + 3, WM_APP + 3,
			   PM_NOREMOVE) &&
	      msg.wParam == 99 && msg.lParam == 100);
	CHECK(PeekMessageW(&msg, main_window, WM_APP + 3, WM_APP + 3,
			   PM_REMOVE));
	CHECK(!PeekMessageW(&msg, main_window, WM_APP + 3, WM_APP + 3,
			    PM_REMOVE));
	ready = CreateEventW(0, TRUE, FALSE, 0);
	HANDLE thread = CreateThread(0, 0, worker, instance, 0, 0);
	CHECK(thread != 0 && WaitForSingleObject(ready, 5000) == 0);
	CHECK(GetWindowThreadProcessId(worker_window, 0) !=
	      GetCurrentThreadId());
	CHECK(!DestroyWindow(worker_window) &&
	      GetLastError() == ERROR_ACCESS_DENIED);
	CHECK(PostMessageW(worker_window, WM_CLOSE, 0, 0));
	CHECK(WaitForSingleObject(thread, 5000) == 0);
	CloseHandle(thread);
	CloseHandle(ready);
	int result;
	while ((result = GetMessageW(&msg, 0, 0, 0)) > 0) {
		if (msg.message == WM_APP + 2) {
			PostMessageW(main_window, WM_CLOSE, 0, 0);
		} else {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	CHECK(result == 0 && paints > 0 && !IsWindow(main_window));
	CHECK(UnregisterClassW(cls.lpszClassName, instance));
	CHECK(DeleteObject(blue));
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  failures ? "windows FAIL\n" : "windows ok\n",
		  failures ? 13 : 11, &written, 0);
	ExitProcess(failures ? 1 : 0);
}
