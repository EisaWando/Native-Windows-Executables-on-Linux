/* Removed records remain pinned until all active dispatches finish. */
typedef intptr_t (__attribute__((ms_abi)) *pe_hook_proc)(int, uintptr_t, intptr_t);
struct pe_hook { struct pe_hook *next; HANDLE handle; pe_hook_proc proc; DWORD tid; int type; bool removed; };
struct pe_hook_frame { struct pe_hook_frame *prev; struct pe_hook *current; int type; DWORD tid; };
static struct pe_hook *pe_hooks;
static unsigned pe_hook_dispatches;
static void pe_hook_reap(void)
{
    if(pe_hook_dispatches) return;
    struct pe_hook **link=&pe_hooks;
    while(*link) { struct pe_hook *h=*link; if(!h->removed) { link=&h->next; continue; }
        *link=h->next; raw_syscall3(SYS_munmap,(long)h,sizeof(*h),0); }
}
static intptr_t pe_hook_next(struct pe_ui *u, struct pe_hook *start, int type, DWORD tid, int code, uintptr_t wp, intptr_t lp)
{
    struct pe_hook *h = start;
    {
        PE_STATE_GUARD;
        while (h && (h->removed || h->type != type || (h->tid && h->tid != tid))) h = h->next;
    }
    if (!h) return 0;
    for (struct pe_hook_frame *f=u->hook_frame; f; f=f->prev)
        if (f->current == h) return pe_hook_next(u,h->next,type,tid,code,wp,lp);
    struct pe_hook_frame frame = { u->hook_frame, h, type, tid };
    u->hook_frame = &frame;
    intptr_t result = h->proc(code,wp,lp);
    u->hook_frame = frame.prev;
    return result;
}
static intptr_t pe_hook_dispatch(int type, int code, uintptr_t wp, intptr_t lp)
{
    struct pe_ui *u=pe_ui_current(); if (!u) return 0;
    struct pe_hook *start; { PE_STATE_GUARD; pe_hook_dispatches++; start=pe_hooks; }
    intptr_t result=pe_hook_next(u,start,type,u->tid,code,wp,lp);
    { PE_STATE_GUARD; pe_hook_dispatches--; pe_hook_reap(); }
    return result;
}
__attribute__((ms_abi)) intptr_t CallNextHookEx(HANDLE ignored,int code,uintptr_t wp,intptr_t lp)
{
    (void)ignored; struct pe_ui *u=pe_ui_current();
    if (!u || !u->hook_frame) return 0;
    struct pe_hook_frame *f=u->hook_frame;
    return pe_hook_next(u,f->current->next,f->type,f->tid,code,wp,lp);
}
__attribute__((ms_abi)) HANDLE SetWindowsHookExW(int type,pe_hook_proc proc,HANDLE module,DWORD tid)
{
    if (type < -1 || type > 14 || !proc) { SetLastError(87); return NULL; }
    if (!tid && !module) { SetLastError(1428); return NULL; }
    struct pe_hook *h=pe_raw_map(sizeof(*h)); if (!h) { SetLastError(8); return NULL; }
    PE_STATE_GUARD;
    struct pe_gui_object *o=pe_gui_new(PE_GUI_HOOK);
    if (!o) { raw_syscall3(SYS_munmap,(long)h,sizeof(*h),0); return NULL; }
    h->proc=proc; h->tid=tid; h->type=type; h->handle=pe_gui_handle(o); h->next=pe_hooks; pe_hooks=h; o->hook=h;
    return h->handle;
}
__attribute__((ms_abi)) HANDLE SetWindowsHookExA(int type,pe_hook_proc proc,HANDLE module,DWORD tid)
{ return SetWindowsHookExW(type,proc,module,tid); }
__attribute__((ms_abi)) BOOL UnhookWindowsHookEx(HANDLE handle)
{
    PE_STATE_GUARD; struct pe_gui_object *o=pe_gui_lookup(handle,PE_GUI_HOOK);
    if (!o) { SetLastError(1404); return 0; }
    o->hook->removed=true; pe_gui_drop(o); pe_hook_reap(); return 1;
}
__attribute__((ms_abi)) HANDLE SetWindowsHookW(int type,pe_hook_proc proc)
{ return SetWindowsHookExW(type,proc,NULL,GetCurrentThreadId()); }
__attribute__((ms_abi)) BOOL UnhookWindowsHook(int type,pe_hook_proc proc)
{
    HANDLE h=NULL; { PE_STATE_GUARD; for (struct pe_hook *p=pe_hooks;p;p=p->next)
        if (!p->removed && p->type==type && p->proc==proc && (!p->tid || p->tid==GetCurrentThreadId())) { h=p->handle; break; } }
    if (!h) { SetLastError(1404); return 0; } return UnhookWindowsHookEx(h);
}
struct pe_cwp { intptr_t lp; uintptr_t wp; DWORD msg; HANDLE hwnd; };
struct pe_cwpret { intptr_t result, lp; uintptr_t wp; DWORD msg; HANDLE hwnd; };
static intptr_t pe_hook_send(struct pe_window *w,DWORD msg,uintptr_t wp,intptr_t lp,bool own)
{
    struct pe_cwp info={lp,wp,msg,w->handle};
    pe_hook_dispatch(4,0,own,(intptr_t)&info);
    intptr_t result=w->proc(w->handle,msg,wp,lp);
    struct pe_cwpret after={result,lp,wp,msg,w->handle};
    pe_hook_dispatch(12,0,own,(intptr_t)&after);
    return result;
}
