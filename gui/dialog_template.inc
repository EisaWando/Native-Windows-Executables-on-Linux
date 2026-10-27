/* Modeless dialog templates. Resource lengths are exact; indirect templates
 * have no ABI length, so guarded reads stop at the 1 MiB hard limit. */
#define PE_DIALOG_LIMIT (1024u * 1024u)
#define PE_DIALOG_MAX_ITEMS 4096u
#define PE_DIALOG_MAX_STRING 1024u
struct pe_dialog_item {
    uint32_t style, exstyle, id, help;
    int16_t x,y,cx,cy;
    const uint16_t *class_name, *title;
    void *extra;
};
struct pe_dialog_template {
    uint32_t style, exstyle, help;
    uint16_t count, point, weight;
    uint8_t italic, charset;
    int16_t x,y,cx,cy;
    const uint16_t *menu, *class_name, *title, *face;
    struct pe_dialog_item *items;
    unsigned char *copy;
    size_t copy_size;
};
struct pe_dialog_reader {
    uintptr_t source;
    unsigned char *copy;
    size_t size, pos;
};
/* Unlike /proc/self/mem, process_vm_readv respects PROT_NONE. Keep the
 * existing guarded reader as the actual copy after checking readability. */
static bool pe_dialog_read(struct pe_dialog_reader *r, size_t n, void *out)
{
    if (r->pos > r->size || n > r->size-r->pos ||
        r->source > UINTPTR_MAX-r->pos || n > 4096) return false;
    struct { void *base; size_t len; } local = {r->copy+r->pos,n}, remote = {(void *)(r->source+r->pos),n};
    if (n && (raw_syscall6(310, raw_syscall3(SYS_getpid,0,0,0), (long)&local,1,(long)&remote,1,0) != (long)n ||
              !diagnostic_read(r->source+r->pos, r->copy+r->pos,n))) return false;
    if (out) memcpy(out,r->copy+r->pos,n);
    r->pos += n;
    return true;
}
static bool pe_dialog_skip(struct pe_dialog_reader *r, size_t n)
{
    while (n) { size_t step=n>4096?4096:n; if (!pe_dialog_read(r,step,NULL)) return false; n-=step; }
    return true;
}
static bool pe_dialog_align(struct pe_dialog_reader *r)
{
    size_t pad = (4-(r->pos & 3))&3;
    return pe_dialog_skip(r,pad);
}
static bool pe_dialog_word(struct pe_dialog_reader *r,uint16_t *v)
{ return pe_dialog_read(r,2,v); }
static bool pe_dialog_dword(struct pe_dialog_reader *r,uint32_t *v)
{ return pe_dialog_read(r,4,v); }
static bool pe_dialog_name(struct pe_dialog_reader *r,const uint16_t **result)
{
    size_t start = r->pos; uint16_t v;
    if (!pe_dialog_word(r,&v)) return false;
    if (!v) { *result=NULL; return true; }
    if (v==0xffff) { if (!pe_dialog_word(r,&v)) return false; *result=(void *)(uintptr_t)v; return true; }
    for (unsigned i=1;i<PE_DIALOG_MAX_STRING;i++) {
        if (!pe_dialog_word(r,&v)) return false;
        if (!v) { *result=(void *)(r->copy+start); return true; }
    }
    return false;
}
static void pe_dialog_template_free(struct pe_dialog_template *d)
{
    if (d->items) raw_syscall3(SYS_munmap,(long)d->items,(size_t)d->count*sizeof(*d->items),0);
    if (d->copy) raw_syscall3(SYS_munmap,(long)d->copy,d->copy_size,0);
    memset(d,0,sizeof(*d));
}
static bool pe_dialog_parse(const void *source,size_t limit,struct pe_dialog_template *d)
{
    memset(d,0,sizeof(*d));
    if (!source || !limit) { SetLastError(87); return false; }
    d->copy=pe_raw_map(limit); d->copy_size=limit;
    if (!d->copy) { SetLastError(8); return false; }
    struct pe_dialog_reader r = {(uintptr_t)source,d->copy,limit,0};
    uint16_t version, sig; bool extended;
    if (!pe_dialog_word(&r,&version) || !pe_dialog_word(&r,&sig)) goto bad;
    extended = version==1 && sig==0xffff;
    r.pos=0;
    if (extended) {
        if (!pe_dialog_word(&r,&version) || !pe_dialog_word(&r,&sig) ||
            !pe_dialog_dword(&r,&d->help) || !pe_dialog_dword(&r,&d->exstyle) ||
            !pe_dialog_dword(&r,&d->style)) goto bad;
    } else if (!pe_dialog_dword(&r,&d->style) || !pe_dialog_dword(&r,&d->exstyle)) goto bad;
    if (!pe_dialog_word(&r,&d->count) || d->count>PE_DIALOG_MAX_ITEMS ||
        !pe_dialog_read(&r,8,&d->x) || !pe_dialog_name(&r,&d->menu) ||
        !pe_dialog_name(&r,&d->class_name) || !pe_dialog_name(&r,&d->title)) goto bad;
    if (d->style & 0x40) {
        if (!pe_dialog_word(&r,&d->point)) goto bad;
        if (extended && (!pe_dialog_word(&r,&d->weight) ||
                         !pe_dialog_read(&r,1,&d->italic) || !pe_dialog_read(&r,1,&d->charset))) goto bad;
        if (!pe_dialog_name(&r,&d->face) || (uintptr_t)d->face<65536) goto bad;
    }
    if (d->count) {
        d->items=pe_raw_map((size_t)d->count*sizeof(*d->items));
        if (!d->items) { pe_dialog_template_free(d); SetLastError(8); return false; }
    }
    for (unsigned i=0;i<d->count;i++) {
        struct pe_dialog_item *it=&d->items[i]; uint16_t id=0, extra=0;
        if (!pe_dialog_align(&r)) goto bad;
        if (extended) {
            if (!pe_dialog_dword(&r,&it->help) || !pe_dialog_dword(&r,&it->exstyle) ||
                !pe_dialog_dword(&r,&it->style) || !pe_dialog_read(&r,8,&it->x) ||
                !pe_dialog_dword(&r,&it->id)) goto bad;
        } else {
            if (!pe_dialog_dword(&r,&it->style) || !pe_dialog_dword(&r,&it->exstyle) ||
                !pe_dialog_read(&r,8,&it->x) || !pe_dialog_word(&r,&id)) goto bad;
            it->id=id;
        }
        if (!pe_dialog_name(&r,&it->class_name) || !it->class_name ||
            !pe_dialog_name(&r,&it->title) || !pe_dialog_word(&r,&extra)) goto bad;
        if (extra) {
            if (!extended) { if (extra<2) goto bad; extra-=2; }
            it->extra=r.copy+r.pos;
            if (!pe_dialog_skip(&r,extra)) goto bad;
        }
    }
    return true;
bad:
    pe_dialog_template_free(d); SetLastError(87); return false;
}

