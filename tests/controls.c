#include <windows.h>
#include <commctrl.h>
static LONG failures;
static DWORD main_tid, child_tid;
static HWND parent;
static HANDLE ready;
static int exit_mode;
#define CHECK(x)                                         \
	do {                                             \
		if (!(x))                                \
			InterlockedIncrement(&failures); \
	} while (0)
static LRESULT CALLBACK callback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_APP + 5) {
		CHECK(GetCurrentThreadId() == main_tid);
		CHECK(SendMessageW((HWND)wp, WM_APP + 6, 0, 0) == 333);
		return 444;
	}
	if (msg == WM_APP + 6) {
		CHECK(GetCurrentThreadId() == child_tid);
		return 333;
	}
	if (msg == WM_APP + 8)
		ExitThread(9);
	return DefWindowProcW(hwnd, msg, wp, lp);
}
static DWORD WINAPI worker(void *arg)
{
	HWND hwnd = CreateWindowExW(0, L"BridgeControlParent", 0, 0, 0, 0, 0, 0,
				    0, 0, GetModuleHandleW(0), 0);
	*(HWND *)arg = hwnd;
	child_tid = GetCurrentThreadId();
	SetEvent(ready);
	if (!exit_mode) {
		CHECK(SendMessageW(parent, WM_APP + 5, (WPARAM)hwnd, 0) == 444);
		PostMessageW(parent, WM_APP + 7, 0, 0);
		return 0;
	}
	MSG msg;
	while (GetMessageW(&msg, 0, 0, 0) > 0)
		DispatchMessageW(&msg);
	return 1;
}
void mainCRTStartup(void)
{
	main_tid = GetCurrentThreadId();
	static WNDCLASSW cls;
	cls.lpfnWndProc = callback;
	cls.hInstance = GetModuleHandleW(0);
	cls.lpszClassName = L"BridgeControlParent";
	CHECK(RegisterClassW(&cls));
	parent = CreateWindowExW(0, cls.lpszClassName, 0, 0, 0, 0, 100, 100, 0,
				 0, cls.hInstance, 0);
	CHECK(parent != 0);
	INITCOMMONCONTROLSEX init = { sizeof(init), 0xffff };
	CHECK(InitCommonControlsEx(&init));
	static const WCHAR *names[] = {
		L"Button",	     L"Static",		 L"Edit",
		L"ListBox",	     L"ComboBox",	 L"ToolbarWindow32",
		L"ReBarWindow32",    L"SysTabControl32", L"msctls_statusbar32",
		L"tooltips_class32", L"SysListView32",	 L"msctls_updown32"
	};
	HWND controls[12];
	for (int i = 0; i < 12; i++) {
		controls[i] = CreateWindowExW(0, names[i], L"initial",
					      WS_CHILD | WS_VISIBLE, 0, 0, 100,
					      20, parent,
					      (HMENU)(INT_PTR)(i + 1),
					      cls.hInstance, 0);
		CHECK(controls[i] && GetParent(controls[i]) == parent &&
		      !IsWindowVisible(controls[i]));
	}
	CHECK(SendMessageW(controls[0], BM_SETCHECK, BST_CHECKED, 0) == 0 &&
	      SendMessageW(controls[0], BM_GETCHECK, 0, 0) == BST_CHECKED);
	WCHAR text[64];
	CHECK(SetWindowTextW(controls[2], L"edited text") &&
	      GetWindowTextW(controls[2], text, 64) == 11);
	SendMessageW(controls[2], EM_SETSEL, 2, 5);
	DWORD a, b;
	SendMessageW(controls[2], EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
	CHECK(a == 2 && b == 5);
	CHECK(SendMessageW(controls[3], LB_ADDSTRING, 0, (LPARAM)L"one") == 0 &&
	      SendMessageW(controls[3], LB_ADDSTRING, 0, (LPARAM)L"two") == 1);
	CHECK(SendMessageW(controls[3], LB_GETCOUNT, 0, 0) == 2 &&
	      SendMessageW(controls[3], LB_SETCURSEL, 1, 0) == 1 &&
	      SendMessageW(controls[3], LB_GETCURSEL, 0, 0) == 1);
	CHECK(SendMessageW(controls[3], LB_GETTEXT, 1, (LPARAM)text) == 3);
	SendMessageW(controls[3], LB_SETITEMDATA, 1, 123);
	CHECK(SendMessageW(controls[3], LB_GETITEMDATA, 1, 0) == 123);
	CHECK(SendMessageW(controls[4], CB_ADDSTRING, 0, (LPARAM)L"choice") ==
		      0 &&
	      SendMessageW(controls[4], CB_SETCURSEL, 0, 0) == 0 &&
	      SendMessageW(controls[4], CB_GETCOUNT, 0, 0) == 1);
	static TBBUTTON buttons[2];
	buttons[0].idCommand = 100;
	buttons[1].idCommand = 101;
	SendMessageW(controls[5], TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	CHECK(SendMessageW(controls[5], TB_ADDBUTTONSW, 2, (LPARAM)buttons) &&
	      SendMessageW(controls[5], TB_BUTTONCOUNT, 0, 0) == 2);
	TBBUTTON out;
	CHECK(SendMessageW(controls[5], TB_GETBUTTON, 1, (LPARAM)&out) &&
	      out.idCommand == 101);
	static REBARBANDINFOW band;
	band.cbSize = sizeof(band);
	CHECK(SendMessageW(controls[6], RB_INSERTBANDW, -1, (LPARAM)&band) &&
	      SendMessageW(controls[6], RB_GETBANDCOUNT, 0, 0) == 1);
	TCITEMW item = { 0 };
	item.mask = TCIF_TEXT;
	item.pszText = L"tab";
	CHECK(SendMessageW(controls[7], TCM_INSERTITEMW, 0, (LPARAM)&item) ==
		      0 &&
	      SendMessageW(controls[7], TCM_GETITEMCOUNT, 0, 0) == 1);
	int parts[] = { 50, -1 }, back[2];
	CHECK(SendMessageW(controls[8], SB_SETPARTS, 2, (LPARAM)parts) &&
	      SendMessageW(controls[8], SB_GETPARTS, 2, (LPARAM)back) == 2 &&
	      back[0] == 50 && back[1] == -1);
	CHECK(SendMessageW(controls[8], SB_SETTEXTW, 0, (LPARAM)L"status") &&
	      SendMessageW(controls[8], SB_GETTEXTW, 0, (LPARAM)text) == 6);
	TOOLINFOW tool = { 0 };
	tool.cbSize = sizeof(tool);
	CHECK(SendMessageW(controls[9], TTM_ADDTOOLW, 0, (LPARAM)&tool) &&
	      SendMessageW(controls[9], TTM_GETTOOLCOUNT, 0, 0) == 1);
	LVITEMW listitem = { 0 };
	listitem.mask = LVIF_TEXT;
	listitem.pszText = L"item";
	CHECK(SendMessageW(controls[10], LVM_INSERTITEMW, 0,
			   (LPARAM)&listitem) == 0 &&
	      SendMessageW(controls[10], LVM_GETITEMCOUNT, 0, 0) == 1);
	SendMessageW(controls[11], UDM_SETRANGE32, -100, 100);
	SendMessageW(controls[11], UDM_SETPOS32, 0, 33);
	CHECK(SendMessageW(controls[11], UDM_GETPOS32, 0, 0) == 33);
	ready = CreateEventW(0, TRUE, FALSE, 0);
	HWND child;
	HANDLE thread = CreateThread(0, 0, worker, &child, 0, 0);
	CHECK(thread && WaitForSingleObject(ready, 5000) == 0);
	MSG msg;
	while (GetMessageW(&msg, 0, 0, 0) > 0) {
		if (msg.message == WM_APP + 7)
			break;
		DispatchMessageW(&msg);
	}
	CHECK(WaitForSingleObject(thread, 5000) == 0);
	CloseHandle(thread);
	exit_mode = 1;
	ResetEvent(ready);
	thread = CreateThread(0, 0, worker, &child, 0, 0);
	CHECK(thread && WaitForSingleObject(ready, 5000) == 0);
	CHECK(SendMessageW(child, WM_APP + 8, 0, 0) == 0 &&
	      GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
	CHECK(WaitForSingleObject(thread, 5000) == 0);
	CloseHandle(thread);
	CloseHandle(ready);
	CHECK(DestroyWindow(parent));
	for (int i = 0; i < 12; i++)
		CHECK(!IsWindow(controls[i]));
	CHECK(UnregisterClassW(cls.lpszClassName, cls.hInstance));
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  failures ? "controls FAIL\n" : "controls ok\n",
		  failures ? 14 : 12, &written, 0);
	ExitProcess(failures ? 1 : 0);
}
