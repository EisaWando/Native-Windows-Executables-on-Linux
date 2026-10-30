/* Exact update geometry, shared by paint delivery, validation and HRGN APIs.
 * Callbacks / cross-thread SendMessage always run outside the bridge lock. */
__attribute__((ms_abi)) int GetRgnBox(HANDLE h, struct pe_rect *box)
{
    PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GDI_REGION);
    if (!o) return 0;
    if (!box) { SetLastError(87); return 0; }
    return pe_region_box(&o->region, box);
}
__attribute__((ms_abi)) BOOL PtInRegion(HANDLE h, int x, int y)
{
    PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GDI_REGION);
    return o && pe_region_contains(&o->region, x, y);
}
__attribute__((ms_abi)) int CombineRgn(HANDLE dst, HANDLE a, HANDLE b, int mode)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_gui_lookup(dst, PE_GDI_REGION);
    struct pe_gui_object *s = pe_gui_lookup(a, PE_GDI_REGION), *t = NULL;
    if (!d || !s) return 0;
    if (mode < 1 || mode > 5) { SetLastError(87); return 0; }
    if (mode != 5 && !(t = pe_gui_lookup(b, PE_GDI_REGION))) return 0;
    if (!pe_region_combine(&d->region, &s->region, t ? &t->region : NULL, mode)) return 0;
    struct pe_rect box;
    return pe_region_box(&d->region, &box);
}
static bool pe_dc_clip_init(struct pe_gui_object *d)
{
    if (d->clip_active) return true;
    struct pe_rect box;
    if (!GetClipBox(pe_gui_handle(d), &box) || !pe_region_rect(&d->region, box)) return false;
    d->clip_active = true;
    return true;
}
__attribute__((ms_abi)) BOOL PtVisible(HANDLE dc, int x, int y)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    if (d->clip_active) return pe_region_contains(&d->region, x, y);
    struct pe_rect box;
    if (!GetClipBox(dc, &box)) return 0;
    return x >= box.left && x < box.right && y >= box.top && y < box.bottom;
}
static struct pe_rect pe_update_client(struct pe_window *w)
{
    int height = w->height - pe_window_menu_height(w);
    return (struct pe_rect){0, 0, w->width > 0 ? w->width : 0, height > 0 ? height : 0};
}
static void pe_invalidate_full(struct pe_window *w)
{
    PE_STATE_GUARD;
    if (pe_region_rect(&w->update, pe_update_client(w))) w->invalid = w->update.count != 0;
}
static BOOL pe_update_change(struct pe_window *w, const struct pe_region *area,
                             bool validate, bool erase)
{
    struct pe_rect box = pe_update_client(w);
    struct pe_region client = { &box, pe_rect_nonempty(box) ? 1 : 0, 0 }, clipped = {0};
    if (!area) area = &client;
    if (!pe_region_combine(&clipped, area, &client, 1)) return 0;
    bool nonempty = clipped.count != 0;
    bool ok = pe_region_combine(&w->update, &w->update, &clipped, validate ? 4 : 2);
    pe_region_clear(&clipped);
    if (!ok) return 0;
    w->invalid = w->update.count != 0;
    if (!w->invalid) w->erase = false;
    else if (!validate && nonempty && erase) w->erase = true;
    return 1;
}
static BOOL pe_update_rect(HANDLE hwnd, const struct pe_rect *box, bool validate, bool erase)
{
    PE_STATE_GUARD;
    struct pe_region area = { (struct pe_rect *)box, box && pe_rect_nonempty(*box) ? 1 : 0, 0 };
    if (!hwnd) { /* Desktop-wide invalidation; no fabricated desktop HWND. */
        for (struct pe_window *w = pe_windows; w; w = w->next)
            if (!w->dead && !pe_update_change(w, box ? &area : NULL, validate, erase)) return 0;
        return 1;
    }
    struct pe_window *w = pe_window_lookup(hwnd, false);
    return w && pe_update_change(w, box ? &area : NULL, validate, erase);
}
__attribute__((ms_abi)) BOOL InvalidateRect(HANDLE hwnd, const struct pe_rect *r, BOOL erase)
{ return pe_update_rect(hwnd, r, false, erase != 0); }
__attribute__((ms_abi)) BOOL ValidateRect(HANDLE hwnd, const struct pe_rect *r)
{ return pe_update_rect(hwnd, r, true, false); }
static BOOL pe_update_rgn(HANDLE hwnd, HANDLE rgn, bool validate, bool erase)
{
    PE_STATE_GUARD;
    struct pe_window *w = pe_window_lookup(hwnd, false);
    if (!w) return 0;
    struct pe_gui_object *r = rgn ? pe_gui_lookup(rgn, PE_GDI_REGION) : NULL;
    if (rgn && !r) return 0;
    return pe_update_change(w, r ? &r->region : NULL, validate, erase);
}
__attribute__((ms_abi)) BOOL InvalidateRgn(HANDLE hwnd, HANDLE rgn, BOOL erase)
{ return pe_update_rgn(hwnd, rgn, false, erase != 0); }
__attribute__((ms_abi)) BOOL ValidateRgn(HANDLE hwnd, HANDLE rgn)
{ return pe_update_rgn(hwnd, rgn, true, false); }
/* Consume the pending erase before calling application code; re-invalidations
 * from WM_ERASEBKGND therefore survive. GetUpdate* never validates geometry. */
