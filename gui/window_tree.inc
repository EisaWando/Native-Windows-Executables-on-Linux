/* Window tree and z-order. pe_windows is ordered top-to-bottom; window
 * structures remain allocated after destruction so enumeration snapshots are safe. */
static HANDLE pe_tree_group(struct pe_window *w)
{ return ((w->style & 0x40000000u) || w->reparented) ? w->parent : NULL; }
static bool pe_tree_sibling(struct pe_window *a,struct pe_window *b)
{ return pe_tree_group(a)==pe_tree_group(b); }
static bool pe_tree_reorder(struct pe_window *w,HANDLE after)
{
    PE_STATE_GUARD;
    intptr_t special=(intptr_t)after;
    struct pe_window *target=NULL;
    if (special!=0 && special!=1 && special!=-1 && special!=-2) {
        target=pe_window_lookup(after,false);
        if (!target) return false;
        if (target==w) return true;
        if (!pe_tree_sibling(w,target)) { SetLastError(87); return false; }
    }
    if (special==-1) w->exstyle|=8;
    else if (special==-2 || special==1) w->exstyle&=~8u;
    else if (target) { if (target->exstyle&8) w->exstyle|=8; else w->exstyle&=~8u; }
    struct pe_window **link=&pe_windows;
    while (*link && *link!=w) link=&(*link)->next;
    if (!*link) { SetLastError(1400); return false; }
    *link=w->next;
    struct pe_window **insert=&pe_windows;
    if (target) {
        while (*insert && *insert!=target) insert=&(*insert)->next;
        if (*insert) insert=&(*insert)->next;
    } else if (special==1) {
        struct pe_window **last=NULL;
        while (*insert) { if (!(*insert)->dead && pe_tree_sibling(w,*insert)) last=&(*insert)->next; insert=&(*insert)->next; }
        if (last) insert=last;
    } else {
        while (*insert) {
            if (!(*insert)->dead && pe_tree_sibling(w,*insert) &&
                ((w->exstyle&8) || !((*insert)->exstyle&8))) break;
            insert=&(*insert)->next;
        }
    }
    w->next=*insert; *insert=w;
    return true;
}
__attribute__((ms_abi)) HANDLE SetParent(HANDLE child,HANDLE parent)
{
    HANDLE old;
    {
        PE_STATE_GUARD;
        struct pe_window *w=pe_window_lookup(child,false);
        if (!w) return NULL;
        struct pe_window *p=parent?pe_window_lookup(parent,false):NULL;
        if (parent && !p) return NULL;
        for (unsigned depth=0;p;depth++) {
            if (p==w || depth>=128) { SetLastError(87); return NULL; }
            p=p->parent?pe_window_lookup(p->parent,false):NULL;
        }
        old=w->parent; bool was_reparented=w->reparented; w->parent=parent; w->reparented=true;
        if (!pe_tree_reorder(w,NULL)) { w->parent=old; w->reparented=was_reparented; return NULL; }
        if (w->visible) {
            pe_invalidate_full(w); w->erase=true;
            struct pe_window *o=old?pe_window_lookup(old,false):NULL;
            struct pe_window *n=parent?pe_window_lookup(parent,false):NULL;
            if (o) { pe_invalidate_full(o); o->erase=true; }
            if (n) { pe_invalidate_full(n); n->erase=true; }
        }
    }
    /* WM_PARENTNOTIFY documents creation/destruction, not reparenting. */
    return old;
}
__attribute__((ms_abi)) HANDLE GetAncestor(HANDLE hwnd,unsigned flags)
{
    PE_STATE_GUARD;
    struct pe_window *w=pe_window_lookup(hwnd,false); if (!w) return NULL;
    if (flags<1 || flags>3) { SetLastError(87); return NULL; }
    if (flags==1) return ((w->style&0x40000000u) || w->reparented)?w->parent:NULL;
    for (unsigned depth=0;depth<128;depth++) {
        bool follow=(w->style&0x40000000u) || w->reparented || (flags==3 && (w->style&0x80000000u));
        if (!follow || !w->parent) return w->handle;
        w=pe_window_lookup(w->parent,false); if (!w) return NULL;
    }
    SetLastError(87); return NULL;
}
__attribute__((ms_abi)) HANDLE GetTopWindow(HANDLE parent)
{
    PE_STATE_GUARD;
    if (parent && !pe_window_lookup(parent,false)) return NULL;
    for (struct pe_window *w=pe_windows;w;w=w->next)
        if (!w->dead && pe_tree_group(w)==parent) return w->handle;
    return NULL;
}
__attribute__((ms_abi)) HANDLE GetWindow(HANDLE hwnd,unsigned command)
{
    PE_STATE_GUARD;
    struct pe_window *w=pe_window_lookup(hwnd,false); if (!w) return NULL;
    if (command==4) return ((w->style&0x40000000u) || w->reparented)?NULL:w->parent;
    if (command==5) return GetTopWindow(hwnd);
    if (command>5) { SetLastError(87); return NULL; }
    struct pe_window *first=NULL,*last=NULL,*prev=NULL,*next=NULL,*prior=NULL;
    for (struct pe_window *p=pe_windows;p;p=p->next) {
        if (p->dead || !pe_tree_sibling(w,p)) continue;
        if (!first) first=p;
        if (p==w) prev=prior;
        else if (prior==w && !next) next=p;
        prior=last=p;
    }
    struct pe_window *r=command==0?first:command==1?last:command==2?next:prev;
    return r?r->handle:NULL;
}
typedef BOOL (__attribute__((ms_abi)) *pe_enum_window_proc)(HANDLE,intptr_t);
struct pe_window_snapshot { HANDLE *v; size_t n,capacity; };
static bool pe_tree_snapshot_add(struct pe_window_snapshot *s,HANDLE hwnd)
{
    if (s->n==s->capacity) {
        size_t cap=s->capacity?s->capacity*2:32;
        HANDLE *v=pe_raw_map(cap*sizeof(*v)); if (!v) { SetLastError(8); return false; }
        if (s->n) memcpy(v,s->v,s->n*sizeof(*v));
        if (s->v) raw_syscall3(SYS_munmap,(long)s->v,s->capacity*sizeof(*v),0);
        s->v=v;s->capacity=cap;
    }
    s->v[s->n++]=hwnd; return true;
}
static bool pe_tree_children(struct pe_window_snapshot *s,HANDLE parent,unsigned depth)
{
    if (depth>=128) { SetLastError(87); return false; }
    for (struct pe_window *w=pe_windows;w;w=w->next)
        if (!w->dead && (w->style&0x40000000u) && w->parent==parent)
            if (!pe_tree_snapshot_add(s,w->handle) || !pe_tree_children(s,w->handle,depth+1)) return false;
    return true;
}
static BOOL pe_tree_enum(HANDLE parent,DWORD tid,pe_enum_window_proc callback,intptr_t param,int kind)
{
    if (!callback) { SetLastError(87); return 0; }
    struct pe_window_snapshot s={0}; bool ok=true;
    {
        PE_STATE_GUARD;
        if (kind==1 && parent) {
            if (!pe_window_lookup(parent,false)) return 0;
            ok=pe_tree_children(&s,parent,0);
        } else for (struct pe_window *w=pe_windows;w;w=w->next)
            if (!w->dead && !pe_tree_group(w) && (!tid || w->ui->tid==tid))
                if (!pe_tree_snapshot_add(&s,w->handle)) { ok=false; break; }
    }
    if (ok) for (size_t i=0;i<s.n;i++) {
        if (!IsWindow(s.v[i])) continue;
        if (!callback(s.v[i],param)) { ok=false; break; }
    }
    if (s.v) raw_syscall3(SYS_munmap,(long)s.v,s.capacity*sizeof(*s.v),0);
    return ok;
}
__attribute__((ms_abi)) BOOL EnumChildWindows(HANDLE p,pe_enum_window_proc f,intptr_t v) { return pe_tree_enum(p,0,f,v,1); }
__attribute__((ms_abi)) BOOL EnumWindows(pe_enum_window_proc f,intptr_t v) { return pe_tree_enum(NULL,0,f,v,0); }
__attribute__((ms_abi)) BOOL EnumThreadWindows(DWORD t,pe_enum_window_proc f,intptr_t v)
{ if (!t) { SetLastError(87); return 0; } return pe_tree_enum(NULL,t,f,v,0); }
static HANDLE pe_tree_find(HANDLE parent,HANDLE after,const uint16_t *cls,const uint16_t *title,bool child)
{
    PE_STATE_GUARD;
    if (parent && !pe_window_lookup(parent,false)) return NULL;
    struct pe_window *start=after?pe_window_lookup(after,false):NULL;
    if (after && !start) return NULL;
    bool past=!after;
    for (struct pe_window *w=pe_windows;w;w=w->next) {
        if (w->dead || pe_tree_group(w)!=parent) continue;
        if (!past) { if (w==start) past=true; continue; }
        if (child && !(w->style&0x40000000u)) continue;
        if (cls && ((uintptr_t)cls<65536 ? (uintptr_t)cls!=w->cls->atom : !pe_name_equal(cls,w->cls->name))) continue;
        if (title && !pe_name_equal(title,pe_control_caption(w))) continue;
        return w->handle;
    }
    return NULL;
}
__attribute__((ms_abi)) HANDLE FindWindowExW(HANDLE p,HANDLE a,const uint16_t *c,const uint16_t *t)
{ return pe_tree_find(p,a,c,t,p!=NULL); }
__attribute__((ms_abi)) HANDLE FindWindowW(const uint16_t *c,const uint16_t *t)
{ return pe_tree_find(NULL,NULL,c,t,false); }
static bool pe_tree_ansi(const char *s,uint16_t *out)
{ return !s || (uintptr_t)s<65536 || MultiByteToWideChar(65001,8,s,-1,out,1024)!=0; }
__attribute__((ms_abi)) HANDLE FindWindowExA(HANDLE p,HANDLE a,const char *c,const char *t)
{
    uint16_t wc[1024],wt[1024];
    if (!pe_tree_ansi(c,wc) || !pe_tree_ansi(t,wt)) return NULL;
    return FindWindowExW(p,a,(uintptr_t)c<65536?(void *)c:wc,(uintptr_t)t<65536?(void *)t:wt);
}
__attribute__((ms_abi)) HANDLE FindWindowA(const char *c,const char *t) { return FindWindowExA(NULL,NULL,c,t); }
__attribute__((ms_abi)) BOOL BringWindowToTop(HANDLE hwnd)
{ return SetWindowPos(hwnd,NULL,0,0,0,0,1|2); }
struct pe_defer_item { HANDLE hwnd,after; int x,y,width,height; DWORD flags; };
struct pe_defer { struct pe_defer *next; struct pe_defer_item *items; size_t count,capacity; };
static struct pe_defer *pe_defers;
static struct pe_defer *pe_defer_lookup(HANDLE h)
{ for (struct pe_defer *d=pe_defers;d;d=d->next) if ((HANDLE)d==h) return d; SetLastError(6); return NULL; }
static void pe_defer_free(struct pe_defer *d)
{
    struct pe_defer **p=&pe_defers; while (*p && *p!=d) p=&(*p)->next; if (*p) *p=d->next;
    if (d->items) raw_syscall3(SYS_munmap,(long)d->items,d->capacity*sizeof(*d->items),0);
    raw_syscall3(SYS_munmap,(long)d,sizeof(*d),0);
}
__attribute__((ms_abi)) HANDLE BeginDeferWindowPos(int n)
{
    if (n<0 || n>65536) { SetLastError(87); return NULL; }
    PE_STATE_GUARD;
    struct pe_defer *d=pe_raw_map(sizeof(*d)); if (!d) { SetLastError(8); return NULL; }
    d->capacity=n?n:1; d->items=pe_raw_map(d->capacity*sizeof(*d->items));
    if (!d->items) { raw_syscall3(SYS_munmap,(long)d,sizeof(*d),0); SetLastError(8); return NULL; }
    d->next=pe_defers; pe_defers=d; return d;
}
__attribute__((ms_abi)) HANDLE DeferWindowPos(HANDLE h,HANDLE hwnd,HANDLE after,int x,int y,int cx,int cy,DWORD flags)
{
    PE_STATE_GUARD;
    struct pe_defer *d=pe_defer_lookup(h); if (!d || !pe_window_lookup(hwnd,false)) return NULL;
    if (d->count==d->capacity) {
        size_t cap=d->capacity*2;
        if (cap>65536) { SetLastError(8); return NULL; }
        struct pe_defer_item *v=pe_raw_map(cap*sizeof(*v)); if (!v) { SetLastError(8); return NULL; }
        memcpy(v,d->items,d->count*sizeof(*v));raw_syscall3(SYS_munmap,(long)d->items,d->capacity*sizeof(*v),0);
        d->items=v;d->capacity=cap;
    }
    d->items[d->count++]=(struct pe_defer_item){hwnd,after,x,y,cx,cy,flags};return d;
}
__attribute__((ms_abi)) BOOL EndDeferWindowPos(HANDLE h)
{
    struct pe_defer *d;
    { PE_STATE_GUARD; d=pe_defer_lookup(h); if (!d) return 0;
      struct pe_defer **p=&pe_defers; while (*p!=d) p=&(*p)->next; *p=d->next; }
    BOOL ok=1;
    for (size_t i=0;i<d->count;i++) {
        struct pe_defer_item *a=&d->items[i];
        if (!SetWindowPos(a->hwnd,a->after,a->x,a->y,a->width,a->height,a->flags)) { ok=0; break; }
    }
    raw_syscall3(SYS_munmap,(long)d->items,d->capacity*sizeof(*d->items),0);
    raw_syscall3(SYS_munmap,(long)d,sizeof(*d),0); return ok;
}
