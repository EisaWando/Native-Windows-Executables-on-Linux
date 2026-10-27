/* ExtTextOutW / ExtTextOutA.
 *
 * The native helper renders the string with FreeType (PG_TEXT_OUT) and can fill
 * a background cell per glyph (background mode 2), so:
 *  - ETO_OPAQUE fills the clip rectangle with the DC background colour first,
 *  - ETO_CLIPPED intersects the DC clip region with the rectangle before the
 *    text packet is sent,
 *  - a non-NULL lpDx places every character individually, which is exactly what
 *    the documented intercharacter spacing means.
 *
 * Honest limits: ETO_GLYPH_INDEX is rejected with ERROR_INVALID_FUNCTION
 * because the bridge has no glyph-index to code-point mapping for the host
 * font, and drawing it as text would put wrong glyphs on the screen. The
 * right-to-left flag ETO_RTLREADING succeeds but does not reorder runs, which
 * needs bidirectional layout. */

#define PE_ETO_OPAQUE      0x0002u
#define PE_ETO_CLIPPED     0x0004u
#define PE_ETO_GLYPH_INDEX 0x0010u

static BOOL pe_text_out_wide(HANDLE dc, int x, int y, uint32_t options,
                             const struct pe_rect *rect, const uint16_t *text,
                             int count, const int32_t *dx)
{
    if (count < 0 || (!text && count)) { SetLastError(87); return 0; }
    if (options & PE_ETO_GLYPH_INDEX) { SetLastError(1); return 0; }
    if (!count) return 1;
    struct pe_gui_object *d;
    struct pe_window *w = pe_drawing_window(dc, &d);
    if (!w) return 0;
    DWORD text_color, background;
    int height;
    struct pe_logfont f;
    {
        PE_STATE_GUARD;
        struct pe_gui_object *font = pe_gui_lookup(d->u.dc.font, PE_GDI_FONT);
        if (!font) return 0;
        f = font->u.font;
        height = f.height;
        text_color = pe_color_rgb(d->u.dc.text_color);
        background = pe_color_rgb(d->u.dc.background);
    }
    int origin = pe_window_menu_height(w);
    /* ETO_OPAQUE paints the clipping rectangle with the background colour. */
    if ((options & PE_ETO_OPAQUE) && rect) {
        struct pg_packet fill = { .op = PG_FILL, .id = (uintptr_t)w->handle,
                                  .a = rect->left, .b = rect->top + origin,
                                  .c = rect->right, .d = rect->bottom + origin,
                                  .color = background };
        if (!pe_ui_rpc(w->ui, &fill)) return 0;
    }
    /* Install the DC clip, intersected with the ETO_CLIPPED rectangle. */
    bool active;
    struct pe_region clip = {0};
    {
        PE_STATE_GUARD;
        active = d->clip_active;
        if (active && !pe_region_copy(&clip, &d->region)) return 0;
    }
    if ((options & PE_ETO_CLIPPED) && rect) {
        struct pe_region area = { (struct pe_rect *)rect, 1, 0 }, limited = {0};
        BOOL ok = active ? pe_region_combine(&limited, &clip, &area, 1)
                         : pe_region_copy(&limited, &area);
        pe_region_clear(&clip);
        clip = limited;
        active = true;
        if (!ok) { pe_region_clear(&clip); return 0; }
    }
    struct pg_packet set = { .op = PG_CLIP, .id = (uintptr_t)w->handle,
                             .flags = active ? 1 : 0 };
    BOOL ok = pe_ui_rpc(w->ui, &set);
    for (size_t i = 0; ok && i < clip.count; i++) {
        struct pe_rect r = pe_rect_intersect(clip.rects[i],
                (struct pe_rect){0, 0, w->width, w->height - origin});
        if (!pe_rect_nonempty(r)) continue;
        set = (struct pg_packet){ .op = PG_CLIP, .id = (uintptr_t)w->handle, .flags = 2,
                                  .a = r.left, .b = r.top + origin,
                                  .c = r.right, .d = r.bottom + origin };
        ok = pe_ui_rpc(w->ui, &set);
    }
    pe_region_clear(&clip);
    if (!ok) return 0;
    struct pg_packet p = { .op = PG_TEXT_OUT, .id = (uintptr_t)w->handle,
                           .color = text_color, .background = background,
                           .c = height };
    {
        PE_STATE_GUARD;
        p.flags = d->u.dc.background_mode;
    }
    /* Without lpDx the whole run goes out in one packet, which keeps the font's
     * own advances and kerning. With lpDx each character is placed explicitly. */
    if (dx) {
        for (int i = 0; i < count; i++) {
            int len = WideCharToMultiByte(65001, 0, text + i, 1, p.text, PG_TEXT / 2, NULL, NULL);
            if (len <= 0) return 0;
            p.length = len;
            p.text[len] = 0;
            if (!WideCharToMultiByte(65001, 0, f.face, -1, p.text + len + 1,
                                     PG_TEXT - len - 1, NULL, NULL))
                return 0;
            p.a = x + (i ? dx[i - 1] : 0);
            p.b = y + origin;
            if (!pe_ui_rpc(w->ui, &p)) return 0;
            x = p.a;
        }
        return 1;
    }
    int len = WideCharToMultiByte(65001, 0, text, count, p.text, PG_TEXT / 2, NULL, NULL);
    if (len <= 0) return 0;
    p.length = len;
    p.text[len] = 0;
    if (!WideCharToMultiByte(65001, 0, f.face, -1, p.text + len + 1,
                             PG_TEXT - len - 1, NULL, NULL))
        return 0;
    p.a = x;
    p.b = y + origin;
    return pe_ui_rpc(w->ui, &p);
}
__attribute__((ms_abi)) BOOL ExtTextOutW(HANDLE dc, int x, int y, uint32_t options,
                                        const struct pe_rect *rect, const uint16_t *text,
                                        uint32_t count, const int32_t *dx)
{
    return pe_text_out_wide(dc, x, y, options, rect, text, (int)count, dx);
}
__attribute__((ms_abi)) BOOL ExtTextOutA(HANDLE dc, int x, int y, uint32_t options,
                                        const struct pe_rect *rect, const char *text,
                                        uint32_t count, const int32_t *dx)
{
    if (!text && count) { SetLastError(87); return 0; }
    if (!count) return 1;
    /* The profile's ANSI code page is UTF-8; decode, then reuse the wide path. */
    uint16_t wide[PG_TEXT];
    int characters = MultiByteToWideChar(65001, 0, text, (int)count, wide, PG_TEXT);
    if (characters <= 0) return 0;
    return pe_text_out_wide(dc, x, y, options, rect, wide, characters, dx);
}