static void pe_update_erase(struct pe_window *w)
{
    HANDLE dc;
    {
        PE_STATE_GUARD;
        if (w->dead || !w->invalid || !w->erase) return;
        dc = GetDC(w->handle);
        struct pe_gui_object *d = dc ? pe_dc_lookup(dc) : NULL;
        if (!d) return;
        if (!pe_region_copy(&d->region, &w->update)) { ReleaseDC(w->handle, dc); return; }
        d->clip_active = true;
        w->erase = false;
    }
    pe_ui_send(w->handle, 0x14, (uintptr_t)dc, 0);
    ReleaseDC(w->handle, dc);
}
__attribute__((ms_abi)) int GetUpdateRgn(HANDLE hwnd, HANDLE rgn, BOOL erase)
{
    struct pe_window *w;
    {
        PE_STATE_GUARD;
        w = pe_window_lookup(hwnd, false);
        if (!w || !pe_gui_lookup(rgn, PE_GDI_REGION)) return 0;
    }
    if (erase) pe_update_erase(w);
    PE_STATE_GUARD;
    w = pe_window_lookup(hwnd, false);
    struct pe_gui_object *r = pe_gui_lookup(rgn, PE_GDI_REGION);
    if (!w || !r || !pe_region_copy(&r->region, &w->update)) return 0;
    struct pe_rect box;
    return pe_region_box(&r->region, &box);
}
__attribute__((ms_abi)) BOOL GetUpdateRect(HANDLE hwnd, struct pe_rect *rect, BOOL erase)
{
    struct pe_window *w = pe_window_lookup(hwnd, false);
    if (!w) return 0;
    if (erase) pe_update_erase(w);
    PE_STATE_GUARD;
    if (!(w = pe_window_lookup(hwnd, false))) return 0;
    struct pe_rect box;
    int type = pe_region_box(&w->update, &box);
    if (rect) *rect = box;
    return type != 1;
}
__attribute__((ms_abi)) BOOL UpdateWindow(HANDLE hwnd)
{
    bool invalid;
    {
        PE_STATE_GUARD;
        struct pe_window *w = pe_window_lookup(hwnd, false);
        if (!w) return 0;
        invalid = w->invalid;
    }
    if (invalid) pe_ui_send(hwnd, 0xf, 0, 0);
    return 1;
}
__attribute__((ms_abi)) HANDLE BeginPaint(HANDLE hwnd, struct pe_paint *paint)
{
    struct pe_window *w;
    bool erase;
    {
        PE_STATE_GUARD;
        w = pe_window_lookup(hwnd, true);
        if (!w) return NULL;
        if (!paint) { SetLastError(87); return NULL; }
        if (w->paint_dc) { SetLastError(170); return NULL; }
        memset(paint, 0, sizeof(*paint));
        HANDLE dc = GetDC(hwnd);
        if (!dc) return NULL;
        struct pe_gui_object *d = pe_dc_lookup(dc);
        /* Transfer ownership: allocations cannot fail during validation. */
        d->region = w->update; w->update = (struct pe_region){0}; d->clip_active = true;
        if(pe_update_locked(hwnd)) pe_region_clear(&d->region);
        paint->dc = w->paint_dc = dc;
        pe_region_box(&d->region, &paint->rect);
        erase = w->invalid && w->erase;
        w->invalid = w->erase = false;
    }
    if (erase) paint->erase = !pe_ui_send(hwnd, 0x14, (uintptr_t)paint->dc, 0);
    if (w->dead) { SetLastError(1400); return NULL; }
    return paint->dc;
}
__attribute__((ms_abi)) BOOL EndPaint(HANDLE hwnd, const struct pe_paint *paint)
{
    struct pe_window *w = pe_window_lookup(hwnd, true);
    if (!w) return 0;
    if (!paint || !w->paint_dc || paint->dc != w->paint_dc) { SetLastError(87); return 0; }
    HANDLE dc = w->paint_dc;
    { PE_STATE_GUARD; w->paint_dc = NULL; }
    /* Do not validate new invalidations made since BeginPaint. */
    return ReleaseDC(hwnd, dc);
}
__attribute__((ms_abi)) int ExcludeUpdateRgn(HANDLE dc, HANDLE hwnd)
{
    PE_STATE_GUARD;
    struct pe_window *w = pe_window_lookup(hwnd, false);
    if (!w) return 0;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d || !pe_dc_clip_init(d)) return 0;
    if (!pe_region_combine(&d->region, &d->region, &w->update, 4)) return 0;
    struct pe_rect box;
    return pe_region_box(&d->region, &box);
}
/* Recursive geometry processing before message delivery. The window list and
 * window addresses are stable; callbacks may destroy children, so recheck them. */
