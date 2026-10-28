#define UNICODE
#include <windows.h>
static HWND owner;static HANDLE thread_handle;static volatile LONG worker_ok;static unsigned ok_commands,cancel_commands,timers;
static void fail(unsigned line)
{
    char b[64]="FAIL modal_dialogs ";unsigned n=19;char rev[12];unsigned k=0;
    do{rev[k++]='0'+line%10;line/=10;}while(line);
    while(k)b[n++]=rev[--k];b[n++]='\n';DWORD out;WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&out,0);ExitProcess(1);
}
#define C(x) do{if(!(x))fail(__LINE__);}while(0)
static DWORD WINAPI worker(void *arg)
{
    Sleep(20);HWND h=arg;
    if(!IsWindowEnabled(owner) && EndDialog(h,(INT_PTR)0x123456789abcdefLL) && GetWindowLongPtrW(h,DWLP_MSGRESULT)==(LONG_PTR)0x123456789abcdefLL)InterlockedIncrement(&worker_ok);
    return 0;
}
static void CALLBACK timer_proc(HWND h,UINT msg,UINT_PTR id,DWORD time)
{ (void)time;C(msg==WM_TIMER && id==77);C(KillTimer(h,id));timers++;C(EndDialog(h,4321)); }
static INT_PTR CALLBACK dlgproc(HWND h,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_INITDIALOG){
        C(!IsWindowEnabled(owner));
        if(lp==1){C(SetTimer(h,77,10,timer_proc)==77);return TRUE;}
        if(lp==2){thread_handle=CreateThread(NULL,0,worker,h,0,NULL);C(thread_handle);return TRUE;}
        if(lp==3){PostMessageW(h,WM_APP+5,0,0);return TRUE;}
        if(lp==4){PostMessageW(h,WM_CLOSE,0,0);return TRUE;}
        if(lp==5){PostQuitMessage(63);return TRUE;}
        if(lp==6){C(EndDialog(h,99));return TRUE;}
    }
    if(msg==WM_APP+5){
        HWND edit=GetDlgItem(h,101),r1=GetDlgItem(h,102),r2=GetDlgItem(h,103),list=GetDlgItem(h,104); C(edit&&r1&&r2&&list);
        C((HIWORD(SendMessageW(h,DM_GETDEFID,0,0))==DC_HASDEFID) && LOWORD(SendMessageW(h,DM_GETDEFID,0,0))==IDOK);
        SetFocus(edit); MSG m={0};m.hwnd=edit;m.message=WM_KEYDOWN;m.wParam=VK_TAB;
        C(IsDialogMessageW(h,&m) && GetFocus()==r1);
        BYTE keys[256]={0};keys[VK_SHIFT]=0x80;C(SetKeyboardState(keys));m.hwnd=r1;
        C(IsDialogMessageA(h,&m) && GetFocus()==edit);keys[VK_SHIFT]=0;C(SetKeyboardState(keys));
        SendMessageW(h,WM_NEXTDLGCTL,(WPARAM)r1,TRUE);C(GetFocus()==r1);
        m.hwnd=r1;m.wParam=VK_RIGHT;C(IsDialogMessageW(h,&m) && GetFocus()==r2);
        C(SendMessageW(h,DM_SETDEFID,IDOK,0));m.hwnd=r2;m.wParam=VK_RETURN;C(IsDialogMessageW(h,&m) && ok_commands==1);
        C(SetDlgItemTextA(h,101,"caf\xc3\xa9"));C(SendDlgItemMessageA(h,101,WM_GETTEXTLENGTH,0,0)==5);
        SendDlgItemMessageA(h,101,EM_SETSEL,0,-1);SendDlgItemMessageA(h,101,EM_REPLACESEL,TRUE,(LPARAM)"pi\xc3\xb1" "a");
        char text[32];C(GetDlgItemTextA(h,101,text,32)==5 && !lstrcmpA(text,"pi\xc3\xb1" "a"));
        C(SendDlgItemMessageA(h,104,LB_ADDSTRING,0,(LPARAM)"caf\xc3\xa9")==0);
        C(SendDlgItemMessageA(h,104,LB_GETTEXTLEN,0,0)==5);
        C(SendDlgItemMessageA(h,104,LB_GETTEXT,0,(LPARAM)text)==5 && !lstrcmpA(text,"caf\xc3\xa9"));
        C(SendDlgItemMessageA(h,104,LB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)"caf\xc3\xa9")==0);
        m.wParam=VK_ESCAPE;C(IsDialogMessageW(h,&m) && cancel_commands==1);
        C(EndDialog(h,321));return TRUE;
    }
    if(msg==WM_COMMAND){
        if(LOWORD(wp)==IDOK){ok_commands++;return TRUE;}
        if(LOWORD(wp)==IDCANCEL){cancel_commands++;if(cancel_commands==2)C(EndDialog(h,777));return TRUE;}
    }
    return FALSE;
}
void mainCRTStartup(void)
{
    HINSTANCE h=GetModuleHandleW(NULL);WNDCLASSEXW c={0};c.cbSize=sizeof(c);c.hInstance=h;c.lpfnWndProc=DefWindowProcW;c.lpszClassName=L"ModalOwner";C(RegisterClassExW(&c));
    owner=CreateWindowExW(0,c.lpszClassName,L"",0,0,0,100,100,NULL,NULL,h,NULL);C(owner && IsWindowEnabled(owner));
    C(DialogBoxParamW(h,MAKEINTRESOURCEW(201),owner,dlgproc,1)==4321 && timers==1 && IsWindowEnabled(owner));
    C(DialogBoxParamA(h,MAKEINTRESOURCEA(201),owner,dlgproc,2)==(INT_PTR)0x123456789abcdefLL && IsWindowEnabled(owner));
    C(WaitForSingleObject(thread_handle,5000)==WAIT_OBJECT_0 && worker_ok==1 && CloseHandle(thread_handle));
    C(DialogBoxParamW(h,MAKEINTRESOURCEW(201),owner,dlgproc,3)==321 && IsWindowEnabled(owner));
    C(DialogBoxParamW(h,MAKEINTRESOURCEW(201),owner,dlgproc,4)==777 && IsWindowEnabled(owner));
    C(DialogBoxParamW(h,MAKEINTRESOURCEW(201),owner,dlgproc,5)==-1 && IsWindowEnabled(owner));
    MSG m;C(PeekMessageW(&m,NULL,WM_QUIT,WM_QUIT,PM_REMOVE) && m.message==WM_QUIT && m.wParam==63);
    HRSRC res=FindResourceW(h,MAKEINTRESOURCEW(201),RT_DIALOG);C(res);LPCDLGTEMPLATE templ=LockResource(LoadResource(h,res));C(templ);
    C(DialogBoxIndirectParamW(h,templ,owner,dlgproc,6)==99);
    C(DialogBoxIndirectParamA(h,(LPCDLGTEMPLATEA)templ,owner,dlgproc,6)==99);
    C(!EndDialog(owner,0) && GetLastError()==87);
    C(EnableWindow(owner,FALSE)==0);C(DialogBoxParamW(h,MAKEINTRESOURCEW(201),owner,dlgproc,6)==99 && !IsWindowEnabled(owner));
    C(EnableWindow(owner,TRUE)!=0 && DestroyWindow(owner));
    DWORD out;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"modal dialogs ok\n",17,&out,0);ExitProcess(0);
}
