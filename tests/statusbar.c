#include <windows.h>
#include <commctrl.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.hInstance=GetModuleHandleW(NULL);c.lpfnWndProc=proc;c.lpszClassName=L"StatusParent";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND parent=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,400,300,NULL,NULL,c.hInstance,NULL);
    HWND bar=CreateWindowExW(0,STATUSCLASSNAMEW,L"",WS_CHILD,0,0,0,0,parent,NULL,c.hInstance,NULL);if(!bar)ExitProcess(2);
    RECT r;if(!GetWindowRect(bar,&r) || r.right-r.left!=400 || r.bottom-r.top<=0)ExitProcess(3);
    int parts[2]={120,-1},back[2];if(!SendMessageW(bar,SB_SETPARTS,2,(LPARAM)parts) || SendMessageW(bar,SB_GETPARTS,2,(LPARAM)back)!=2 || back[0]!=120 || back[1]!=-1)ExitProcess(4);
    if(!SendMessageW(bar,SB_GETRECT,1,(LPARAM)&r) || r.left!=120 || r.right!=400)ExitProcess(5);
    if(!SendMessageW(bar,SB_SETTEXTW,SBT_POPOUT,(LPARAM)L"ready"))ExitProcess(6);
    WCHAR text[16];LRESULT len=SendMessageW(bar,SB_GETTEXTW,0,(LPARAM)text);
    if(LOWORD(len)!=5 || HIWORD(len)!=SBT_POPOUT || lstrcmpW(text,L"ready"))ExitProcess(7);
    int borders[3];if(!SendMessageW(bar,SB_GETBORDERS,0,(LPARAM)borders))ExitProcess(8);
    SendMessageW(bar,SB_SIMPLE,TRUE,0);if(!SendMessageW(bar,SB_ISSIMPLE,0,0))ExitProcess(9);
    SendMessageW(bar,SB_SETMINHEIGHT,40,0);SendMessageW(bar,WM_SIZE,0,0);
    if(!GetWindowRect(bar,&r) || r.bottom-r.top<44)ExitProcess(10);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"statusbar ok\n",13,&n,NULL);ExitProcess(0);
}
