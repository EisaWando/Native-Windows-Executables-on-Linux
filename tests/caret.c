#define UNICODE
#include <windows.h>
static DWORD WINAPI other_thread(void *window)
{
	POINT p;
	return !GetCaretPos(&p) && !ShowCaret(window) &&
			       !CreateCaret(window, 0, 1, 12) ?
		       0 :
		       1;
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written, code;
	WNDCLASSEXW c = { 0 };
	c.cbSize = sizeof(c);
	c.lpfnWndProc = DefWindowProcW;
	c.hInstance = GetModuleHandleW(0);
	c.lpszClassName = L"caret-test";
	ok &= RegisterClassExW(&c) != 0;
	HWND w = CreateWindowExW(0, c.lpszClassName, L"hidden", 0, 0, 0, 0, 0,
				 0, 0, c.hInstance, 0);
	POINT p;
	ok &= w && !GetCaretPos(&p) && !DestroyCaret();
	UINT blink = GetCaretBlinkTime();
	ok &= blink > 0 && SetCaretBlinkTime(321) && GetCaretBlinkTime() == 321;
	ok &= SetCaretBlinkTime(blink);
	ok &= CreateCaret(w, 0, 1, 12) && GetCaretPos(&p) && p.x == 0 &&
	      p.y == 0;
	ok &= SetCaretPos(-3, 41) && GetCaretPos(&p) && p.x == -3 && p.y == 41;
	ok &= HideCaret(w) && HideCaret(w) && ShowCaret(w) && ShowCaret(0) &&
	      ShowCaret(w);
	HANDLE t = CreateThread(0, 0, other_thread, w, 0, 0);
	ok &= t && WaitForSingleObject(t, 5000) == WAIT_OBJECT_0 &&
	      GetExitCodeThread(t, &code) && code == 0;
	CloseHandle(t);
	ok &= GetCaretPos(&p) && p.x == -3 && p.y == 41;
	ok &= DestroyCaret() && !GetCaretPos(&p) && !ShowCaret(w);
	ok &= CreateCaret(w, (HBITMAP)1, 0, 0) && DestroyWindow(w) &&
	      !GetCaretPos(&p);
	ok &= UnregisterClassW(c.lpszClassName, c.hInstance);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "caret ok\n" : "caret FAIL\n", ok ? 9 : 11, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
