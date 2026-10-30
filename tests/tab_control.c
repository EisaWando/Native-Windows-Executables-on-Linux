#include <windows.h>
#include <commctrl.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.hInstance=GetModuleHandleW(NULL);c.lpfnWndProc=proc;c.lpszClassName=L"TabParent";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND parent=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,100,100,NULL,NULL,c.hInstance,NULL);
    HWND tab=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD,0,0,100,100,parent,(HMENU)9,c.hInstance,NULL);if(!tab)ExitProcess(2);
    TCITEMW item={0};item.mask=TCIF_TEXT|TCIF_PARAM|TCIF_IMAGE|TCIF_STATE;item.pszText=L"first";item.lParam=0x12345678;item.iImage=3;item.dwState=2;item.dwStateMask=2;
    if(SendMessageW(tab,TCM_INSERTITEMW,0,(LPARAM)&item)!=0)ExitProcess(3);
    WCHAR text[16];TCITEMW got={0};got.mask=item.mask;got.lParam=-1;got.pszText=text;got.cchTextMax=16;got.dwStateMask=2;
    if(!SendMessageW(tab,TCM_GETITEMW,0,(LPARAM)&got) || got.lParam!=0x12345678 || got.iImage!=3 || got.dwState!=2 || lstrcmpW(text,L"first"))ExitProcess(4);
    TCITEMA a={0};a.mask=TCIF_TEXT|TCIF_PARAM;a.pszText="second";a.lParam=42;if(SendMessageW(tab,TCM_SETITEMA,0,(LPARAM)&a)!=1)ExitProcess(5);
    char buf[16];a.pszText=buf;a.cchTextMax=16;a.lParam=-1;if(!SendMessageW(tab,TCM_GETITEMA,0,(LPARAM)&a) || lstrcmpA(buf,"second") || a.lParam!=42)ExitProcess(6);
    if(SendMessageW(tab,TCM_GETITEMCOUNT,0,0)!=1 || !SendMessageW(tab,TCM_DELETEITEM,0,0) || SendMessageW(tab,TCM_GETITEMCOUNT,0,0))ExitProcess(7);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"tab control ok\n",15,&n,NULL);ExitProcess(0);
}
