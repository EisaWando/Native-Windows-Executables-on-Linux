#define UNICODE
#include <windows.h>
static void fail(unsigned line)
{
    char b[64]="FAIL clipboard line "; unsigned n=19; char r[12]; unsigned i=0;
    do { r[i++]='0'+line%10; line/=10; } while(line);
    while(i) b[n++]=r[--i]; b[n++]='\n'; DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&written,0); ExitProcess(1);
}
#define C(x) do { if(!(x)) fail(__LINE__); } while(0)
static unsigned destroys, renders, render_format;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if(m==WM_DESTROYCLIPBOARD) { destroys++; return 0; }
    if(m==WM_RENDERFORMAT) {                       /* delayed rendering on demand */
        renders++; render_format=(unsigned)wp;
        HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,8); C(h);
        lstrcpyA(GlobalLock(h),"delayed"); C(!GlobalUnlock(h) && GetLastError()==0);
        C(SetClipboardData((UINT)wp,h)==h);
        return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(0);
    WNDCLASSEXW c={0}; c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=h;c.lpszClassName=L"ClipboardTest";
    C(RegisterClassExW(&c));
    HWND w1=CreateWindowExW(0,c.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,100,100,0,0,h,0);
    HWND w2=CreateWindowExW(0,c.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,100,100,0,0,h,0);
    C(w1 && w2);

    /* 1. Fresh process-local clipboard, closed. */
    C(!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT));
    C(CountClipboardFormats()==0);
    C(!GetClipboardOwner() && !GetOpenClipboardWindow());
    C(!GetClipboardData(CF_TEXT) && GetLastError()==5);
    C(!SetClipboardData(CF_TEXT,(HANDLE)1) && GetLastError()==5);
    C(!EmptyClipboard() && GetLastError()==5);
    C(!CloseClipboard() && GetLastError()==5);
    C(EnumClipboardFormats(0)==0 && GetLastError()==5);
    UINT probe[2]={CF_TEXT,CF_UNICODETEXT};
    C(GetPriorityClipboardFormat(probe,2)==0);          /* empty clipboard */
    C(GetPriorityClipboardFormat(0,2)==-1 && GetLastError()==87);

    /* 2. Global memory: locks, size, flags, realloc, free. */
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,16);
    C(mem && GlobalSize(mem)==16 && GlobalFlags(mem)==(GMEM_MOVEABLE|GMEM_ZEROINIT));
    char *text=GlobalLock(mem);
    C(text && !text[0] && !text[15] && GlobalHandle(text)==mem);
    lstrcpyA(text,"hello");
    C(GlobalLock(mem)==text);                            /* same pointer */
    C(GlobalUnlock(mem)==1);                             /* one lock left */
    C(!GlobalUnlock(mem) && GetLastError()==0);          /* unlocked now, NO_ERROR */
    C(!GlobalUnlock(mem) && GetLastError()==158);        /* ERROR_NOT_LOCKED */
    HGLOBAL grown=GlobalReAlloc(mem,64,GMEM_MOVEABLE);
    C(grown && GlobalSize(grown)==64 && !lstrcmpA(GlobalLock(grown),"hello"));
    C(!GlobalUnlock(grown) && GetLastError()==0);
    C(!GlobalFree(grown));
    HGLOBAL fixedmem=GlobalAlloc(GMEM_FIXED,8);
    C(fixedmem); lstrcpyA((char*)fixedmem,"fix");
    C(GlobalUnlock(fixedmem)==1);                        /* fixed: no lock count */
    C(!GlobalFree(fixedmem));
    C(!GlobalSize(0) && GetLastError()==6);
    static char notglobal[8];
    C(!GlobalHandle(notglobal) && GetLastError()==6);

    /* 3. Ownership: one window at a time, nested opens by the same window. */
    C(OpenClipboard(w1));
    C(!OpenClipboard(w2) && GetLastError()==5);
    C(GetOpenClipboardWindow()==w1);
    C(OpenClipboard(w1));
    C(EmptyClipboard());
    C(GetClipboardOwner()==w1 && destroys==0);           /* nothing was destroyed yet */
    C(CloseClipboard() && GetOpenClipboardWindow()==w1); /* still open once */
    C(CloseClipboard() && !GetOpenClipboardWindow());
    C(GetClipboardOwner()==w1);                          /* contents keep their owner */

    /* 4. Store CF_UNICODETEXT then CF_TEXT. */
    HGLOBAL u16=GlobalAlloc(GMEM_MOVEABLE,22);
    C(u16); lstrcpyW(GlobalLock(u16),L"clipboard"); C(!GlobalUnlock(u16) && GetLastError()==0);
    unsigned notify=destroys;                            /* owner is w1 since step 3 */
    C(OpenClipboard(w1) && EmptyClipboard());
    C(destroys==notify+1);                               /* previous owner notified */
    unsigned seq=GetClipboardSequenceNumber();
    C(SetClipboardData(CF_UNICODETEXT,u16)==u16);
    C(GetClipboardSequenceNumber()>seq);
    C(CountClipboardFormats()==1);
    C(IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT));
    C(EnumClipboardFormats(0)==CF_UNICODETEXT);
    C(EnumClipboardFormats(CF_UNICODETEXT)==0 && GetLastError()==0);
    C(GetPriorityClipboardFormat(probe,2)==CF_UNICODETEXT);
    C(GetClipboardOwner()==w1);
    HGLOBAL back=GetClipboardData(CF_UNICODETEXT);
    C(back==u16 && !lstrcmpW(GlobalLock(back),L"clipboard"));
    C(!GlobalUnlock(back) && GetLastError()==0);
    HGLOBAL ascii=GlobalAlloc(GMEM_MOVEABLE,8);
    C(ascii); lstrcpyA(GlobalLock(ascii),"plain"); C(!GlobalUnlock(ascii) && GetLastError()==0);
    C(SetClipboardData(CF_TEXT,ascii)==ascii);
    C(CountClipboardFormats()==2);
    C(EnumClipboardFormats(0)==CF_UNICODETEXT && EnumClipboardFormats(CF_UNICODETEXT)==CF_TEXT);
    C(EnumClipboardFormats(CF_TEXT)==0);
    C(GetPriorityClipboardFormat(probe,2)==CF_TEXT);      /* list order decides */
    UINT none[2]={CF_BITMAP,CF_METAFILEPICT};
    C(GetPriorityClipboardFormat(none,2)==-1);            /* not empty, no match */
    C(CloseClipboard());

    /* 5. Contents survive CloseClipboard; reading needs an open clipboard. */
    C(IsClipboardFormatAvailable(CF_TEXT) && IsClipboardFormatAvailable(CF_UNICODETEXT));
    C(CountClipboardFormats()==2);
    C(!GetClipboardData(CF_TEXT) && GetLastError()==5);
    C(EnumClipboardFormats(0)==0 && GetLastError()==5);
    C(!GetOpenClipboardWindow() && GetClipboardOwner()==w1);

    /* 6. EmptyClipboard notifies the previous owner. */
    notify=destroys;
    C(OpenClipboard(w1) && EmptyClipboard());
    C(destroys==notify+1);
    C(CountClipboardFormats()==0 && !IsClipboardFormatAvailable(CF_TEXT));
    C(GetClipboardOwner()==w1);
    C(CloseClipboard());

    /* 7. OpenClipboard(NULL): the clipboard has no owner, so setting fails. */
    notify=destroys;
    C(OpenClipboard(0) && EmptyClipboard());
    C(destroys==notify+1);
    C(!GetClipboardOwner());
    HGLOBAL unowned=GlobalAlloc(GMEM_MOVEABLE,8);
    C(unowned && !SetClipboardData(CF_TEXT,unowned) && GetLastError()==5);
    C(!GetClipboardData(CF_TEXT) && GetLastError()==0);
    C(CloseClipboard());
    C(!GlobalFree(unowned));                             /* still the app's memory */

    /* 8. Delayed rendering: NULL handle, rendered through WM_RENDERFORMAT. */
    notify=destroys;
    C(OpenClipboard(w1) && EmptyClipboard());
    C(destroys==notify);                                 /* there was no owner to notify */
    C(SetClipboardData(CF_TEXT,0)==0 && renders==0);
    C(IsClipboardFormatAvailable(CF_TEXT));
    HGLOBAL rendered=GetClipboardData(CF_TEXT);
    C(renders==1 && render_format==CF_TEXT);
    C(rendered && !lstrcmpA(GlobalLock(rendered),"delayed"));
    C(!GlobalUnlock(rendered) && GetLastError()==0);
    C(GetClipboardData(CF_TEXT)==rendered);              /* stored for real now */
    C(!SetClipboardData(0,(HANDLE)1) && GetLastError()==87);
    C(CloseClipboard());

    /* 9. Invalid handles. */
    C(!OpenClipboard((HWND)1) && GetLastError()==1400);
    C(!IsClipboardFormatAvailable(0x12345));

    /* 10. Emptying the clipboard frees the data this process allocated. */
    C(DestroyWindow(w1) && DestroyWindow(w2));
    C(OpenClipboard(0) && EmptyClipboard());
    C(!IsClipboardFormatAvailable(CF_TEXT) && CountClipboardFormats()==0);
    C(CloseClipboard());
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"clipboard ok\n",13,&written,0);ExitProcess(0);
}
