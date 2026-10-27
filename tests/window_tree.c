#define UNICODE
#include <windows.h>
static int changing,changed,moves,sizes,create_notify,destroy_notify;
static HWND seen[32]; static int nseen,stop_after;
static void fail(unsigned line)
{
    char b[64]="FAIL window_tree line ";unsigned n=22;char rev[12];unsigned k=0;
    do {rev[k++]='0'+line%10;line/=10;} while(line);
    while(k)b[n++]=rev[--k];b[n++]='\n';DWORD out;WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&out,0);ExitProcess(1);
}
#define C(x) do { if(!(x)) fail(__LINE__); } while(0)
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_WINDOWPOSCHANGING) changing++;
    if(m==WM_WINDOWPOSCHANGED) changed++;
    if(m==WM_MOVE) moves++;
    if(m==WM_SIZE) sizes++;
    if(m==WM_PARENTNOTIFY) { if(LOWORD(w)==WM_CREATE)create_notify++;if(LOWORD(w)==WM_DESTROY)destroy_notify++; }
    return DefWindowProcW(h,m,w,l);
}
static BOOL CALLBACK visit(HWND h,LPARAM p) { (void)p; seen[nseen++]=h; return !stop_after || nseen<stop_after; }
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(0);WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.lpfnWndProc=proc;cls.hInstance=h;cls.lpszClassName=L"TreeTest";C(RegisterClassExW(&cls));
    HWND root=CreateWindowExW(0,cls.lpszClassName,L"root",0,100,200,300,200,0,0,h,0);
    HWND other=CreateWindowExW(0,cls.lpszClassName,L"other",0,500,200,200,200,0,0,h,0);
    HWND a=CreateWindowExW(0,cls.lpszClassName,L"first",WS_CHILD,10,10,80,80,root,(HMENU)1,h,0);
    HWND b=CreateWindowExW(0,cls.lpszClassName,L"second",WS_CHILD,10,10,80,80,root,(HMENU)2,h,0);
    HWND sub=CreateWindowExW(0,cls.lpszClassName,L"nested",WS_CHILD,5,5,20,20,a,(HMENU)3,h,0);
    C(root&&other&&a&&b&&sub && create_notify>=4);
    C(GetParent(sub)==a && GetAncestor(sub,GA_PARENT)==a && GetAncestor(sub,GA_ROOT)==root && IsChild(root,sub));
    C(GetTopWindow(root)==b && GetWindow(b,GW_HWNDNEXT)==a && GetWindow(a,GW_HWNDPREV)==b);
    C(GetWindow(a,GW_HWNDFIRST)==b && GetWindow(b,GW_HWNDLAST)==a && GetWindow(a,GW_CHILD)==sub);
    C(FindWindowW(cls.lpszClassName,L"root")==root && FindWindowA("treetest","OTHER")==other);
    C(FindWindowExW(root,NULL,cls.lpszClassName,L"second")==b);
    C(FindWindowExA(root,b,"TreeTest","first")==a);
    nseen=0;stop_after=0;C(EnumChildWindows(root,visit,0));C(nseen==3 && seen[0]==b && seen[1]==a && seen[2]==sub);
    nseen=0;stop_after=2;C(!EnumChildWindows(root,visit,0) && nseen==2); stop_after=0;
    nseen=0;C(EnumThreadWindows(GetCurrentThreadId(),visit,0) && nseen==2);
    nseen=0;C(EnumWindows(visit,0) && nseen==2);
    RECT r;C(SetParent(sub,b)==a);C(GetParent(sub)==b && IsChild(b,sub));
    C(GetWindowRect(sub,&r) && r.left==115 && r.top==215 && r.right==135 && r.bottom==235);
    C(!SetParent(b,sub) && GetLastError()==87 && GetParent(b)==root);
    C(!SetParent((HWND)1,root) && GetLastError()==1400);
    C(!SetParent(b,(HWND)1) && GetLastError()==1400);
    C(SetParent(sub,NULL)==b && !GetParent(sub));
    SetLastError(0);C(!SetParent(sub,a) && GetLastError()==0 && GetParent(sub)==a);
    C((GetWindowLongPtrW(sub,GWL_STYLE)&(WS_CHILD|WS_POPUP))==WS_CHILD);
    SetWindowLongPtrW(root,GWL_STYLE,WS_VISIBLE);SetWindowLongPtrW(a,GWL_STYLE,WS_CHILD|WS_VISIBLE);SetWindowLongPtrW(b,GWL_STYLE,WS_CHILD|WS_VISIBLE);
    POINT p={111,211};C(WindowFromPoint(p)==b);
    int c0=changing,d0=changed;C(SetWindowPos(a,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    C(changing==c0+1 && changed==d0+1 && GetTopWindow(root)==a && WindowFromPoint(p)==a);
    C(SetWindowPos(a,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));C(GetTopWindow(root)==b);
    C(SetWindowPos(a,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));C(GetTopWindow(root)==a && (GetWindowLongPtrW(a,GWL_EXSTYLE)&WS_EX_TOPMOST));
    C(SetWindowPos(a,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));C(!(GetWindowLongPtrW(a,GWL_EXSTYLE)&WS_EX_TOPMOST));
    C(SetWindowPos(a,b,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));C(GetWindow(b,GW_HWNDNEXT)==a);
    HDWP defer=BeginDeferWindowPos(1);C(defer);defer=DeferWindowPos(defer,a,0,30,40,70,60,SWP_NOZORDER|SWP_NOACTIVATE);C(defer);
    defer=DeferWindowPos(defer,b,0,60,50,50,45,SWP_NOZORDER|SWP_NOACTIVATE);C(defer);
    C(GetWindowRect(a,&r)&&r.left==110&&r.top==210);int m0=moves,s0=sizes;
    C(EndDeferWindowPos(defer));C(moves==m0+2 && sizes==s0+2);
    C(GetWindowRect(a,&r)&&r.left==130&&r.top==240&&r.right==200&&r.bottom==300);
    C(GetWindowRect(b,&r)&&r.left==160&&r.top==250&&r.right==210&&r.bottom==295);
    C(!GetWindow((HWND)1,GW_CHILD) && GetLastError()==1400);
    C(!GetAncestor((HWND)1,GA_ROOT) && GetLastError()==1400);
    C(!SetWindowPos(a,(HWND)0x200,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE) && GetLastError()==1400);
    C(DestroyWindow(root));C(destroy_notify>=3);C(DestroyWindow(other));
    DWORD out;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"window tree ok\n",15,&out,0);ExitProcess(0);
}
