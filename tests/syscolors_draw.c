#define UNICODE
#include <windows.h>
static void fail(unsigned line)
{
    char b[64]="FAIL syscolors_draw ";unsigned n=20;char rev[12];unsigned k=0;
    do {rev[k++]='0'+line%10;line/=10;} while(line);
    while(k)b[n++]=rev[--k];b[n++]='\n';DWORD out;WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&out,0);ExitProcess(1);
}
#define C(x) do {if(!(x))fail(__LINE__);} while(0)
static unsigned color_messages;
static LRESULT CALLBACK color_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{ if(m==WM_SYSCOLORCHANGE)color_messages++;return DefWindowProcW(h,m,w,l); }
static BOOL CALLBACK state_proc(HDC dc,LPARAM data,WPARAM w,int cx,int cy)
{ (void)data;(void)w;(void)cx;(void)cy; RECT r={0,0,2,2};return FillRect(dc,&r,GetStockObject(BLACK_BRUSH)); }
static BOOL CALLBACK gray_proc(HDC dc,LPARAM data,int count)
{ (void)data;(void)count; RECT r={0,0,2,2};return FillRect(dc,&r,GetStockObject(BLACK_BRUSH)); }
void mainCRTStartup(void)
{
    for(int i=0;i<=30;i++){ HBRUSH b=GetSysColorBrush(i);LOGBRUSH l; C(b && b==GetSysColorBrush(i));
        C(GetObjectW(b,sizeof(l),&l)==sizeof(l) && l.lbColor==GetSysColor(i));C(DeleteObject(b) && b==GetSysColorBrush(i)); }
    HBRUSH stock=GetSysColorBrush(COLOR_3DFACE); C(stock && stock==GetSysColorBrush(COLOR_3DFACE));
    C(DeleteObject(stock) && GetSysColorBrush(COLOR_3DFACE)==stock);
    COLORREF initial=GetSysColor(COLOR_3DFACE), color=RGB(10,20,30); int idx=COLOR_3DFACE;
    HINSTANCE module=GetModuleHandleW(NULL);WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.hInstance=module;cls.lpfnWndProc=color_proc;cls.lpszClassName=L"ColorNotify";
    C(RegisterClassExW(&cls));HWND window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,NULL,NULL,module,NULL);C(window);
    C(SetSysColors(1,&idx,&color) && GetSysColor(idx)==color && color_messages==1);
    LOGBRUSH brush; C(GetObjectW(stock,sizeof(brush),&brush)==sizeof(brush) && brush.lbColor==color);
    HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateCompatibleBitmap(dc,16,16);C(dc&&bm);
    HGDIOBJ old=SelectObject(dc,bm);C(old);
    RECT rect={0,0,16,16};C(FillRect(dc,&rect,(HBRUSH)(COLOR_3DFACE+1)));
    C(GetPixel(dc,8,8)==color);C(FillRect(dc,&rect,stock) && GetPixel(dc,7,7)==color);
    int bad_indexes[2]={COLOR_3DFACE,31};COLORREF bad_colors[2]={RGB(99,88,77),0};
    C(!SetSysColors(2,bad_indexes,bad_colors) && GetLastError()==87 && GetSysColor(COLOR_3DFACE)==color);
    HBRUSH white=CreateSolidBrush(RGB(255,255,255));C(white);
    rect=(RECT){2,2,10,10};C(FrameRect(dc,&rect,white));
    C(GetPixel(dc,2,2)==RGB(255,255,255) && GetPixel(dc,5,5)==color);
    rect=(RECT){4,4,6,6};C(InvertRect(dc,&rect));
    C(GetPixel(dc,4,4)==RGB(245,235,225) && GetPixel(dc,3,3)==color);
    C(InvertRect(dc,&rect) && GetPixel(dc,4,4)==color);
    rect=(RECT){2,2,10,10};COLORREF before=GetPixel(dc,2,2);
    C(DrawFocusRect(dc,&rect) && GetPixel(dc,2,2)!=before);
    C(DrawFocusRect(dc,&rect) && GetPixel(dc,2,2)==before);
    C(SetBkMode(dc,TRANSPARENT)); C(SetTextColor(dc,RGB(0,0,0)) != CLR_INVALID);
    RECT layout={0,0,16,16}; SIZE extent; C(GetTextExtentPoint32W(dc,L"A",1,&extent));
    RECT calc=layout; int height=DrawTextW(dc,L"A",1,&calc,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
    C(height>0 && calc.right==extent.cx && calc.bottom==height);
    C(FillRect(dc,&layout,white)); RECT clipped={3,2,12,15};
    C(DrawTextW(dc,L"A",1,&clipped,DT_SINGLELINE|DT_NOPREFIX)>0);
    C(GetPixel(dc,0,0)==RGB(255,255,255));
    int dark=0;for(int y=2;y<15;y++)for(int x=3;x<12;x++)if(GetPixel(dc,x,y)!=RGB(255,255,255))dark++;
    C(dark>0 && GetPixel(dc,2,2)==RGB(255,255,255));
    RECT edge={1,1,15,15}; C(FillRect(dc,&layout,white));
    C(DrawEdge(dc,&edge,EDGE_RAISED,BF_RECT|BF_ADJUST));
    C(edge.left==3 && edge.top==3 && edge.right==13 && edge.bottom==13);
    C(GetPixel(dc,1,1)==GetSysColor(COLOR_3DLIGHT));
    C(GetPixel(dc,14,14)==GetSysColor(COLOR_3DDKSHADOW));
    RECT control={1,1,15,15}; C(DrawFrameControl(dc,&control,DFC_BUTTON,DFCS_BUTTONCHECK|DFCS_CHECKED));
    C(GetPixel(dc,5,8)==GetSysColor(COLOR_BTNTEXT));
    HICON icon=LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(400),IMAGE_ICON,0,0,0);C(icon);
    HBRUSH backing=CreateSolidBrush(RGB(16,32,48)); C(backing && FillRect(dc,&layout,backing));
    C(DrawIconEx(dc,0,0,icon,2,2,0,NULL,DI_NORMAL));
    C(GetPixel(dc,0,0)==RGB(255,0,0) && GetPixel(dc,1,0)==RGB(8,144,24));
    C(GetPixel(dc,0,1)==RGB(16,32,48) && GetPixel(dc,1,1)==RGB(255,255,255));
    C(DrawIconEx(dc,0,0,icon,2,2,0,NULL,DI_IMAGE)); C(GetPixel(dc,1,0)==RGB(0,255,0));
    C(DrawIconEx(dc,0,0,icon,2,2,0,NULL,DI_MASK)); C(GetPixel(dc,0,0)==RGB(0,0,0));
    C(FillRect(dc,&layout,white));C(DrawIconEx(dc,0,0,icon,2,2,0,backing,DI_NORMAL));
    C(GetPixel(dc,0,1)==RGB(16,32,48) && GetPixel(dc,1,0)==RGB(8,144,24));
    C(DestroyIcon(icon) && DeleteObject(backing));
    RECT calc2={0,0,100,100}; SIZE measured;
    C(GetTextExtentPoint32W(dc,L"&A",2,&measured));
    C(DrawTextW(dc,L"&A",2,&calc2,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX)>0 && calc2.right==measured.cx);
    calc2=(RECT){0,0,100,100};C(GetTextExtentPoint32W(dc,L"A",1,&measured));
    C(DrawTextW(dc,L"&A",2,&calc2,DT_CALCRECT|DT_SINGLELINE)>0 && calc2.right==measured.cx);
    calc2=(RECT){0,0,100,100};C(GetTextExtentPoint32W(dc,L"A       B",9,&measured));
    C(DrawTextA(dc,"A\tB",3,&calc2,DT_CALCRECT|DT_SINGLELINE|DT_EXPANDTABS)>0 && calc2.right==measured.cx);
    DRAWTEXTPARAMS dtp={sizeof(dtp),8,1,1,0};WCHAR run[]=L"long sample text";
    calc2=(RECT){0,0,12,16};C(DrawTextExW(dc,run,-1,&calc2,DT_SINGLELINE|DT_END_ELLIPSIS,&dtp)>0 && dtp.uiLengthDrawn==16);
    calc2=(RECT){0,0,12,16};C(DrawTextW(dc,L"C:\\folder\\file.txt",-1,&calc2,DT_SINGLELINE|DT_PATH_ELLIPSIS)>0);
    calc2=(RECT){0,0,1,200};C(GetTextExtentPoint32W(dc,L"word",4,&measured));
    C(DrawTextW(dc,L"word",4,&calc2,DT_CALCRECT|DT_WORDBREAK)>0 && calc2.right==measured.cx);
    calc2=(RECT){0,0,16,16};C(DrawTextW(dc,L"I",1,&calc2,DT_SINGLELINE|DT_BOTTOM|DT_RIGHT)==16);
    calc2=(RECT){0,0,16,16};C(DrawTextW(dc,L"I",1,&calc2,DT_SINGLELINE|DT_VCENTER|DT_CENTER)>0);
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_CAPTION,DFCS_CAPTIONCLOSE));C(GetPixel(dc,8,8)==GetSysColor(COLOR_BTNTEXT));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_SCROLL,DFCS_SCROLLDOWN));C(GetPixel(dc,8,8)==GetSysColor(COLOR_BTNTEXT));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_MENU,DFCS_MENUCHECK));C(GetPixel(dc,5,8)==GetSysColor(COLOR_BTNTEXT));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_BUTTON,DFCS_BUTTONRADIO|DFCS_CHECKED));C(GetPixel(dc,8,8)==GetSysColor(COLOR_BTNTEXT));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_CAPTION,DFCS_CAPTIONHELP));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_SCROLL,DFCS_SCROLLSIZEGRIP));
    control=(RECT){1,1,15,15};C(DrawFrameControl(dc,&control,DFC_MENU,DFCS_MENUBULLET));
    edge=(RECT){1,1,15,15};C(DrawEdge(dc,&edge,EDGE_SUNKEN,BF_DIAGONAL_ENDTOPRIGHT|BF_MONO));
    DWORD speed=0;C(SystemParametersInfoW(SPI_GETMOUSESPEED,0,&speed,0) && speed==10);
    C(FillRect(dc,&layout,white)); HBRUSH red=CreateSolidBrush(RGB(200,0,0)); C(red);
    C(DrawStateW(dc,red,state_proc,0,0,4,4,2,2,DST_COMPLEX|DSS_MONO));
    C(GetPixel(dc,4,4)==RGB(200,0,0) && GetPixel(dc,6,4)==RGB(255,255,255));
    C(GrayStringW(dc,red,gray_proc,0,1,8,8,2,2));
    C(GetPixel(dc,8,8)==RGB(200,0,0) && GetPixel(dc,9,8)==RGB(255,255,255));
    C(DrawStateW(dc,red,state_proc,0,0,11,11,2,2,DST_COMPLEX));
    C(GetPixel(dc,11,11)==RGB(0,0,0));
    C(DrawStateW(dc,red,state_proc,0,0,2,11,2,2,DST_COMPLEX|DSS_DISABLED));
    C(GetPixel(dc,2,11)==GetSysColor(COLOR_3DSHADOW));
    C(DeleteObject(red));
    C(!FrameRect((HDC)1,&layout,stock) && GetLastError()==6);
    C(!DrawTextW(dc,NULL,1,&layout,0) && GetLastError()==87);
    C(!DrawIconEx(dc,0,0,(HICON)1,1,1,0,NULL,DI_NORMAL) && GetLastError()==6);
    C(!GetSysColorBrush(31) && GetLastError()==87);
    C(!SetSysColors(1,NULL,&color) && GetLastError()==87);
    C(SetSysColors(1,&idx,&initial) && color_messages==2);C(DestroyWindow(window));C(SelectObject(dc,old));C(DeleteObject(bm));C(DeleteObject(white));C(DeleteDC(dc));
    DWORD out;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"syscolors draw ok\n",18,&out,0);ExitProcess(0);
}