static const uint16_t pe_dialog_class[]={'#','3','2','7','7','0',0};
typedef intptr_t (__attribute__((ms_abi)) *pe_dlgproc)(HANDLE,DWORD,uintptr_t,intptr_t);
__attribute__((ms_abi)) intptr_t DefDlgProcW(HANDLE hwnd,DWORD msg,uintptr_t wp,intptr_t lp)
{
    struct pe_window *w=pe_window_lookup(hwnd,false);
    if (!w) return 0;
    if (msg==0x31) return (intptr_t)w->dialog_font;
    return DefWindowProcW(hwnd,msg,wp,lp);
}
__attribute__((ms_abi)) intptr_t DefDlgProcA(HANDLE hwnd,DWORD msg,uintptr_t wp,intptr_t lp)
{ return DefDlgProcW(hwnd,msg,wp,lp); }
static __attribute__((ms_abi)) intptr_t pe_dialog_proc(HANDLE hwnd,DWORD msg,uintptr_t wp,intptr_t lp)
{
    pe_dlgproc proc=(pe_dlgproc)GetWindowLongPtrW(hwnd,8);
    if (proc) {
        intptr_t handled=proc(hwnd,msg,wp,lp);
        if (msg==0x110) return handled;
        if (handled) {
            /* These messages use the procedure's result directly. */
            if (msg==0x2e || msg==0x37 || msg==0x39 || msg==0x3b ||
                (msg>=0x132 && msg<=0x138)) return handled;
            return GetWindowLongPtrW(hwnd,0);
        }
    }
    return DefDlgProcW(hwnd,msg,wp,lp);
}
static bool pe_dialog_register(HANDLE instance)
{
    struct pe_wndclassex cls={0}; cls.size=sizeof(cls);
    if (GetClassInfoExW(instance,pe_dialog_class,&cls)) return true;
    cls=(struct pe_wndclassex){0}; cls.size=sizeof(cls); cls.instance=instance;
    cls.wndproc=pe_dialog_proc; cls.window_extra=24; cls.class_name=pe_dialog_class;
    return RegisterClassExW(&cls)!=0;
}
/* Windows uses the mean alphabet width and font height as dialog units.
 * A fixed 8x16 fallback applies to templates without a font, or when font
 * measurement fails because the host helper is unavailable. */