static BOOL pe_redraw_tree(struct pe_window *w, const struct pe_region *area, DWORD flags)
{
    if ((flags & 9) && !pe_update_change(w, area, (flags & 8) != 0, (flags & 4) != 0)) return 0;
    if (flags & 0x20) w->erase = false;
    bool children = (flags & 0x80) || (!(flags & 0x40) && !(w->style & 0x02000000u));
    if (!children) return 1;
    for (struct pe_window *c = pe_windows; c; c = c->next) {
        if (c->dead || c->parent != w->handle || !(c->style & 0x40000000u)) continue;
        struct pe_region shifted = {0};
        if (area) {
            struct pe_rect parent_box = pe_update_client(w);
            for (size_t i = 0; i < area->count; i++) {
                struct pe_rect b = pe_rect_intersect(area->rects[i], parent_box);
                if (!pe_rect_nonempty(b)) continue;
                int64_t l=(int64_t)b.left-c->x, r=(int64_t)b.right-c->x;
                int64_t t=(int64_t)b.top-c->y, bottom=(int64_t)b.bottom-c->y;
                struct pe_rect cb=pe_update_client(c);
                if (l < 0) l=0; if (t < 0) t=0;
                if (r > cb.right) r=cb.right; if (bottom > cb.bottom) bottom=cb.bottom;
                if (l >= r || t >= bottom) continue;
                if (!pe_region_append(&shifted, (struct pe_rect){l,t,r,bottom})) { pe_region_clear(&shifted); return 0; }
            }
        }
        BOOL ok = pe_redraw_tree(c, area ? &shifted : NULL, flags);
        pe_region_clear(&shifted);
        if (!ok) return 0;
    }
    return 1;
}
static void pe_redraw_deliver(struct pe_window *w, DWORD flags)
{
    if (w->dead) return;
    if (flags & 0x200) pe_update_erase(w);
    if ((flags & 0x100) && !w->dead) UpdateWindow(w->handle);
    if ((flags & 0x80) || (!(flags & 0x40) && !(w->style & 0x02000000u)))
        for (struct pe_window *c=pe_windows; c; c=c->next)
            if (!c->dead && c->parent==w->handle && (c->style & 0x40000000u)) pe_redraw_deliver(c,flags);
}
__attribute__((ms_abi)) BOOL RedrawWindow(HANDLE hwnd, const struct pe_rect *rect, HANDLE rgn, DWORD flags)
{
    /* Frame/internal-paint flags need non-client/internal-paint state; don't fake it. */
    if (flags & ~(1u|4u|8u|0x20u|0x40u|0x80u|0x100u|0x200u)) { SetLastError(50); return 0; }
    struct pe_window *w;
    {
        PE_STATE_GUARD;
        w = pe_window_lookup(hwnd, false);
        if (!w) return 0;
        struct pe_gui_object *r = rgn ? pe_gui_lookup(rgn, PE_GDI_REGION) : NULL;
        if (rgn && !r) return 0;
        struct pe_region a = {(struct pe_rect *)rect, rect && pe_rect_nonempty(*rect) ? 1 : 0, 0};
        if (!pe_redraw_tree(w, r ? &r->region : rect ? &a : NULL, flags)) return 0;
    }
    pe_redraw_deliver(w, flags);
    return 1;
}
