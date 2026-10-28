#include <windows.h>
static HHOOK first, second;
static int sequence, good=1;
static LRESULT CALLBACK hook1(int code,WPARAM wp,LPARAM lp) {
    CWPSTRUCT *c=(CWPSTRUCT*)lp;
    if(code==HC_ACTION && c->message==WM_APP+1) { if(sequence++!=1 || !wp) good=0; }
    return CallNextHookEx(first,code,wp,lp);
}
static LRESULT CALLBACK hook2(int code,WPARAM wp,LPARAM lp) {
    CWPSTRUCT *c=(CWPSTRUCT*)lp;
    if(code==HC_ACTION && c->message==WM_APP+1) { if(sequence++!=0 || !wp) good=0; }
    return CallNextHookEx(second,code,wp,lp);
}
static LRESULT CALLBACK wnd(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_APP+1) return 42;
    return DefWindowProcW(h,m,w,l);
}
void mainCRTStartup(void) {
    WNDCLASSW c={0}; c.lpfnWndProc=wnd; c.hInstance=GetModuleHandleW(NULL); c.lpszClassName=L"HookTest";
    if(!RegisterClassW(&c)) ExitProcess(1);
    HWND h=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,1,1,NULL,NULL,c.hInstance,NULL);
    if(!h) ExitProcess(2);
    first=SetWindowsHookExW(WH_CALLWNDPROC,hook1,NULL,GetCurrentThreadId());
    second=SetWindowsHookExA(WH_CALLWNDPROC,hook2,NULL,GetCurrentThreadId());
    if(!first || !second || SendMessageW(h,WM_APP+1,0,0)!=42 || !good || sequence!=2) ExitProcess(3);
    if(!UnhookWindowsHookEx(second) || !UnhookWindowsHookEx(first)) ExitProcess(4);
    SetLastError(0); if(UnhookWindowsHookEx(first) || GetLastError()!=1404) ExitProcess(5);
    SetLastError(0); if(SetWindowsHookExW(99,hook1,NULL,GetCurrentThreadId()) || GetLastError()!=87) ExitProcess(6);
    if(CallNextHookEx(NULL,0,0,0)!=0) ExitProcess(7);
    DWORD n; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"hooks ok\n",9,&n,NULL); ExitProcess(0);
}
