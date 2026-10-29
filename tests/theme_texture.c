#include <windows.h>
#include <uxtheme.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){ return DefWindowProcW(h,m,w,l); }
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(NULL);c.lpszClassName=L"TextureTest";
    if(!RegisterClassW(&c)) ExitProcess(1);
    HWND h=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,0,0,NULL,NULL,c.hInstance,NULL);
    if(!h || IsThemeDialogTextureEnabled(h)) ExitProcess(2);
    if(EnableThemeDialogTexture(h,ETDT_ENABLETAB)!=S_OK || !IsThemeDialogTextureEnabled(h)) ExitProcess(3);
    if(EnableThemeDialogTexture(h,ETDT_DISABLE)!=S_OK || IsThemeDialogTextureEnabled(h)) ExitProcess(4);
    if(EnableThemeDialogTexture(h,0x80000000)!=E_INVALIDARG || SUCCEEDED(EnableThemeDialogTexture((HWND)1,ETDT_ENABLE))) ExitProcess(5);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"theme texture ok\n",17,&n,NULL);ExitProcess(0);
}
