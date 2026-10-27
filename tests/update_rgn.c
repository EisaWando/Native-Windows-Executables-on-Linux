#define UNICODE
#include <windows.h>
static void fail(unsigned line)
{
    char b[64]="FAIL update_rgn line "; unsigned n=21; char r[12]; unsigned i=0;
    do { r[i++]='0'+line%10; line/=10; } while(line);
    while(i) b[n++]=r[--i]; b[n++]='\n'; DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&written,0); ExitProcess(1);
}
#define C(x) do { if(!(x)) fail(__LINE__); } while(0)
static int paints, erases, handled=1, reinvalidate;
static RECT painted; static BOOL needs_erase;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if(m==WM_ERASEBKGND) { erases++; C(wp!=0); return handled; }
    if(m==WM_PAINT) {
        PAINTSTRUCT p; paints++; C(BeginPaint(w,&p)!=0);
        painted=p.rcPaint; needs_erase=p.fErase;
        C(!GetUpdateRect(w,0,FALSE));
        if(reinvalidate) { RECT r={70,70,80,80}; C(InvalidateRect(w,&r,FALSE)); }
        C(EndPaint(w,&p)); return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
static void rect(RECT r,int l,int t,int right,int b)
{ C(r.left==l && r.top==t && r.right==right && r.bottom==b); }
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(0);
    WNDCLASSEXW c={0}; c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=h;c.lpszClassName=L"UpdateRegion";
    C(RegisterClassExW(&c));
    HWND w=CreateWindowExW(0,c.lpszClassName,L"",WS_CLIPCHILDREN,0,0,100,100,0,0,h,0);
    HWND child=CreateWindowExW(0,c.lpszClassName,L"",WS_CHILD,10,10,30,30,w,0,h,0);
    C(w && child);
    HRGN out=CreateRectRgn(0,0,1,1), a=CreateRectRgn(10,10,20,20), b=CreateRectRgn(30,30,40,40);
    C(out && a && b);
    RECT r={1,2,3,4};
    C(GetUpdateRgn(w,out,FALSE)==NULLREGION); C(!GetUpdateRect(w,&r,FALSE)); rect(r,0,0,0,0);
    C(UpdateWindow(w) && paints==0);
    C(InvalidateRgn(w,a,FALSE)); C(GetUpdateRgn(w,out,FALSE)==SIMPLEREGION);
    C(GetRgnBox(out,&r)==SIMPLEREGION);rect(r,10,10,20,20);
    C(InvalidateRgn(w,b,TRUE)); C(GetUpdateRgn(w,out,FALSE)==COMPLEXREGION);
    C(PtInRegion(out,11,11) && PtInRegion(out,31,31) && !PtInRegion(out,25,25));
    C(GetUpdateRect(w,&r,TRUE));rect(r,10,10,40,40); C(erases==1);
    C(GetUpdateRgn(w,out,TRUE)==COMPLEXREGION && erases==1); /* no validation or duplicate erase */
    C(ValidateRgn(w,a)); C(GetUpdateRgn(w,out,FALSE)==SIMPLEREGION);
    RECT partial={30,30,35,40}; C(ValidateRect(w,&partial));
    C(GetUpdateRect(w,&r,FALSE));rect(r,35,30,40,40);
    C(ValidateRect(w,0)); C(GetUpdateRgn(w,out,FALSE)==NULLREGION);
    RECT one={10,10,20,20}, two={30,30,40,40};
    C(InvalidateRect(w,&one,TRUE)); C(InvalidateRect(w,&two,FALSE));
    C(UpdateWindow(w)); C(paints==1 && erases==2 && !needs_erase);rect(painted,10,10,40,40);
    C(UpdateWindow(w) && paints==1);
    handled=0; C(InvalidateRect(w,&one,TRUE));C(UpdateWindow(w));C(needs_erase && erases==3);handled=1;
    reinvalidate=1; C(InvalidateRect(w,&one,FALSE));C(UpdateWindow(w));
    C(GetUpdateRect(w,&r,FALSE));rect(r,70,70,80,80);reinvalidate=0;C(ValidateRgn(w,0));
    RECT big={-20,-20,120,120}; C(InvalidateRect(w,&big,FALSE));
    C(GetUpdateRect(w,&r,FALSE));rect(r,0,0,100,100);
    C(!GetUpdateRect(child,0,FALSE)); C(ValidateRect(w,0));
    C(RedrawWindow(w,&one,0,RDW_INVALIDATE|RDW_ALLCHILDREN));
    C(GetUpdateRect(child,&r,FALSE));rect(r,0,0,10,10);
    C(RedrawWindow(w,0,0,RDW_VALIDATE|RDW_ALLCHILDREN));
    C(!GetUpdateRect(w,0,FALSE) && !GetUpdateRect(child,0,FALSE));
    C(RedrawWindow(w,0,0,RDW_INVALIDATE|RDW_NOCHILDREN)); C(!GetUpdateRect(child,0,FALSE));
    C(ValidateRect(w,0));
    int before=erases;
    C(RedrawWindow(w,&one,0,RDW_INVALIDATE|RDW_ERASE|RDW_ERASENOW|RDW_NOCHILDREN));
    C(erases==before+1 && GetUpdateRect(w,0,FALSE));
    before=erases;
    C(RedrawWindow(w,&one,0,RDW_INVALIDATE|RDW_ERASE|RDW_NOERASE|RDW_UPDATENOW|RDW_NOCHILDREN));
    C(erases==before && !GetUpdateRect(w,0,FALSE));
    C(CombineRgn(out,a,b,RGN_OR)==COMPLEXREGION);
    C(InvalidateRgn(w,out,FALSE));
    HDC dc=GetDC(w); C(dc); C(ExcludeUpdateRgn(dc,w)==COMPLEXREGION);
    C(!PtVisible(dc,11,11) && !PtVisible(dc,31,31) && PtVisible(dc,25,25));
    int saved=SaveDC(dc);C(saved);C(SelectClipRgn(dc,a)==SIMPLEREGION);
    C(RestoreDC(dc,saved));C(!PtVisible(dc,11,11) && PtVisible(dc,25,25));
    C(ReleaseDC(w,dc));
    PAINTSTRUCT ps; C(BeginPaint(w,&ps));C(PtVisible(ps.hdc,11,11) && !PtVisible(ps.hdc,25,25));C(EndPaint(w,&ps));
    C(BeginPaint(w,&ps));rect(ps.rcPaint,0,0,0,0);C(!ps.fErase && !PtVisible(ps.hdc,0,0));C(EndPaint(w,&ps));
    HDC mem=CreateCompatibleDC(0); HBITMAP bm=CreateCompatibleBitmap(mem,100,100);C(mem && bm);
    HGDIOBJ old=SelectObject(mem,bm);C(old);C(PatBlt(mem,0,0,100,100,BLACKNESS));
    C(InvalidateRgn(w,a,FALSE));C(ExcludeUpdateRgn(mem,w)==COMPLEXREGION);
    C(PatBlt(mem,0,0,100,100,WHITENESS));C(GetPixel(mem,11,11)==RGB(0,0,0) && GetPixel(mem,25,25)==RGB(255,255,255));
    C(SelectObject(mem,old));C(DeleteObject(bm));C(DeleteDC(mem));
    C(GetUpdateRgn((HWND)1,out,FALSE)==ERROR && GetLastError()==1400);
    C(GetUpdateRgn(w,(HRGN)1,FALSE)==ERROR && GetLastError()==6);
    C(!InvalidateRgn(w,(HRGN)1,TRUE) && GetLastError()==6);
    C(!ValidateRgn(w,(HRGN)1) && GetLastError()==6);
    C(!ValidateRect((HWND)1,0) && GetLastError()==1400);
    C(ExcludeUpdateRgn((HDC)1,w)==ERROR && GetLastError()==6);
    C(!RedrawWindow(w,0,(HRGN)1,RDW_INVALIDATE) && GetLastError()==6);
    C(DestroyWindow(w)); C(!GetUpdateRect(w,0,FALSE) && GetLastError()==1400);
    C(DeleteObject(a) && DeleteObject(b) && DeleteObject(out));
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"update rgn ok\n",14,&written,0);ExitProcess(0);
}
