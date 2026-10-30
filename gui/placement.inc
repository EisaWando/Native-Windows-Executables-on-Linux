struct pe_window_placement {
    DWORD length,flags,show;
    int32_t min_x,min_y,max_x,max_y;
    struct pe_rect normal;
};
_Static_assert(sizeof(struct pe_window_placement)==44,"WINDOWPLACEMENT layout");
__attribute__((ms_abi)) BOOL GetWindowPlacement(HANDLE hwnd,struct pe_window_placement *out)
{
    if(!out || out->length!=sizeof(*out)) { SetLastError(87);return 0; }
    PE_STATE_GUARD;struct pe_window *w=pe_window_lookup(hwnd,false);if(!w)return 0;
    struct pe_rect normal=w->placement_set?w->placement_normal:(struct pe_rect){w->x,w->y,w->x+w->width,w->y+w->height};
    *out=(struct pe_window_placement){sizeof(*out),0,w->placement_show?w->placement_show:1,
        w->placement_min_x,w->placement_min_y,w->placement_max_x,w->placement_max_y,normal};return 1;
}
__attribute__((ms_abi)) BOOL SetWindowPlacement(HANDLE hwnd,const struct pe_window_placement *p)
{
    if(!p || p->length!=sizeof(*p) || p->show>11 || (p->flags&~7u)) { SetLastError(87);return 0; }
    struct pe_window *w=pe_window_lookup(hwnd,true);if(!w)return 0;
    int64_t width=(int64_t)p->normal.right-p->normal.left,height=(int64_t)p->normal.bottom-p->normal.top;
    if(width<0 || height<0 || width>INT32_MAX || height>INT32_MAX) { SetLastError(87);return 0; }
    {
        PE_STATE_GUARD;w->placement_normal=p->normal;w->placement_set=true;
        if(p->flags&1) { w->placement_min_x=p->min_x;w->placement_min_y=p->min_y; }
        w->placement_max_x=p->max_x;w->placement_max_y=p->max_y;
        w->placement_show=(p->show==2 || p->show==6 || p->show==7)?2:p->show==3?3:1;
    }
    /* A locked window cannot move, but its requested size must still be applied. */
    DWORD positioning_flags=4|0x10;
    if(pe_update_locked(hwnd)) positioning_flags|=2;
    if(!SetWindowPos(hwnd,NULL,p->normal.left,p->normal.top,(int)width,(int)height,positioning_flags)) return 0;
    if(p->show || w->visible) {
        SetLastError(0);ShowWindow(hwnd,p->show);
        if(p->show && !w->visible) return 0;
    }
    return 1;
}
