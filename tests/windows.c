#include <windows.h>
#include <commctrl.h>
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
static LRESULT CALLBACK test_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
				      UINT_PTR id, DWORD_PTR data)
{
	if (msg == WM_APP + 30)
		return id + data;
	if (msg == WM_APP + 31) {
		RemoveWindowSubclass(h, test_subclass, id);
		return DefSubclassProc(h, msg, wp, lp);
	}
	return DefSubclassProc(h, msg, wp, lp);
}
static void test_hidden_windows(void)
{
	HINSTANCE instance = GetModuleHandleW(0);
	static WNDCLASSEXW c;
	c.cbSize = sizeof(c);
	c.lpfnWndProc = DefWindowProcW;
	c.hInstance = instance;
	c.lpszClassName = L"BridgeHiddenState";
	c.cbWndExtra = 8;
	CHECK(RegisterClassExW(&c));
	DWORD ex = WS_EX_ACCEPTFILES | WS_EX_TOOLWINDOW | WS_EX_APPWINDOW |
		   WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_CLIENTEDGE;
	HWND w = CreateWindowExW(ex, c.lpszClassName, L"hidden",
				 WS_OVERLAPPEDWINDOW | WS_DISABLED, 0, 0, 0, 0,
				 0, 0, instance, 0);
	CHECK(w && IsWindow(w) && !IsWindowVisible(w));
	CHECK((DWORD)GetWindowLongPtrW(w, GWL_EXSTYLE) == ex);
	RECT r;
	CHECK(GetClientRect(w, &r) && r.right == 0 && r.bottom == 0);
	CHECK(SetWindowTextW(w, L"hidden title"));
	WCHAR text[32];
	CHECK(GetWindowTextW(w, text, 32) == 12);
	CHECK(SetPropW(w, L"UseImmersiveDarkModeColors", 0));
	CHECK(SetPropA(w, "example", (HANDLE)123) &&
	      GetPropW(w, L"EXAMPLE") == (HANDLE)123);
	CHECK(RemovePropA(w, "example") == (HANDLE)123 &&
	      !GetPropW(w, L"example"));
	CHECK(SetPropW(w, (LPCWSTR)42, (HANDLE)456) &&
	      GetPropW(w, (LPCWSTR)42) == (HANDLE)456);
	DWORD_PTR data;
	CHECK(SetWindowSubclass(w, test_subclass, 42, 100) &&
	      GetWindowSubclass(w, test_subclass, 42, &data) && data == 100);
	CHECK(SendMessageW(w, WM_APP + 30, 0, 0) == 142);
	CHECK(SetWindowSubclass(w, test_subclass, 42, 200) &&
	      SendMessageW(w, WM_APP + 30, 0, 0) == 242);
	CHECK(SetWindowSubclass(w, test_subclass, 43, 10) &&
	      SendMessageW(w, WM_APP + 30, 0, 0) == 53);
	CHECK(RemoveWindowSubclass(w, test_subclass, 43) &&
	      SendMessageW(w, WM_APP + 30, 0, 0) == 242);
	CHECK(SendMessageW(w, WM_APP + 31, 0, 0) == 0 &&
	      !GetWindowSubclass(w, test_subclass, 42, &data));
	CHECK(UpdateWindow(w));
	CHECK(EnableWindow(w, TRUE) && IsWindowEnabled(w));
	CHECK(SetFocus(w) == 0 && GetFocus() == w);
	CHECK(SetActiveWindow(w) == 0 && GetActiveWindow() == w);
	CHECK(!EnableWindow(w, FALSE) && !IsWindowEnabled(w) &&
	      GetFocus() == 0);
	EnableWindow(w, TRUE);
	DragAcceptFiles(w, FALSE);
	CHECK(!(GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_ACCEPTFILES));
	DragAcceptFiles(w, TRUE);
	CHECK(SetWindowPos(w, 0, 20, 30, 120, 80,
			   SWP_NOZORDER | SWP_NOACTIVATE));
	CHECK(GetWindowRect(w, &r) && r.left == 20 && r.top == 30 &&
	      r.right == 140 && r.bottom == 110);
	CHECK(GetClientRect(w, &r) && r.right == 120 && r.bottom == 80);
	CHECK(MoveWindow(w, 0, 0, -5, -7, FALSE));
	CHECK(GetWindowRect(w, &r) && r.right == -5 && r.bottom == -7);
	CHECK(!ShowWindow(w, SW_HIDE) && !IsWindowVisible(w));
	CHECK(DestroyWindow(w));
	CHECK(GetActiveWindow() == 0);
	w = CreateWindowExW(0, c.lpszClassName, 0, 0, CW_USEDEFAULT,
			    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
			    instance, 0);
	CHECK(w && GetWindowRect(w, &r) && r.right > r.left &&
	      r.bottom > r.top);
	CHECK(DestroyWindow(w));
	CHECK(!CreateWindowExW(0, L"NoSuchBridgeWindowClass", 0, 0, 0, 0, 0, 0,
			       0, 0, instance, 0) &&
	      GetLastError() == 1407);
	CHECK(UnregisterClassW(c.lpszClassName, instance));
}

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
	test_hidden_windows();
#ifdef PE_HIDDEN_ONLY
	DWORD output;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  failures ? "hidden windows FAIL\n" : "hidden windows ok\n",
		  failures ? 20 : 18, &output, 0);
	ExitProcess(failures ? 1 : 0);
#endif

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
