/* Per-thread WM_TIMER scheduling; messages are coalesced until removed. */
typedef void (__attribute__((ms_abi)) *pe_timer_proc)(HANDLE,DWORD,uintptr_t,DWORD);
struct pe_timer { struct pe_timer *next; struct pe_ui *ui; HANDLE hwnd; uintptr_t id; DWORD interval; uint64_t due; pe_timer_proc proc; bool pending; };
static struct pe_timer *pe_timers;
static uintptr_t pe_timer_id=1;
__attribute__((ms_abi)) uintptr_t SetTimer(HANDLE hwnd,uintptr_t id,unsigned delay,pe_timer_proc proc)
{
    struct pe_ui *ui=pe_ui_current(); if (!ui) return 0;
    if (hwnd && !pe_window_lookup(hwnd,true)) return 0;
    if (delay<10) delay=10;
    if (delay>0x7fffffffu) delay=0x7fffffffu;
    PE_STATE_GUARD;
    if (!hwnd || !id) {
        do { id=pe_timer_id++; if (!pe_timer_id) pe_timer_id=1; }
        while (!id);
    }
    struct pe_timer *t;
    for(t=pe_timers;t;t=t->next) if(t->ui==ui && t->hwnd==hwnd && t->id==id)break;
    if(!t) {t=pe_raw_map(sizeof(*t));if(!t){SetLastError(8);return 0;}t->next=pe_timers;pe_timers=t;}
    t->ui=ui;t->hwnd=hwnd;t->id=id;t->interval=delay;t->due=GetTickCount64()+delay;t->proc=proc;t->pending=false;
    return id;
}
__attribute__((ms_abi)) BOOL KillTimer(HANDLE hwnd,uintptr_t id)
{
    struct pe_ui *ui=pe_ui_current(); if(!ui)return 0;
    PE_STATE_GUARD;
    struct pe_timer **link=&pe_timers;
    while(*link){struct pe_timer *t=*link;if(t->ui==ui&&t->hwnd==hwnd&&t->id==id){*link=t->next;raw_syscall3(SYS_munmap,(long)t,sizeof(*t),0);return 1;}link=&t->next;}
    SetLastError(87);return 0;
}
static void pe_timer_remove_window(HANDLE hwnd)
{
    PE_STATE_GUARD;
    struct pe_timer **p=&pe_timers;
    while(*p){struct pe_timer *t=*p;if(t->hwnd==hwnd){*p=t->next;raw_syscall3(SYS_munmap,(long)t,sizeof(*t),0);}else p=&t->next;}
}
static void pe_timer_pump(struct pe_ui *ui)
{
    PE_STATE_GUARD;
    uint64_t now=GetTickCount64();
    for(struct pe_timer *t=pe_timers;t;t=t->next) if(t->ui==ui && !t->pending && now>=t->due) {
        if(pe_ui_post(ui,t->hwnd,0x113,t->id,(intptr_t)t->proc,0)) {t->pending=true;t->due=now+t->interval;}
    }
}
static void pe_timer_dequeued(struct pe_ui *ui,HANDLE hwnd,uintptr_t id)
{
    PE_STATE_GUARD;
    for(struct pe_timer *t=pe_timers;t;t=t->next)if(t->ui==ui&&t->hwnd==hwnd&&t->id==id){t->pending=false;break;}
}
