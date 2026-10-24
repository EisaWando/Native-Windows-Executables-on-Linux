#include <windows.h>
static HANDLE ready;
static DWORD child_tid;
static DWORD WINAPI child(void *arg)
{
	(void)arg;
	MSG msg;
	PeekMessageW(&msg, 0, 0, 0, PM_NOREMOVE);
	child_tid = GetCurrentThreadId();
	SetEvent(ready);
	if (GetMessageW(&msg, (HWND)-1, 0, 0) != 1 ||
	    msg.message != WM_APP + 9 || msg.wParam != 11 || msg.lParam != 22)
		return 1;
	return 0;
}
void mainCRTStartup(void)
{
	int ok = 1;
	MSG msg;
	ok &= !PeekMessageW(&msg, 0, 0, 0, PM_REMOVE);
	ok &= PostMessageW(0, WM_APP + 1, 123, 456);
	ok &= PeekMessageW(&msg, (HWND)-1, WM_APP + 1, WM_APP + 1,
			   PM_NOREMOVE) &&
	      msg.wParam == 123 && msg.lParam == 456;
	ok &= PeekMessageW(&msg, (HWND)-1, 0, 0, PM_REMOVE) &&
	      msg.message == WM_APP + 1;
	ok &= !PeekMessageW(&msg, 0, 0, 0, PM_REMOVE);
	PostQuitMessage(42);
	ok &= GetMessageW(&msg, 0, WM_APP, WM_APP + 2) == 0 &&
	      msg.wParam == 42 && msg.message == WM_QUIT;
	ok &= GetMessageW(&msg, 0, 10, 1) == -1 &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !PostThreadMessageW(0x7ffffffe, WM_APP, 0, 0) &&
	      GetLastError() == ERROR_INVALID_THREAD_ID;
	ready = CreateEventW(0, TRUE, FALSE, 0);
	HANDLE thread = CreateThread(0, 0, child, 0, 0, 0);
	ok &= thread && WaitForSingleObject(ready, 5000) == 0;
	ok &= PostThreadMessageW(child_tid, WM_APP + 9, 11, 22) &&
	      WaitForSingleObject(thread, 5000) == 0;
	DWORD result;
	GetExitCodeThread(thread, &result);
	ok &= result == 0;
	CloseHandle(thread);
	CloseHandle(ready);
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "queues ok\n" : "queues FAIL\n", ok ? 10 : 12, &written,
		  0);
	ExitProcess(ok ? 0 : 1);
}
