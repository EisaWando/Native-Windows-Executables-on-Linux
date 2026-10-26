/* bridge/dc_saved.inc — device context save/restore, mapping mode, viewport/window origin and clipping regions */

struct pe_dc_extra {
    int vpx, vpy;
    int wox, woy;
    int map_mode;
    int clip_left, clip_top, clip_right, clip_bottom;
    bool has_clip;
};
static struct pe_dc_extra *pe_dc_extras;

static struct pe_dc_extra *pe_dc_extra_get(unsigned slot)
{
    if (!pe_dc_extras) {
        pe_dc_extras = (struct pe_dc_extra *)pe_raw_map(PE_GUI_MAX_SLOTS * sizeof(struct pe_dc_extra));
        if (!pe_dc_extras) return NULL;
    }
    if (slot >= PE_GUI_MAX_SLOTS) return NULL;
    struct pe_dc_extra *ex = &pe_dc_extras[slot];
    if (ex->map_mode == 0) ex->map_mode = 1; /* MM_TEXT default */
    return ex;
}

struct pe_saved_dc {
    HANDLE dc;
    int id;
    HANDLE pen, brush, font, bitmap, palette;
    DWORD text_color, background;
    int background_mode, x, y, character_extra;
    DWORD text_align;
    int vpx, vpy;
    int wox, woy;
    int map_mode;
    int clip_left, clip_top, clip_right, clip_bottom;
    bool has_clip;
    struct pe_saved_dc *prev;
};
static struct pe_saved_dc *pe_saved_dc_head;
static int pe_save_dc_id = 0;

__attribute__((ms_abi))
int SaveDC(HANDLE dc)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_saved_dc *s = (struct pe_saved_dc *)pe_raw_map(sizeof(*s));
    if (!s) { SetLastError(8); return 0; }
    s->dc = dc;
    s->id = ++pe_save_dc_id;
    s->pen = d->u.dc.pen;
    s->brush = d->u.dc.brush;
    s->font = d->u.dc.font;
    s->bitmap = d->u.dc.bitmap;
    s->palette = d->u.dc.palette;
    s->text_color = d->u.dc.text_color;
    s->background = d->u.dc.background;
    s->background_mode = d->u.dc.background_mode;
    s->x = d->u.dc.x;
    s->y = d->u.dc.y;
    s->character_extra = d->u.dc.character_extra;
    s->text_align = d->u.dc.text_align;

    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (ex) {
        s->vpx = ex->vpx; s->vpy = ex->vpy;
        s->wox = ex->wox; s->woy = ex->woy;
        s->map_mode = ex->map_mode;
        s->clip_left = ex->clip_left; s->clip_top = ex->clip_top;
        s->clip_right = ex->clip_right; s->clip_bottom = ex->clip_bottom;
        s->has_clip = ex->has_clip;
    } else {
        s->map_mode = 1;
    }

    HANDLE objs[] = { s->pen, s->brush, s->font, s->bitmap, s->palette };
    for (int i = 0; i < 5; i++) {
        struct pe_gui_object *o = pe_gui_lookup(objs[i], 0);
        if (o) o->selected++;
    }

    s->prev = pe_saved_dc_head;
    pe_saved_dc_head = s;
    return s->id;
}

