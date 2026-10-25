#include <windows.h>
void mainCRTStartup(void)
{
	static WNDCLASSW cls;
	cls.hInstance = GetModuleHandleW(0);
	cls.lpfnWndProc = DefWindowProcW;
	cls.lpszClassName = L"BridgeDisplayFailure";
	int ok = RegisterClassW(&cls) != 0;
	HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"Must not appear",
				    WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100,
				    100, 0, 0, cls.hInstance, 0);
	ok &= !hwnd && GetLastError() == ERROR_MOD_NOT_FOUND;
	ok &= UnregisterClassW(cls.lpszClassName, cls.hInstance);
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "gui failure ok\n" : "gui failure FAIL\n", ok ? 15 : 17,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
