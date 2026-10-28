/* Keyboard state belongs to the calling GUI thread. */
__attribute__((ms_abi)) int16_t GetKeyState(int vk)
{
    struct pe_ui *u=pe_ui_current();if(!u || vk<0 || vk>255){SetLastError(87);return 0;}
    PE_STATE_GUARD;return u->keys[vk]?((int16_t)0x8000):0;
}
__attribute__((ms_abi)) BOOL GetKeyboardState(unsigned char *state)
{
    if(!state){SetLastError(87);return 0;}struct pe_ui *u=pe_ui_current();if(!u)return 0;
    PE_STATE_GUARD;for(int i=0;i<256;i++)state[i]=u->keys[i]?0x80:0;return 1;
}
__attribute__((ms_abi)) BOOL SetKeyboardState(const unsigned char *state)
{
    if(!state){SetLastError(87);return 0;}struct pe_ui *u=pe_ui_current();if(!u)return 0;
    PE_STATE_GUARD;for(int i=0;i<256;i++)u->keys[i]=!!(state[i]&0x80);return 1;
}
__attribute__((ms_abi)) BOOL EndDialog(HANDLE hwnd,intptr_t result)
{
    {
        PE_STATE_GUARD;
        struct pe_window *w=pe_window_lookup(hwnd,false);
        if(!w)return 0;
        if(!w->modal || __atomic_load_n(&w->modal->ended,__ATOMIC_ACQUIRE)){SetLastError(87);return 0;}
        memcpy(w->extra,&result,sizeof(result));
        w->modal->result=result;__atomic_store_n(&w->modal->ended,true,__ATOMIC_RELEASE);
    }
    PostMessageW(hwnd,0,0,0); /* wake an otherwise idle modal loop */
    return 1;
}
__attribute__((ms_abi)) BOOL IsDialogMessageW(HANDLE dialog,const struct pe_msg *msg)
{
    struct pe_window *w=pe_window_lookup(dialog,false);if(!w)return 0;
    if(!msg){SetLastError(87);return 0;}
    if(msg->message==0x12 || !msg->hwnd || (msg->hwnd!=dialog && !IsChild(dialog,msg->hwnd))) return 0;
    if(msg->message==0x100 || msg->message==0x104){
        HANDLE focus=GetFocus(); if(!focus || (focus!=dialog && !IsChild(dialog,focus))) focus=msg->hwnd;
        intptr_t dlgcode=focus?SendMessageW(focus,0x87,msg->wparam,(intptr_t)msg):0;
        unsigned key=msg->wparam;
        if (!(dlgcode&4)) {
            if(key==9 && !(dlgcode&2)) {
                HANDLE next=GetNextDlgTabItem(dialog,focus,GetKeyState(16)<0);
                if(next)SetFocus(next);return 1;
            }
            if(key>=0x25 && key<=0x28 && !(dlgcode&1)) {
                HANDLE next=GetNextDlgGroupItem(dialog,focus,key==0x25 || key==0x26);
                if(next)SetFocus(next);return 1;
            }
            if(key==13 || key==27) {
                unsigned id=key==27?2:(w->dialog_default_id?w->dialog_default_id:1);
                HANDLE button=GetDlgItem(dialog,id);
                if(!button || IsWindowEnabled(button))SendMessageW(dialog,0x111,id,(intptr_t)button);
                return 1;
            }
        }
    }
    TranslateMessage(msg);DispatchMessageW(msg);return 1;
}
__attribute__((ms_abi)) BOOL IsDialogMessageA(HANDLE h,const struct pe_msg *m)
{ return IsDialogMessageW(h,m); }
static intptr_t pe_modal_run(HANDLE instance,const void *source,size_t size,HANDLE owner,pe_dlgproc proc,intptr_t param)
{
    if(!proc){SetLastError(87);return -1;}
    bool enable_owner=false;
    if(owner){if(!pe_window_lookup(owner,true))return -1;enable_owner=IsWindowEnabled(owner);if(enable_owner)EnableWindow(owner,0);}
    struct pe_modal_state state={0};
    HANDLE hwnd=pe_dialog_create(instance,source,size,owner,proc,param,&state);
    intptr_t result=-1;bool quit=false;int quit_code=0;
    if(hwnd) {
        struct pe_window *w=pe_window_lookup(hwnd,true);
        if(w && !__atomic_load_n(&state.ended,__ATOMIC_ACQUIRE) && !w->visible) {
            SetLastError(0);ShowWindow(hwnd,5);
            if(!w->visible) { DWORD error=GetLastError();if(!error)error=31;SetLastError(error);goto cleanup; }
        }
        if(w && !__atomic_load_n(&state.ended,__ATOMIC_ACQUIRE) && !(w->style&0x40000000u)) SetActiveWindow(hwnd);
        while(!__atomic_load_n(&state.ended,__ATOMIC_ACQUIRE) && IsWindow(hwnd)) {
            struct pe_msg msg;int got=GetMessageW(&msg,NULL,0,0);
            if(got<0)break;
            if(!got){quit=true;quit_code=(int)msg.wparam;break;}
            if(!pe_hook_dispatch(-1,0,0,(intptr_t)&msg) && !IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(__atomic_load_n(&state.ended,__ATOMIC_ACQUIRE))result=state.result;
    }
cleanup:
    if(hwnd && IsWindow(hwnd)) {
        { PE_STATE_GUARD; struct pe_window *w=pe_window_lookup(hwnd,false);if(w)w->modal=NULL; }
        DestroyWindow(hwnd);
    }
    if(enable_owner && IsWindow(owner)){EnableWindow(owner,1);SetActiveWindow(owner);}
    if(quit)PostQuitMessage(quit_code);
    return result;
}
__attribute__((ms_abi)) intptr_t DialogBoxIndirectParamW(HANDLE instance,const void *source,HANDLE owner,pe_dlgproc proc,intptr_t param)
{ return pe_modal_run(instance,source,PE_DIALOG_LIMIT,owner,proc,param); }
__attribute__((ms_abi)) intptr_t DialogBoxIndirectParamA(HANDLE instance,const void *source,HANDLE owner,pe_dlgproc proc,intptr_t param)
{ return DialogBoxIndirectParamW(instance,source,owner,proc,param); }
__attribute__((ms_abi)) intptr_t DialogBoxParamW(HANDLE instance,const uint16_t *name,HANDLE owner,pe_dlgproc proc,intptr_t param)
{
    HANDLE resource=FindResourceW(instance,name,(void *)5);if(!resource)return -1;
    DWORD n=SizeofResource(instance,resource);void *src=LockResource(LoadResource(instance,resource));
    if(!src || !n){SetLastError(87);return -1;}
    return pe_modal_run(instance,src,n,owner,proc,param);
}
__attribute__((ms_abi)) intptr_t DialogBoxParamA(HANDLE instance,const char *name,HANDLE owner,pe_dlgproc proc,intptr_t param)
{
    uint16_t wide[1025];const uint16_t *s=(void *)name;
    if((uintptr_t)name>=65536){if(!MultiByteToWideChar(65001,8,name,-1,wide,1025))return -1;s=wide;}
    return DialogBoxParamW(instance,s,owner,proc,param);
}
/* Marshal text-bearing standard messages. Non-string messages pass unchanged;
 * this follows the currently supported edit/list/combo control messages. */
static intptr_t pe_dialog_ansi_message(HANDLE parent,int id,DWORD msg,uintptr_t wp,intptr_t lp)
{
    HANDLE child=GetDlgItem(parent,id);if(!child)return 0;
    if(msg==0xc)return SetDlgItemTextA(parent,id,(const char *)lp);
    if(msg==0xd)return GetDlgItemTextA(parent,id,(char *)lp,(int)wp);
    bool len=msg==0xe || msg==0x18a || msg==0x149;
    bool get=msg==0x189 || msg==0x148;
    if(len || get){
        DWORD lengthmsg=(msg==0xe)?0xe:(msg==0x189||msg==0x18a)?0x18a:0x149;
        DWORD getmsg=(msg==0xe)?0xd:(msg==0x189||msg==0x18a)?0x189:0x148;
        intptr_t n=SendMessageW(child,lengthmsg,wp,0);
        if(n<0 || n>1048576)return n<0?n:0;
        uint16_t *wide=pe_raw_map(((size_t)n+1)*2);if(!wide){SetLastError(8);return 0;}
        intptr_t got=SendMessageW(child,getmsg,msg==0xe?(uintptr_t)n+1:wp,(intptr_t)wide);
        int bytes=got<0?0:WideCharToMultiByte(65001,0,wide,(int)got,NULL,0,NULL,NULL);
        if(get && !lp){raw_syscall3(SYS_munmap,(long)wide,((size_t)n+1)*2,0);SetLastError(87);return -1;}
        if(get && got>=0){if(bytes)WideCharToMultiByte(65001,0,wide,(int)got,(char *)lp,bytes,NULL,NULL);((char *)lp)[bytes]=0;}
        raw_syscall3(SYS_munmap,(long)wide,((size_t)n+1)*2,0);return got<0?got:bytes;
    }
    bool input=msg==0xc2 || msg==0x180 || msg==0x181 || msg==0x18c || msg==0x18d || msg==0x18f || msg==0x1a2 ||
               msg==0x143 || msg==0x145 || msg==0x14a || msg==0x14c || msg==0x14d || msg==0x158;
    if(input && lp){
        int n=MultiByteToWideChar(65001,8,(const char *)lp,-1,NULL,0);if(!n)return -1;
        uint16_t *wide=pe_raw_map((size_t)n*2);if(!wide){SetLastError(8);return -1;}
        if(!MultiByteToWideChar(65001,8,(const char *)lp,-1,wide,n)){raw_syscall3(SYS_munmap,(long)wide,(size_t)n*2,0);return -1;}
        intptr_t result=SendMessageW(child,msg,wp,(intptr_t)wide);raw_syscall3(SYS_munmap,(long)wide,(size_t)n*2,0);return result;
    }
    return SendMessageW(child,msg,wp,lp);
}
