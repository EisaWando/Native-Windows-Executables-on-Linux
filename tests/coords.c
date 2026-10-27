#define UNICODE
#include <windows.h>
static void failed(unsigned line)
{
    char b[64]="FAIL coords line "; unsigned n=17; char r[12]; unsigned i=0;
    do { r[i++]='0'+line%10; line/=10; } while(line);
    while(i) b[n++]=r[--i]; b[n++]='\n'; DWORD out;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&out,0); ExitProcess(1);
}
#define C(x) do { if(!(x)) failed(__LINE__); } while(0)
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{ if (m==WM_NCHITTEST && GetWindowLongPtrW(h,GWLP_USERDATA)==1) return HTTRANSPARENT; return DefWindowProcW(h,m,w,l); }
static void pt(POINT p,int x,int y) { C(p.x==x && p.y==y); }
static void rect(RECT r,int l,int t,int right,int b) { C(r.left==l && r.top==t && r.right==right && r.bottom==b); }
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(NULL); WNDCLASSEXW c={0};c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=h;c.lpszClassName=L"Coords"; C(RegisterClassExW(&c));
    HMENU menu=CreateMenu(); C(menu);
    HWND w=CreateWindowExW(0,c.lpszClassName,L"",0,100,200,300,200,NULL,menu,h,NULL); C(w);
    HWND a=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD,10,20,80,60,w,(HMENU)1,h,0); C(a);
    HWND b=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD,5,7,30,20,a,(HMENU)2,h,0); C(b);
    RECT r; POINT p={0,0};
    C(GetWindowRect(w,&r));rect(r,100,200,400,400);
    C(GetClientRect(w,&r));rect(r,0,0,300,176);
    C(GetWindowRect(a,&r));rect(r,110,244,190,304);
    C(GetWindowRect(b,&r));rect(r,115,251,145,271);
    C(ClientToScreen(b,&p));pt(p,115,251); C(ScreenToClient(b,&p));pt(p,0,0);
    p=(POINT){-7,9};C(ClientToScreen(w,&p));pt(p,93,233);C(ScreenToClient(w,&p));pt(p,-7,9);
    POINT pair[2]={{0,0},{5,7}};
    int delta=MapWindowPoints(b,w,pair,2);C((short)LOWORD(delta)==15 && (short)HIWORD(delta)==27);
    pt(pair[0],15,27);pt(pair[1],20,34);
    delta=MapWindowPoints(w,b,pair,2);C((short)LOWORD(delta)==-15 && (short)HIWORD(delta)==-27);
    pt(pair[0],0,0);pt(pair[1],5,7);
    r=(RECT){0,0,30,20}; C(MapWindowPoints(b,NULL,(POINT *)&r,2));rect(r,115,251,145,271);
    r=(RECT){0,0,100,100};C(AdjustWindowRectEx(&r,0,TRUE,0));rect(r,0,-24,100,100);
    SetLastError(0);C(!ScreenToClient((HWND)1,&p) && GetLastError()==1400);
    SetLastError(0);C(!ClientToScreen(w,NULL) && GetLastError()==87);
    SetLastError(0);C(!MapWindowPoints(w,(HWND)1,&p,1) && GetLastError()==1400);
    SetLastError(0);C(!MapWindowPoints(w,NULL,NULL,1) && GetLastError()==87);
    SetLastError(0);C(MapWindowPoints(w,w,&p,1)==0 && GetLastError()==0);
    C(SetCursorPos(-21,345)); C(GetCursorPos(&p));pt(p,-21,345);
    C(PostMessageW(w,WM_APP+7,0,0)); C(SetCursorPos(80,90));
    MSG msg; C(PeekMessageW(&msg,w,WM_APP+7,WM_APP+7,PM_REMOVE));pt(msg.pt,-21,345);
    C((short)LOWORD(GetMessagePos())==-21 && (short)HIWORD(GetMessagePos())==345);
    C((DWORD)GetMessageTime()==msg.time);
    POINT local={12,22}; C(ChildWindowFromPoint(w,local)==a);
    C(ChildWindowFromPointEx(w,local,CWP_SKIPINVISIBLE)==w);
    SetWindowLongPtrW(w,GWL_STYLE,WS_VISIBLE);SetWindowLongPtrW(a,GWL_STYLE,WS_CHILD|WS_VISIBLE);
    SetWindowLongPtrW(b,GWL_STYLE,WS_CHILD|WS_VISIBLE);
    POINT screen={116,252};C(WindowFromPoint(screen)==b);
    C(ChildWindowFromPointEx(w,local,CWP_SKIPINVISIBLE)==a);
    SetWindowLongPtrW(a,GWL_STYLE,WS_CHILD|WS_VISIBLE|WS_DISABLED);
    C(ChildWindowFromPointEx(w,local,CWP_SKIPDISABLED)==w);
    SetWindowLongPtrW(a,GWL_STYLE,WS_CHILD|WS_VISIBLE);
    SetWindowLongPtrW(a,GWL_EXSTYLE,WS_EX_TRANSPARENT);
    C(ChildWindowFromPointEx(w,local,CWP_SKIPTRANSPARENT)==w);
    C(ChildWindowFromPoint(w,local)==a);
    SetWindowLongPtrW(a,GWL_EXSTYLE,0); SetWindowLongPtrW(a,GWLP_USERDATA,1);
    C(RealChildWindowFromPoint(w,local)==w);
    C(!ChildWindowFromPointEx(w,local,8) && GetLastError()==87);
    C(!GetCursorPos(NULL) && GetLastError()==87);
    C(DestroyWindow(w)); C(!ScreenToClient(w,&p) && GetLastError()==1400);
    DWORD out;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"coords ok\n",10,&out,0);ExitProcess(0);
}
