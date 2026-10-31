#include <windows.h>
#include <commctrl.h>
static int command;
static HWND command_hwnd;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_COMMAND){command=LOWORD(w);command_hwnd=(HWND)l;}
    return DefWindowProcW(h,m,w,l);
}
void mainCRTStartup(void)
{
    WNDCLASSW c={0};c.hInstance=GetModuleHandleW(NULL);c.lpfnWndProc=proc;c.lpszClassName=L"ToolbarParent";
    if(!RegisterClassW(&c))ExitProcess(1);
    HWND parent=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,400,300,NULL,NULL,c.hInstance,NULL);
    if(!parent)ExitProcess(2);
    HWND tb=CreateWindowExW(0,TOOLBARCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE,0,0,0,0,parent,NULL,c.hInstance,NULL);
    if(!tb)ExitProcess(3);
    SendMessageW(tb,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
    if(!SendMessageW(tb,TB_SETBUTTONSIZE,0,MAKELONG(20,22)))ExitProcess(4);
    SendMessageW(tb,TB_SETBITMAPSIZE,MAKELONG(16,16),0);
    TBBUTTON b[3]={0};
    b[0].idCommand=101;b[0].fsState=TBSTATE_ENABLED;
    b[1].idCommand=102;b[1].fsState=TBSTATE_ENABLED|TBSTATE_CHECKED;
    b[2].idCommand=103;b[2].fsState=TBSTATE_ENABLED;
    if(!SendMessageW(tb,TB_ADDBUTTONS,3,(LPARAM)b))ExitProcess(5);
    if(SendMessageW(tb,TB_BUTTONCOUNT,0,0)!=3)ExitProcess(6);
    TBBUTTON got={0};
    if(!SendMessageW(tb,TB_GETBUTTON,1,(LPARAM)&got) || got.idCommand!=102)ExitProcess(7);
    if(SendMessageW(tb,TB_COMMANDTOINDEX,102,0)!=1)ExitProcess(8);
    if(SendMessageW(tb,TB_GETSTATE,1,0)!=(TBSTATE_ENABLED|TBSTATE_CHECKED))ExitProcess(9);
    if(SendMessageW(tb,TB_CHECKBUTTON,0,TRUE)!=1)ExitProcess(10);
    if(!(SendMessageW(tb,TB_GETSTATE,0,0)&TBSTATE_CHECKED))ExitProcess(10);
    if(!SendMessageW(tb,TB_ENABLEBUTTON,2,FALSE))ExitProcess(11);
    if(SendMessageW(tb,TB_GETSTATE,2,0)&TBSTATE_ENABLED)ExitProcess(11);
    if(!SendMessageW(tb,TB_HIDEBUTTON,2,TRUE))ExitProcess(12);
    if(!(SendMessageW(tb,TB_GETSTATE,2,0)&TBSTATE_HIDDEN))ExitProcess(12);
    RECT r;
    if(!SendMessageW(tb,TB_GETITEMRECT,1,(LPARAM)&r) || r.left!=20 || r.right!=40 || r.top!=0 || r.bottom!=22)ExitProcess(13);
    if(LOWORD(SendMessageW(tb,TB_GETBUTTONSIZE,0,0))!=20 || HIWORD(SendMessageW(tb,TB_GETBUTTONSIZE,0,0))!=22)ExitProcess(14);
    SIZE sz;
    if(!SendMessageW(tb,TB_GETMAXSIZE,0,(LPARAM)&sz) || sz.cx!=60 || sz.cy!=22)ExitProcess(15);
    SendMessageW(tb,TB_AUTOSIZE,0,0);
    if(!GetWindowRect(tb,&r) || r.right-r.left!=60 || r.bottom-r.top!=22)ExitProcess(16);
    TBBUTTON ins={0};ins.idCommand=100;ins.fsState=TBSTATE_ENABLED;
    if(!SendMessageW(tb,TB_INSERTBUTTON,0,(LPARAM)&ins))ExitProcess(17);
    if(SendMessageW(tb,TB_BUTTONCOUNT,0,0)!=4)ExitProcess(17);
    if(!SendMessageW(tb,TB_GETITEMRECT,3,(LPARAM)&r) || r.left!=60)ExitProcess(17);
    if(SendMessageW(tb,TB_COMMANDTOINDEX,102,0)!=2)ExitProcess(17);
    if(!SendMessageW(tb,TB_DELETEBUTTON,0,0) || SendMessageW(tb,TB_BUTTONCOUNT,0,0)!=3)ExitProcess(18);
    SendMessageW(tb,WM_LBUTTONDOWN,0,MAKELPARAM(25,10));
    SendMessageW(tb,WM_LBUTTONUP,0,MAKELPARAM(25,10));
    if(command!=102 || command_hwnd!=tb)ExitProcess(19);
    command=0;
    SendMessageW(tb,WM_LBUTTONDOWN,0,MAKELPARAM(45,10));
    SendMessageW(tb,WM_LBUTTONUP,0,MAKELPARAM(45,10));
    if(command)ExitProcess(20);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"toolbar ok\n",11,&n,NULL);ExitProcess(0);
}