__attribute__((ms_abi)) DWORD GetDialogBaseUnits(void) { return 8 | (16u<<16); }
__attribute__((ms_abi)) BOOL MapDialogRect(HANDLE hwnd,struct pe_rect *r)
{
    if (!r) { SetLastError(87); return 0; }
    struct pe_window *w=pe_window_lookup(hwnd,false); if (!w) return 0;
    int bx=w->dialog_base_x ? w->dialog_base_x : 8;
    int by=w->dialog_base_y ? w->dialog_base_y : 16;
    r->left=MulDiv(r->left,bx,4); r->right=MulDiv(r->right,bx,4);
    r->top=MulDiv(r->top,by,8); r->bottom=MulDiv(r->bottom,by,8);
    return 1;
}
static HANDLE pe_dialog_font(const struct pe_dialog_template *d,int *bx,int *by)
{
    *bx=8; *by=16;
    if (!(d->style & 0x40) || !d->face) return NULL;
    struct pe_logfont lf={0}; lf.height=-MulDiv(d->point,96,72);
    lf.weight=d->weight ? d->weight : 400; lf.italic=d->italic; lf.charset=d->charset;
    for (unsigned i=0;i<31 && d->face[i];i++) lf.face[i]=d->face[i];
    HANDLE font=CreateFontIndirectW(&lf),dc=CreateCompatibleDC(NULL);
    if (!font || !dc) { if (dc) DeleteDC(dc); return font; }
    HANDLE old=SelectObject(dc,font);
    static const uint16_t alphabet[]={'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',0};
    struct pe_text_size size; struct pg_text_metrics tm;
    if (old && GetTextExtentPoint32W(dc,alphabet,52,&size) && GetTextMetricsW(dc,&tm) && size.x>0 && tm.height>0) {
        *bx=(size.x/26+1)/2; *by=tm.height;
    }
    if (old) SelectObject(dc,old);
    DeleteDC(dc); return font;
}
static const uint16_t *pe_dialog_control_class(const uint16_t *name)
{
    static const uint16_t button[]={'B','u','t','t','o','n',0}, edit[]={'E','d','i','t',0};
    static const uint16_t stat[]={'S','t','a','t','i','c',0}, list[]={'L','i','s','t','B','o','x',0};
    static const uint16_t scroll[]={'S','c','r','o','l','l','B','a','r',0}, combo[]={'C','o','m','b','o','B','o','x',0};
    switch ((uintptr_t)name) { case 0x80:return button; case 0x81:return edit; case 0x82:return stat;
        case 0x83:return list; case 0x84:return scroll; case 0x85:return combo; }
    return name;
}
static HANDLE pe_dialog_create(HANDLE instance,const void *source,size_t size,HANDLE parent,pe_dlgproc proc,intptr_t param)
{
    struct pe_dialog_template d;
    if (!pe_dialog_parse(source,size,&d)) return NULL;
    HANDLE hwnd=NULL, font=NULL, menu=NULL, first=NULL; int bx,by;
    if (!pe_dialog_register(instance)) goto done;
    font=pe_dialog_font(&d,&bx,&by);
    if ((d.style & 0x40) && !font) goto done;
    if (d.menu) { menu=LoadMenuW(instance,d.menu); if (!menu) goto done; }
    const uint16_t *cls=d.class_name ? d.class_name : pe_dialog_class;
    if ((uintptr_t)cls==0x8002) cls=pe_dialog_class;
    const uint16_t *title=d.title;
    uint16_t titlebuf[1024];
    if (title && (uintptr_t)title<65536) {
        if (!LoadStringW(instance,(unsigned)(uintptr_t)title,titlebuf,1024)) titlebuf[0]=0;
        title=titlebuf;
    }
    int x=MulDiv(d.x,bx,4), y=MulDiv(d.y,by,8), width=MulDiv(d.cx,bx,4), height=MulDiv(d.cy,by,8);
    if ((d.style & 0x800) && !(d.style & 0x40000000u)) {
        x=(GetSystemMetrics(0)-width)/2; y=(GetSystemMetrics(1)-height)/2;
    }
    hwnd=CreateWindowExW(d.exstyle,cls,title,d.style & ~0x10000000u,x,y,width,height,parent,menu,instance,NULL);
    if (!hwnd) goto done;
    menu=NULL;
    struct pe_window *w=pe_window_lookup(hwnd,true);
    if (!w) { hwnd=NULL; goto done; }
    w->dialog_font=font; font=NULL; w->dialog_base_x=bx; w->dialog_base_y=by;
    SetWindowLongPtrW(hwnd,8,(intptr_t)proc);
    if (w->dialog_font) SendMessageW(hwnd,0x30,(uintptr_t)w->dialog_font,0);
    for (unsigned i=0;i<d.count;i++) {
        struct pe_dialog_item *it=&d.items[i]; const uint16_t *childtitle=it->title;
        uint16_t text[1024];
        if (childtitle && (uintptr_t)childtitle<65536) {
            if (!LoadStringW(instance,(unsigned)(uintptr_t)childtitle,text,1024)) text[0]=0;
            childtitle=text;
        }
        HANDLE c=CreateWindowExW(it->exstyle,pe_dialog_control_class(it->class_name),childtitle,it->style,
            MulDiv(it->x,bx,4),MulDiv(it->y,by,8),MulDiv(it->cx,bx,4),MulDiv(it->cy,by,8),
            hwnd,(HANDLE)(uintptr_t)it->id,instance,it->extra);
        if (!c) { DWORD e=GetLastError(); DestroyWindow(hwnd); hwnd=NULL; SetLastError(e); goto done; }
        if (w->dialog_font) SendMessageW(c,0x30,(uintptr_t)w->dialog_font,0);
        if (!first && (it->style & 0x10000) && !(it->style & 0x08000000u) && (it->style & 0x10000000u)) first=c;
    }
    if (proc && proc(hwnd,0x110,(uintptr_t)first,param) && first && IsWindow(hwnd)) SetFocus(first);
    if (!IsWindow(hwnd)) { hwnd=NULL; goto done; }
    if (d.style & 0x10000000u) ShowWindow(hwnd,5);
done:
    if (font) DeleteObject(font);
    if (menu) DestroyMenu(menu);
    pe_dialog_template_free(&d);
    return hwnd;
}
__attribute__((ms_abi)) HANDLE CreateDialogIndirectParamW(HANDLE instance,const void *source,HANDLE parent,pe_dlgproc proc,intptr_t param)
{ return pe_dialog_create(instance,source,PE_DIALOG_LIMIT,parent,proc,param); }
__attribute__((ms_abi)) HANDLE CreateDialogIndirectParamA(HANDLE instance,const void *source,HANDLE parent,pe_dlgproc proc,intptr_t param)
{ return CreateDialogIndirectParamW(instance,source,parent,proc,param); }
__attribute__((ms_abi)) HANDLE CreateDialogParamW(HANDLE instance,const uint16_t *name,HANDLE parent,pe_dlgproc proc,intptr_t param)
{
    HANDLE resource=FindResourceW(instance,name,(void *)5); if (!resource) return NULL;
    DWORD n=SizeofResource(instance,resource); void *src=LockResource(LoadResource(instance,resource));
    if (!src || !n) { SetLastError(87); return NULL; }
    return pe_dialog_create(instance,src,n,parent,proc,param);
}
__attribute__((ms_abi)) HANDLE CreateDialogParamA(HANDLE instance,const char *name,HANDLE parent,pe_dlgproc proc,intptr_t param)
{
    uint16_t wide[1025]; const uint16_t *s=(void *)name;
    if ((uintptr_t)name>=65536) {
        if (!MultiByteToWideChar(65001,8,name,-1,wide,1025)) return NULL;
        s=wide;
    }
    return CreateDialogParamW(instance,s,parent,proc,param);
}
__attribute__((ms_abi)) intptr_t SendDlgItemMessageA(HANDLE parent,int id,DWORD msg,uintptr_t wp,intptr_t lp)
{
    if (msg==0xc) return SetDlgItemTextA(parent,id,(const char *)lp);
    if (msg==0xd) return GetDlgItemTextA(parent,id,(char *)lp,(int)wp);
    return SendDlgItemMessageW(parent,id,msg,wp,lp);
}
/* Child windows are linked newest-first. Reverse them to recover creation/tab order. */
static HANDLE pe_dialog_next(HANDLE parent,HANDLE child,BOOL previous,bool group)
{
    PE_STATE_GUARD;
    if (!pe_window_lookup(parent,false)) return NULL;
    unsigned count=0;
    for (struct pe_window *w=pe_windows;w;w=w->next)
        if (!w->dead && w->parent==parent && (w->style&0x40000000u)) count++;
    if (!count) return NULL;
    struct pe_window **items=pe_raw_map(count*sizeof(*items));
    if (!items) { SetLastError(8); return NULL; }
    unsigned n=count;
    for (struct pe_window *w=pe_windows;w;w=w->next)
        if (!w->dead && w->parent==parent && (w->style&0x40000000u)) items[--n]=w;
    int start=-1;
    for (unsigned i=0;i<count;i++) if (items[i]->handle==child) start=i;
    if (child && start<0) { raw_syscall3(SYS_munmap,(long)items,count*sizeof(*items),0); SetLastError(1400); return NULL; }
    int lo=0,hi=(int)count;
    if (group && start>=0) {
        lo=start; while (lo>0 && !(items[lo]->style&0x20000)) lo--;
        hi=start+1; while (hi<(int)count && !(items[hi]->style&0x20000)) hi++;
    }
    int idx=start<0 ? (previous ? lo : hi-1) : start;
    HANDLE result=NULL;
    for (int k=0;k<hi-lo;k++) {
        idx=previous ? (idx==lo?hi-1:idx-1) : (idx+1==hi?lo:idx+1);
        DWORD style=items[idx]->style;
        if ((style&0x10000000u) && !(style&0x08000000u) && (group || (style&0x10000))) { result=items[idx]->handle; break; }
    }
    raw_syscall3(SYS_munmap,(long)items,count*sizeof(*items),0);
    return result;
}
__attribute__((ms_abi)) HANDLE GetNextDlgTabItem(HANDLE h,HANDLE c,BOOL p) { return pe_dialog_next(h,c,p,false); }
__attribute__((ms_abi)) HANDLE GetNextDlgGroupItem(HANDLE h,HANDLE c,BOOL p) { return pe_dialog_next(h,c,p,true); }