__attribute__((ms_abi))
BOOL RestoreDC(HANDLE dc, int nSavedDC)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d || nSavedDC == 0) { SetLastError(87); return 0; }

    struct pe_saved_dc *target = NULL;
    if (nSavedDC < 0) {
        int count = -nSavedDC;
        for (struct pe_saved_dc *cur = pe_saved_dc_head; cur; cur = cur->prev) {
            if (cur->dc == dc) {
                if (--count == 0) { target = cur; break; }
            }
        }
    } else {
        for (struct pe_saved_dc *cur = pe_saved_dc_head; cur; cur = cur->prev) {
            if (cur->dc == dc && cur->id == nSavedDC) { target = cur; break; }
        }
    }
    if (!target) { SetLastError(87); return 0; }

    HANDLE cur_objs[] = { d->u.dc.pen, d->u.dc.brush, d->u.dc.font, d->u.dc.bitmap, d->u.dc.palette };
    for (int i = 0; i < 5; i++) {
        struct pe_gui_object *o = pe_gui_lookup(cur_objs[i], 0);
        if (o) o->selected--;
    }
    d->u.dc.pen = target->pen;
    d->u.dc.brush = target->brush;
    d->u.dc.font = target->font;
    d->u.dc.bitmap = target->bitmap;
    d->u.dc.palette = target->palette;

    d->u.dc.text_color = target->text_color;
    d->u.dc.background = target->background;
    d->u.dc.background_mode = target->background_mode;
    d->u.dc.x = target->x;
    d->u.dc.y = target->y;
    d->u.dc.character_extra = target->character_extra;
    d->u.dc.text_align = target->text_align;

    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (ex) {
        ex->vpx = target->vpx; ex->vpy = target->vpy;
        ex->wox = target->wox; ex->woy = target->woy;
        ex->map_mode = target->map_mode;
        ex->clip_left = target->clip_left; ex->clip_top = target->clip_top;
        ex->clip_right = target->clip_right; ex->clip_bottom = target->clip_bottom;
        ex->has_clip = target->has_clip;
    }

    struct pe_saved_dc **pcur = &pe_saved_dc_head;
    while (*pcur) {
        struct pe_saved_dc *cur = *pcur;
        if (cur->dc == dc) {
            *pcur = cur->prev;
            if (cur != target) {
                HANDLE objs[] = { cur->pen, cur->brush, cur->font, cur->bitmap, cur->palette };
                for (int i = 0; i < 5; i++) {
                    struct pe_gui_object *o = pe_gui_lookup(objs[i], 0);
                    if (o) o->selected--;
                }
            }
            raw_syscall3(SYS_munmap, (long)cur, sizeof(*cur), 0);
            if (cur == target) break;
        } else {
            pcur = &cur->prev;
        }
    }
    return 1;
}

__attribute__((ms_abi))
BOOL SetViewportOrgEx(HANDLE dc, int x, int y, struct pe_rect *lppt)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    if (lppt) {
        /* POINT layout: 2 x 32-bit ints */
        int32_t *pt = (int32_t *)lppt;
        pt[0] = ex->vpx; pt[1] = ex->vpy;
    }
    ex->vpx = x;
    ex->vpy = y;
    return 1;
}

__attribute__((ms_abi))
BOOL GetViewportOrgEx(HANDLE dc, struct pe_rect *lppt)
{
    PE_STATE_GUARD;
    if (!lppt) { SetLastError(87); return 0; }
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    int32_t *pt = (int32_t *)lppt;
    pt[0] = ex->vpx; pt[1] = ex->vpy;
    return 1;
}

__attribute__((ms_abi))
BOOL OffsetViewportOrgEx(HANDLE dc, int dx, int dy, struct pe_rect *lppt)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    if (lppt) {
        int32_t *pt = (int32_t *)lppt;
        pt[0] = ex->vpx; pt[1] = ex->vpy;
    }
    ex->vpx += dx;
    ex->vpy += dy;
    return 1;
}

__attribute__((ms_abi))
BOOL SetWindowOrgEx(HANDLE dc, int x, int y, struct pe_rect *lppt)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    if (lppt) {
        int32_t *pt = (int32_t *)lppt;
        pt[0] = ex->wox; pt[1] = ex->woy;
    }
    ex->wox = x;
    ex->woy = y;
    return 1;
}

__attribute__((ms_abi))
BOOL GetWindowOrgEx(HANDLE dc, struct pe_rect *lppt)
{
    PE_STATE_GUARD;
    if (!lppt) { SetLastError(87); return 0; }
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    int32_t *pt = (int32_t *)lppt;
    pt[0] = ex->wox; pt[1] = ex->woy;
    return 1;
}

__attribute__((ms_abi))
int SetMapMode(HANDLE dc, int mode)
{
    PE_STATE_GUARD;
    if (mode < 1 || mode > 8) { SetLastError(87); return 0; }
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    int old = ex->map_mode ? ex->map_mode : 1;
    ex->map_mode = mode;
    return old;
}

