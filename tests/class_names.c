#include <windows.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
void mainCRTStartup(void)
{
    WNDCLASSW wc={0};wc.hInstance=GetModuleHandleW(NULL);wc.lpfnWndProc=proc;wc.lpszClassName=L"ClassTest";
    if(!RegisterClassW(&wc))ExitProcess(1);
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,NULL,NULL,wc.hInstance,NULL);
    WCHAR wide[32];char ansi[32];if(!h)ExitProcess(2);
    if(GetClassNameW(h,wide,32)!=9 || lstrcmpW(wide,L"ClassTest"))ExitProcess(3);
    if(GetClassNameA(h,ansi,5)!=4 || lstrcmpA(ansi,"Clas"))ExitProcess(4);
    SetLastError(0);if(GetClassNameW((HWND)1,wide,32) || GetLastError()!=1400)ExitProcess(5);
    if(GetClassNameW(h,NULL,32) || GetLastError()!=87)ExitProcess(6);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"class names ok\n",15,&n,NULL);ExitProcess(0);
}
