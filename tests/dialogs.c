#define UNICODE
#include <windows.h>
static unsigned calls; static HWND init_focus; static LPARAM init_arg;
static void fail(unsigned line)
{
    char b[64]="FAIL dialogs line "; unsigned n=18; char rev[12]; unsigned count=0;
    do { rev[count++]='0'+line%10; line/=10; } while(line);
    while(count) b[n++]=rev[--count]; b[n++]=' '; unsigned err=GetLastError(); do { rev[count++]='0'+err%10; err/=10; } while(err); while(count) b[n++]=rev[--count]; b[n++]='\n'; DWORD out;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&out,0); ExitProcess(1);
}
#define C(x) do { if(!(x)) fail(__LINE__); } while(0)
static INT_PTR CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if (m==WM_INITDIALOG) {
        calls++; init_focus=(HWND)w; init_arg=l;
        SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)0x123456789abcdef0ULL);
        return TRUE;
    }
    return FALSE;
}
static void put16(unsigned char *b,unsigned *p,unsigned v)
{ b[(*p)++]=v; b[(*p)++]=v>>8; }
static void put32(unsigned char *b,unsigned *p,unsigned v)
{ put16(b,p,v); put16(b,p,v>>16); }
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(NULL); RECT u={0,0,4,8};
    DWORD base=GetDialogBaseUnits(); C(LOWORD(base)>0 && HIWORD(base)>0);
    HWND d=CreateDialogParamW(h,MAKEINTRESOURCEW(101),NULL,proc,0x1234);
    C(d && calls==1 && init_arg==0x1234);
    C(init_focus==GetDlgItem(d,102) && GetFocus()==init_focus);
    C(GetWindowLongPtrW(d,DWLP_USER)==(LONG_PTR)0x123456789abcdef0ULL);
    C(GetWindowLongPtrW(d,DWLP_DLGPROC)==(LONG_PTR)proc);
    WCHAR caption[64]; C(GetWindowTextW(d,caption,64)==11 && !lstrcmpW(caption,L"Test Dialog"));
    C((GetWindowLongW(d,GWL_STYLE) & (WS_POPUP|WS_CAPTION|DS_SETFONT)) == (WS_POPUP|WS_CAPTION|DS_SETFONT));
    C(GetDlgCtrlID(GetDlgItem(d,103))==103);
    C(MapDialogRect(d,&u)); C(u.right>0 && u.bottom>0);
    RECT rect; C(GetWindowRect(GetDlgItem(d,102),&rect));
    C(rect.right-rect.left==MulDiv(70,u.right,4));
    C(rect.bottom-rect.top==MulDiv(12,u.bottom,8));
    WCHAR text[64]; char ascii[64];
    C(SetDlgItemTextW(d,102,L"hello") && GetDlgItemTextW(d,102,text,64)==5 && !lstrcmpW(text,L"hello"));
    C(SetDlgItemTextA(d,102,"value") && GetDlgItemTextA(d,102,ascii,64)==5 && !lstrcmpA(ascii,"value"));
    C(SetDlgItemInt(d,102,12345,FALSE)); BOOL converted=FALSE;
    C(GetDlgItemInt(d,102,&converted,FALSE)==12345 && converted);
    C(CheckDlgButton(d,103,BST_CHECKED) && IsDlgButtonChecked(d,103)==BST_CHECKED);
    C(CheckRadioButton(d,104,105,105) && IsDlgButtonChecked(d,104)==BST_UNCHECKED && IsDlgButtonChecked(d,105)==BST_CHECKED);
    C(GetNextDlgTabItem(d,NULL,FALSE)==GetDlgItem(d,102));
    C(GetNextDlgGroupItem(d,GetDlgItem(d,104),FALSE)==GetDlgItem(d,105));
    C(SendDlgItemMessageA(d,102,WM_GETTEXT,64,(LPARAM)ascii)==5);
    C(DestroyWindow(d));

    /* An in-memory DIALOGEX with one ordinary button. */
    unsigned char b[256]={0}; unsigned p=0;
    put16(b,&p,1);put16(b,&p,0xffff);put32(b,&p,0);put32(b,&p,0);put32(b,&p,WS_POPUP);
    put16(b,&p,1);put16(b,&p,1);put16(b,&p,2);put16(b,&p,60);put16(b,&p,30);
    put16(b,&p,0);put16(b,&p,0);put16(b,&p,'X');put16(b,&p,0);
    while(p&3)b[p++]=0;
    put32(b,&p,0);put32(b,&p,0);put32(b,&p,WS_CHILD|WS_VISIBLE|WS_TABSTOP);
    put16(b,&p,3);put16(b,&p,4);put16(b,&p,20);put16(b,&p,10);put32(b,&p,200);
    put16(b,&p,0xffff);put16(b,&p,0x80);put16(b,&p,'B');put16(b,&p,0);put16(b,&p,0);
    d=CreateDialogIndirectParamW(h,(DLGTEMPLATE *)b,NULL,proc,0x4321);
    C(d && calls==2 && init_arg==0x4321 && init_focus==GetDlgItem(d,200));
    C(GetDlgCtrlID(init_focus)==200 && !IsWindowVisible(d));
    C(GetWindowRect(init_focus,&rect));C(rect.right-rect.left==40 && rect.bottom-rect.top==20);
    C(DestroyWindow(d));
    /* Wrong item count forces a guarded read beyond the allocated page. */
    DWORD old; unsigned char *allocation=HeapAlloc(GetProcessHeap(),0,140000); C(allocation);
    unsigned char *pages=(unsigned char *)(((ULONG_PTR)allocation+4095)&~(ULONG_PTR)4095);
    C(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old));
    unsigned char *truncated=pages+4096-4; truncated[0]=1;truncated[1]=0;truncated[2]=0xff;truncated[3]=0xff;
    SetLastError(0); C(!CreateDialogIndirectParamW(h,(DLGTEMPLATE *)truncated,NULL,proc,0) && GetLastError()==87);
    /* Item count exceeds the explicit 4096 limit. */
    b[16]=1;b[17]=16;
    SetLastError(0); C(!CreateDialogIndirectParamW(h,(DLGTEMPLATE *)b,NULL,proc,0) && GetLastError()==87);
    C(VirtualProtect(pages+4096,4096,PAGE_READWRITE,&old));
    C(HeapFree(GetProcessHeap(),0,allocation));
    DWORD out; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"dialogs ok\n",11,&out,0);ExitProcess(0);
}
