/* Window geometry is in pixels: top-level positions are screen-relative;
 * child positions are relative to the parent's client origin. */
static bool pe_coord_origin(struct pe_window *w,struct pe_point *out)
{
    int64_t x=0,y=0;
    for (unsigned depth=0; w; depth++) {
        if (depth>=128) { SetLastError(1400); return false; }
        x+=w->x; y+=w->y+pe_window_menu_height(w);
        if (!(w->style&0x40000000u) && !w->reparented) break;
        if (!w->parent) break;
        w=pe_window_lookup(w->parent,false);
        if (!w) return false;
    }
    if (x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX) { SetLastError(87); return false; }
    *out=(struct pe_point){x,y}; return true;
}
__attribute__((ms_abi)) BOOL ScreenToClient(HANDLE hwnd,struct pe_point *point)
{
    PE_STATE_GUARD;
    struct pe_window *w=pe_window_lookup(hwnd,false); if (!w) return 0;
    if (!point) { SetLastError(87); return 0; }
    struct pe_point origin; if (!pe_coord_origin(w,&origin)) return 0;
    point->x=(uint32_t)point->x-(uint32_t)origin.x;
    point->y=(uint32_t)point->y-(uint32_t)origin.y;
    return 1;
}
__attribute__((ms_abi)) BOOL ClientToScreen(HANDLE hwnd,struct pe_point *point)
{
    PE_STATE_GUARD;
    struct pe_window *w=pe_window_lookup(hwnd,false); if (!w) return 0;
    if (!point) { SetLastError(87); return 0; }
    struct pe_point origin; if (!pe_coord_origin(w,&origin)) return 0;
    point->x=(uint32_t)point->x+(uint32_t)origin.x;
    point->y=(uint32_t)point->y+(uint32_t)origin.y;
    return 1;
}
__attribute__((ms_abi)) int MapWindowPoints(HANDLE from,HANDLE to,struct pe_point *points,unsigned count)
{
    PE_STATE_GUARD;
    struct pe_point a={0},b={0};
    struct pe_window *fw=from?pe_window_lookup(from,false):NULL, *tw=to?pe_window_lookup(to,false):NULL;
    if ((from&&!fw) || (to&&!tw)) return 0;
    if (count && !points) { SetLastError(87); return 0; }
    if ((fw&&!pe_coord_origin(fw,&a)) || (tw&&!pe_coord_origin(tw,&b))) return 0;
    int32_t dx=(uint32_t)a.x-(uint32_t)b.x,dy=(uint32_t)a.y-(uint32_t)b.y;
    for (unsigned i=0;i<count;i++) {
        points[i].x=(uint32_t)points[i].x+(uint32_t)dx;
        points[i].y=(uint32_t)points[i].y+(uint32_t)dy;
    }
    return (uint16_t)dx | ((uint32_t)(uint16_t)dy<<16);
}
__attribute__((ms_abi)) BOOL GetCursorPos(struct pe_point *p)
{
    PE_STATE_GUARD; if (!p) { SetLastError(87); return 0; } *p=pe_cursor; return 1;
}
__attribute__((ms_abi)) BOOL SetCursorPos(int x,int y)
{
    /* Logical cursor only; TODO: move the native X11 pointer on request. */
    PE_STATE_GUARD; pe_cursor=(struct pe_point){x,y}; return 1;
}
__attribute__((ms_abi)) DWORD GetMessagePos(void)
{
    struct pe_ui *u=pe_ui_current(); if (!u) return 0;
    PE_STATE_GUARD; return (uint16_t)u->message_x | ((uint32_t)(uint16_t)u->message_y<<16);
}
__attribute__((ms_abi)) int GetMessageTime(void)
{
    struct pe_ui *u=pe_ui_current(); if (!u) return 0;
    PE_STATE_GUARD; return u->message_time;
}
static bool pe_coord_inside(int64_t x,int64_t y,int64_t left,int64_t top,int width,int height)
{ return width>0 && height>0 && x>=left && y>=top && x<left+width && y<top+height; }
static HANDLE pe_coord_child(HANDLE parent,struct pe_point p,unsigned flags,bool real)
{
    if (flags&~7u) { SetLastError(87); return NULL; }
    struct pe_window *pw=pe_window_lookup(parent,false); if (!pw) return NULL;
    struct pe_point origin;
    { PE_STATE_GUARD; if (!pe_coord_origin(pw,&origin)) return NULL; }
    if (!pe_coord_inside(p.x,p.y,0,0,pw->width,pw->height-pe_window_menu_height(pw))) return NULL;
    for (struct pe_window *w=pe_windows;w;w=w->next) {
        HANDLE candidate=NULL; unsigned builtin=0; int64_t sx=(int64_t)origin.x+p.x,sy=(int64_t)origin.y+p.y;
        {
            PE_STATE_GUARD;
            if (!w->dead && w->parent==parent && ((w->style&0x40000000u) || w->reparented) &&
                (!(flags&1) || w->visible) && (!(flags&2) || !(w->style&0x08000000u)) &&
                (!(flags&4) || !(w->exstyle&0x20u)) &&
                pe_coord_inside(p.x,p.y,w->x,w->y,w->width,w->height)) {
                candidate=w->handle; builtin=w->cls->builtin;
            }
        }
        if (!candidate) continue;
        /* RealChildWindowFromPoint treats HTTRANSPARENT as pass-through except
         * for Static controls. Run application procedures outside the lock. */
        if (real && builtin!=3 && pe_ui_send(candidate,0x84,0,(uint16_t)sx|((uint32_t)(uint16_t)sy<<16))==-1) continue;
        return candidate;
    }
    return parent;
}
__attribute__((ms_abi)) HANDLE ChildWindowFromPointEx(HANDLE parent,uint64_t packed,unsigned flags)
{ struct pe_point p={(int32_t)packed,(int32_t)(packed>>32)}; return pe_coord_child(parent,p,flags,false); }
__attribute__((ms_abi)) HANDLE ChildWindowFromPoint(HANDLE parent,uint64_t packed)
{ return ChildWindowFromPointEx(parent,packed,0); }
__attribute__((ms_abi)) HANDLE RealChildWindowFromPoint(HANDLE parent,uint64_t packed)
{ struct pe_point p={(int32_t)packed,(int32_t)(packed>>32)}; return pe_coord_child(parent,p,1,true); }
__attribute__((ms_abi)) HANDLE WindowFromPoint(uint64_t packed)
{
    struct pe_point p={(int32_t)packed,(int32_t)(packed>>32)};
    for (struct pe_window *w=pe_windows;w;w=w->next) {
        struct pe_point origin;
        {
            PE_STATE_GUARD;
            if (w->dead || !w->visible || (((w->style&0x40000000u) || w->reparented) && w->parent) || (w->style&0x08000000u)) continue;
            if (!pe_coord_origin(w,&origin)) continue;
            if (!pe_coord_inside(p.x,p.y,origin.x,origin.y-pe_window_menu_height(w),w->width,w->height)) continue;
        }
        HANDLE result=w->handle;
        for (unsigned depth=0;depth<128;depth++) {
            struct pe_point relative=p;
            if (!ScreenToClient(result,&relative)) break;
            HANDLE next=pe_coord_child(result,relative,3,false);
            if (!next || next==result) break;
            result=next;
        }
        return result;
    }
    return NULL;
}
