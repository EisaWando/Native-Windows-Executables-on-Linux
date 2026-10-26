#define UNICODE
#include <windows.h>
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	HMODULE module = GetModuleHandleW(0);
	WNDCLASSEXW c = { 0 };
	c.cbSize = sizeof(c);
	c.lpfnWndProc = DefWindowProcW;
	c.hInstance = module;
	c.lpszClassName = L"ScrollState";
	ok &= RegisterClassExW(&c) != 0;
	HWND w = CreateWindowExW(0, c.lpszClassName, L"hidden", 0, 0, 0, 0, 0,
				 0, 0, module, 0);
	HWND child = CreateWindowExW(0, L"Static", L"", WS_CHILD, 0, 0, 0, 0, w,
				     (HMENU)1, module, 0);
	SCROLLINFO info = { sizeof(info), SIF_ALL };
	ok &= w && child && GetScrollInfo(w, SB_VERT, &info) &&
	      info.nMin == 0 && info.nMax == 100 && !info.nPos && !info.nPage;
	info = (SCROLLINFO){
		sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, -5, 10, 4, 100, 0
	};
	ok &= SetScrollInfo(w, SB_VERT, &info, FALSE) == 7 &&
	      GetScrollPos(w, SB_VERT) == 7;
	info = (SCROLLINFO){ sizeof(info), SIF_ALL };
	ok &= GetScrollInfo(w, SB_VERT, &info) && info.nMin == -5 &&
	      info.nMax == 10 && info.nPage == 4 && info.nPos == 7 &&
	      info.nTrackPos == 7;
	ok &= SetScrollPos(w, SB_VERT, -100, FALSE) == 7 &&
	      GetScrollPos(w, SB_VERT) == -5;
	ok &= GetScrollPos(w, SB_HORZ) == 0 &&
	      GetScrollPos(child, SB_VERT) == 0;
	int min, max;
	ok &= GetScrollRange(w, SB_VERT, &min, &max) && min == -5 && max == 10;
	ok &= SetScrollRange(w, SB_HORZ, 1, 20, FALSE) &&
	      GetScrollRange(w, SB_HORZ, &min, &max) && min == 1 && max == 20;
	ok &= !SetScrollRange(w, SB_HORZ, 20, 1, FALSE) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	info = (SCROLLINFO){ sizeof(info), SIF_PAGE, 0, 0, 1000, 0, 0 };
	ok &= SetScrollInfo(w, SB_VERT, &info, FALSE) == -5;
	info = (SCROLLINFO){ sizeof(info), SIF_ALL };
	ok &= GetScrollInfo(w, SB_VERT, &info) && info.nPage == 16 &&
	      !(GetWindowLongW(w, GWL_STYLE) & WS_VSCROLL);
	info.fMask = SIF_PAGE | SIF_DISABLENOSCROLL;
	info.nPage = 1000;
	SetScrollInfo(w, SB_VERT, &info, FALSE);
	ok &= (GetWindowLongW(w, GWL_STYLE) & WS_VSCROLL) != 0;
	ok &= ShowScrollBar(w, SB_BOTH, FALSE) &&
	      !(GetWindowLongW(w, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL));
	ok &= ShowScrollBar(w, SB_BOTH, TRUE) &&
	      (GetWindowLongW(w, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL)) ==
		      (WS_VSCROLL | WS_HSCROLL);
	ok &= EnableScrollBar(w, SB_HORZ, ESB_DISABLE_BOTH) &&
	      !EnableScrollBar(w, SB_HORZ, ESB_DISABLE_BOTH);
	ok &= EnableScrollBar(w, SB_BOTH, ESB_ENABLE_BOTH);
	ok &= ShowScrollBar(child, SB_CTL, TRUE) &&
	      (GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE);
	info = (SCROLLINFO){ sizeof(info),
			     SIF_RANGE | SIF_PAGE | SIF_POS,
			     (-2147483647 - 1),
			     2147483647,
			     0xffffffffu,
			     2147483647,
			     0 };
	ok &= SetScrollInfo(w, SB_VERT, &info, FALSE) == -2147483647;
	info = (SCROLLINFO){ 24, SIF_POS };
	ok &= GetScrollInfo(w, SB_VERT, &info) && info.nPos == -2147483647;
	info.fMask = SIF_TRACKPOS;
	ok &= !GetScrollInfo(w, SB_VERT, &info) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	info.cbSize = sizeof(info);
	ok &= !GetScrollInfo(w, SB_BOTH, &info) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !GetScrollInfo(w, SB_VERT, 0) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !EnableScrollBar(w, SB_VERT, 4) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= DestroyWindow(w) && !GetScrollInfo(w, SB_VERT, &info) &&
	      GetLastError() == ERROR_INVALID_WINDOW_HANDLE;
	ok &= UnregisterClassW(c.lpszClassName, module);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "scroll state ok\n" : "scroll state FAIL\n",
		  ok ? 16 : 18, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
