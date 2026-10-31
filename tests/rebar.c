#include <windows.h>
#include <commctrl.h>
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void band(REBARBANDINFOW *b,HWND child,UINT cy,UINT id)
{
    b->cbSize=sizeof(*b);
    b->fMask=RBBIM_CHILD|RBBIM_CHILDSIZE|RBBIM_ID;
    b->hwndChild=child;
    b->cyMinChild=cy;
    b->wID=id;
}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.hInstance=GetModuleHandleW(NULL);c.lpfnWndProc=proc;c.lpszClassName=L"RebarParent";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND parent=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,400,300,NULL,NULL,c.hInstance,NULL);
    if(!parent)ExitProcess(2);
    HWND bar=CreateWindowExW(0,REBARCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE,0,0,400,0,parent,NULL,c.hInstance,NULL);
    if(!bar)ExitProcess(3);
    HWND a=CreateWindowExW(0,L"Button",L"a",WS_CHILD|WS_VISIBLE,0,0,100,24,bar,NULL,c.hInstance,NULL);
    HWND b=CreateWindowExW(0,L"Button",L"b",WS_CHILD|WS_VISIBLE,0,0,100,30,bar,NULL,c.hInstance,NULL);
    if(!a || !b)ExitProcess(4);
    REBARBANDINFOW info={0};
    band(&info,a,24,7);
    if(!SendMessageW(bar,RB_INSERTBANDW,(WPARAM)-1,(LPARAM)&info))ExitProcess(5);
    if(SendMessageW(bar,RB_GETBANDCOUNT,0,0)!=1)ExitProcess(6);
    if(SendMessageW(bar,RB_GETBARHEIGHT,0,0)!=24)ExitProcess(7);
    RECT r;
    if(!SendMessageW(bar,RB_GETRECT,0,(LPARAM)&r) || r.left!=0 || r.top!=0 || r.right!=400 || r.bottom!=24)ExitProcess(8);
    if(SendMessageW(bar,RB_IDTOINDEX,7,0)!=0 || SendMessageW(bar,RB_IDTOINDEX,99,0)!=-1)ExitProcess(9);
    /* band layout moves the child into the band */
    if(!GetWindowRect(a,&r) || r.left!=0 || r.top!=0 || r.right-r.left!=400 || r.bottom-r.top!=24)ExitProcess(10);
    band(&info,b,20,8);
    if(!SendMessageW(bar,RB_INSERTBANDW,1,(LPARAM)&info))ExitProcess(11);
    if(SendMessageW(bar,RB_GETBANDCOUNT,0,0)!=2)ExitProcess(12);
    if(SendMessageW(bar,RB_GETBARHEIGHT,0,0)!=54)ExitProcess(13);
    if(!SendMessageW(bar,RB_GETRECT,1,(LPARAM)&r) || r.top!=24 || r.bottom!=54)ExitProcess(14);
    if(!GetWindowRect(b,&r) || r.top!=24 || r.bottom-r.top!=30)ExitProcess(15);
    info=(REBARBANDINFOW){0};
    info.cbSize=sizeof(info);
    info.fMask=RBBIM_CHILDSIZE;
    info.cyMinChild=40;
    if(!SendMessageW(bar,RB_SETBANDINFOW,0,(LPARAM)&info))ExitProcess(16);
    if(SendMessageW(bar,RB_GETBARHEIGHT,0,0)!=70)ExitProcess(17);
    info=(REBARBANDINFOW){0};
    info.cbSize=sizeof(info);
    info.fMask=RBBIM_CHILDSIZE|RBBIM_ID;
    if(!SendMessageW(bar,RB_GETBANDINFOW,1,(LPARAM)&info) || info.cyMinChild!=20 || info.wID!=8)ExitProcess(18);
    if(!SendMessageW(bar,RB_DELETEBAND,0,0))ExitProcess(19);
    if(SendMessageW(bar,RB_GETBANDCOUNT,0,0)!=1 || SendMessageW(bar,RB_GETBARHEIGHT,0,0)!=30)ExitProcess(20);
    if(!SendMessageW(bar,RB_GETRECT,0,(LPARAM)&r) || r.top!=0 || r.bottom!=30)ExitProcess(21);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"rebar ok\n",9,&n,NULL);ExitProcess(0);
}
