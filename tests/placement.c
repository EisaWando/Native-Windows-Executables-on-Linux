#include <windows.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(NULL);c.lpszClassName=L"PlacementTest";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND h=CreateWindowExW(0,c.lpszClassName,L"",0,10,20,100,80,NULL,NULL,c.hInstance,NULL);if(!h)ExitProcess(2);
    WINDOWPLACEMENT p={0};p.length=sizeof(p);if(!GetWindowPlacement(h,&p) || p.rcNormalPosition.left!=10 || p.rcNormalPosition.bottom!=100)ExitProcess(3);
    p.showCmd=SW_HIDE;p.flags=WPF_SETMINPOSITION;p.ptMinPosition.x=7;p.ptMinPosition.y=8;
    p.rcNormalPosition=(RECT){20,30,220,180};if(!SetWindowPlacement(h,&p) || IsWindowVisible(h))ExitProcess(4);
    p.length=sizeof(p);if(!GetWindowPlacement(h,&p) || p.flags || p.ptMinPosition.x!=7 || p.rcNormalPosition.right!=220)ExitProcess(5);
    p.length=0;if(GetWindowPlacement(h,&p) || SetWindowPlacement(h,&p))ExitProcess(6);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"placement ok\n",13,&n,NULL);ExitProcess(0);
}
