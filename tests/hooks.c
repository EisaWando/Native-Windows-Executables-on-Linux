#include <windows.h>
static HHOOK first, second;
static int sequence, good=1;
static int returned, retrieved;
static int veto, saw_create, saw_key, saw_filter;
static HHOOK self_hook; static int self_calls;
static LRESULT CALLBACK self_removing(int code,WPARAM wp,LPARAM lp) {
    CWPSTRUCT *c=(CWPSTRUCT*)lp;
    if(code==HC_ACTION && c->message==WM_APP+3) { self_calls++; if(!UnhookWindowsHookEx(self_hook)) good=0; }
    return CallNextHookEx(NULL,code,wp,lp);
}
static LRESULT CALLBACK filter_hook(int code,WPARAM wp,LPARAM lp) {
    MSG *m=(MSG*)lp; if(code==MSGF_DIALOGBOX && m->message==WM_APP+4) saw_filter++;
    return CallNextHookEx(NULL,code,wp,lp);
}
static INT_PTR CALLBACK dialog_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp; if(msg==WM_INITDIALOG) { PostMessageW(h,WM_APP+4,0,0); return TRUE; }
    if(msg==WM_APP+4) { EndDialog(h,23); return TRUE; } return FALSE;
}
static LRESULT CALLBACK cbt_hook(int code,WPARAM wp,LPARAM lp) {
    if(code==HCBT_CREATEWND) { CBT_CREATEWNDW *c=(CBT_CREATEWNDW*)lp; if(!wp || !c || !c->lpcs) good=0; saw_create++; if(veto) return 1; }
    return CallNextHookEx(NULL,code,wp,lp);
}
static LRESULT CALLBACK key_hook(int code,WPARAM wp,LPARAM lp) {
    if(code==HC_ACTION && wp=='X') saw_key++;
    return CallNextHookEx(NULL,code,wp,lp);
}
static LRESULT CALLBACK ret_hook(int code,WPARAM wp,LPARAM lp) {
    CWPRETSTRUCT *c=(CWPRETSTRUCT*)lp;
    if(code==HC_ACTION && c->message==WM_APP+1) { if(c->lResult!=42 || !wp) good=0; returned++; }
    return CallNextHookEx(NULL,code,wp,lp);
}
static LRESULT CALLBACK get_hook(int code,WPARAM wp,LPARAM lp) {
    MSG *m=(MSG*)lp;
    if(code==HC_ACTION && m->message==WM_APP+2) { if(wp!=PM_REMOVE) good=0; m->wParam=123; retrieved++; }
    return CallNextHookEx(NULL,code,wp,lp);
}
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
    HHOOK after=SetWindowsHookExW(WH_CALLWNDPROCRET,ret_hook,NULL,GetCurrentThreadId());
    HHOOK get=SetWindowsHookExW(WH_GETMESSAGE,get_hook,NULL,GetCurrentThreadId());
    if(!first || !second || !after || !get || SendMessageW(h,WM_APP+1,0,0)!=42 || !good || sequence!=2 || returned!=1) ExitProcess(3);
    MSG msg; if(!PostMessageW(h,WM_APP+2,0,0) || !PeekMessageW(&msg,h,WM_APP+2,WM_APP+2,PM_REMOVE) || msg.wParam!=123 || retrieved!=1 || !good) ExitProcess(8);
    if(!UnhookWindowsHookEx(after) || !UnhookWindowsHookEx(get)) ExitProcess(9);
    self_hook=SetWindowsHookExW(WH_CALLWNDPROC,self_removing,NULL,GetCurrentThreadId());
    if(!self_hook) ExitProcess(15); SendMessageW(h,WM_APP+3,0,0); SendMessageW(h,WM_APP+3,0,0);
    if(self_calls!=1 || !good) ExitProcess(16);
    HHOOK cbt=SetWindowsHookExW(WH_CBT,cbt_hook,NULL,GetCurrentThreadId());
    HHOOK key=SetWindowsHookExW(WH_KEYBOARD,key_hook,NULL,GetCurrentThreadId());
    veto=1; HWND blocked=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,1,1,NULL,NULL,c.hInstance,NULL);
    if(!cbt || !key || blocked || saw_create!=1 || !good) ExitProcess(10);
    if(!PostMessageW(h,WM_KEYDOWN,'X',0) || !PeekMessageW(&msg,h,WM_KEYDOWN,WM_KEYDOWN,PM_REMOVE) || saw_key!=1) ExitProcess(11);
    if(!UnhookWindowsHookEx(cbt) || !UnhookWindowsHookEx(key)) ExitProcess(12);
    HHOOK filter=SetWindowsHookExW(WH_MSGFILTER,filter_hook,NULL,GetCurrentThreadId());
    if(!filter || DialogBoxParamW(c.hInstance,MAKEINTRESOURCEW(201),h,dialog_proc,0)!=23 || saw_filter!=1) ExitProcess(13);
    if(!UnhookWindowsHookEx(filter)) ExitProcess(14);
    if(!UnhookWindowsHookEx(second) || !UnhookWindowsHookEx(first)) ExitProcess(4);
    SetLastError(0); if(UnhookWindowsHookEx(first) || GetLastError()!=1404) ExitProcess(5);
    SetLastError(0); if(SetWindowsHookExW(99,hook1,NULL,GetCurrentThreadId()) || GetLastError()!=87) ExitProcess(6);
    if(CallNextHookEx(NULL,0,0,0)!=0) ExitProcess(7);
    DWORD n; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"hooks ok\n",9,&n,NULL); ExitProcess(0);
}