__attribute__((ms_abi))
int GetMapMode(HANDLE dc)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    return (ex && ex->map_mode) ? ex->map_mode : 1;
}

__attribute__((ms_abi))
int GetClipBox(HANDLE dc, struct pe_rect *rc)
{
    PE_STATE_GUARD;
    if (!rc) { SetLastError(87); return 0; }
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (ex && ex->has_clip) {
        rc->left = ex->clip_left;
        rc->top = ex->clip_top;
        rc->right = ex->clip_right;
        rc->bottom = ex->clip_bottom;
        return (rc->left >= rc->right || rc->top >= rc->bottom) ? 1 : 2;
    }
    if (d->type == PE_GDI_MEMDC) {
        struct pe_gui_object *b = pe_gui_lookup(d->u.dc.bitmap, PE_GDI_BITMAP);
        rc->left = 0; rc->top = 0;
        rc->right = b ? b->u.bitmap.width : 1;
        rc->bottom = b ? b->u.bitmap.height : 1;
        return 2;
    }
    if (d->type == PE_GDI_DC && d->u.dc.owner) {
        struct pe_window *w = pe_window_lookup(d->u.dc.owner, true);
        if (w) {
            rc->left = 0; rc->top = 0;
            rc->right = w->width;
            rc->bottom = w->height;
            return 2;
        }
    }
    rc->left = 0; rc->top = 0; rc->right = 1920; rc->bottom = 1080;
    return 2;
}

__attribute__((ms_abi))
int IntersectClipRect(HANDLE dc, int left, int top, int right, int bottom)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    if (!ex->has_clip) {
        struct pe_rect rc;
        GetClipBox(dc, &rc);
        ex->clip_left = rc.left;
        ex->clip_top = rc.top;
        ex->clip_right = rc.right;
        ex->clip_bottom = rc.bottom;
        ex->has_clip = true;
    }
    if (left > ex->clip_left) ex->clip_left = left;
    if (top > ex->clip_top) ex->clip_top = top;
    if (right < ex->clip_right) ex->clip_right = right;
    if (bottom < ex->clip_bottom) ex->clip_bottom = bottom;
    if (ex->clip_left >= ex->clip_right || ex->clip_top >= ex->clip_bottom)
        return 1;
    return 2;
}

__attribute__((ms_abi))
int ExcludeClipRect(HANDLE dc, int left, int top, int right, int bottom)
{
    PE_STATE_GUARD;
    (void)left; (void)top; (void)right; (void)bottom;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    return 2;
}

__attribute__((ms_abi))
int SelectClipRgn(HANDLE dc, HANDLE rgn)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_dc_extra *ex = pe_dc_extra_get(d->slot);
    if (!ex) return 0;
    if (!rgn) {
        ex->has_clip = false;
        return 2;
    }
    struct pe_gui_object *r = pe_gui_lookup(rgn, PE_GDI_REGION);
    if (r) {
        ex->clip_left = r->u.rect[0];
        ex->clip_top = r->u.rect[1];
        ex->clip_right = r->u.rect[2];
        ex->clip_bottom = r->u.rect[3];
        ex->has_clip = true;
        return 2;
    }
    return 2;
}

__attribute__((ms_abi))
HANDLE CreateRectRgn(int left, int top, int right, int bottom)
{
    PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_new(PE_GDI_REGION);
    if (!o) return NULL;
    o->u.rect[0] = left;
    o->u.rect[1] = top;
    o->u.rect[2] = right;
    o->u.rect[3] = bottom;
    return pe_gui_handle(o);
}

__attribute__((ms_abi))
HANDLE CreateRectRgnIndirect(const struct pe_rect *rc)
{
    if (!rc) { SetLastError(87); return NULL; }
    return CreateRectRgn(rc->left, rc->top, rc->right, rc->bottom);
}

__attribute__((ms_abi))
BOOL SetRectRgn(HANDLE rgn, int left, int top, int right, int bottom)
{
    PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_lookup(rgn, PE_GDI_REGION);
    if (!o) return 0;
    o->u.rect[0] = left;
    o->u.rect[1] = top;
    o->u.rect[2] = right;
    o->u.rect[3] = bottom;
    return 1;
}
