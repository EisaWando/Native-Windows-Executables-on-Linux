#include <windows.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(NULL);c.lpszClassName=L"LockTest";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND parent=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,100,100,NULL,NULL,c.hInstance,NULL);
    HWND child=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD,0,0,50,50,parent,NULL,c.hInstance,NULL);
    if(!parent || !child || !LockWindowUpdate(parent))ExitProcess(2);
    if(LockWindowUpdate(child))ExitProcess(3);
    HDC dc=GetDC(child);RECT box; if(!dc || GetClipBox(dc,&box)!=NULLREGION)ExitProcess(4);
    RECT draw={2,3,20,25};if(!FillRect(dc,&draw,(HBRUSH)(COLOR_WINDOW+1)))ExitProcess(5);
    if(SetWindowPos(parent,NULL,5,5,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE))ExitProcess(6);
    ReleaseDC(child,dc);if(!LockWindowUpdate(NULL))ExitProcess(7);
    if(!GetUpdateRect(child,&box,FALSE) || box.left!=2 || box.top!=3 || box.right!=20 || box.bottom!=25)ExitProcess(8);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"lock update ok\n",15,&n,NULL);ExitProcess(0);
}
