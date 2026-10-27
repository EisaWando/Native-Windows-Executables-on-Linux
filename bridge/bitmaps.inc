/* bridge/bitmaps.inc — software-raster bitmap and blit shims for memory DCs.
 * Window DCs: operations that require the host return ERROR_NOT_SUPPORTED (50).
 * TODO: route BitBlt src=window-DC through PG_BLIT once the host supports it.
 */

/* Helper: get the raster of the bitmap currently selected into dc (memDC only). */
static struct pe_raster *pe_memdc_raster(HANDLE dc)
{
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d || d->type != PE_GDI_MEMDC) { SetLastError(50); return NULL; }
    struct pe_gui_object *b = pe_gui_lookup(d->u.dc.bitmap, PE_GDI_BITMAP);
    if (!b || !b->raster) { SetLastError(50); return NULL; }
    return b->raster;
}

/* Helper: BGR→ARGB (GDI uses 0x00BBGGRR) */
static uint32_t pe_bgr_to_argb(DWORD c) { return 0xff000000u | ((c&0xff)<<16) | (c&0xff00u) | ((c>>16)&0xff); }
static DWORD    pe_argb_to_bgr(uint32_t c) { return ((c>>16)&0xff) | (c&0xff00u) | ((c&0xff)<<16); }

/* Apply common raster-op on one pixel pair. */
static uint32_t pe_rop(uint32_t dst, uint32_t src, uint32_t pat, DWORD rop)
{
    switch (rop) {
    case 0x00000042: return 0xff000000u;                       /* BLACKNESS  */
    case 0x00FF0062: return 0xffffffffu;                       /* WHITENESS  */
    case 0x00CC0020: return src;                               /* SRCCOPY    */
    case 0x00EE0086: return 0xff000000u|(dst|src);             /* SRCPAINT   */
    case 0x008800C6: return 0xff000000u|(dst&src);             /* SRCAND     */
    case 0x00660046: return 0xff000000u|(dst^src);             /* SRCINVERT  */
    case 0x00440328: return 0xff000000u|(dst&~src);            /* SRCERASE   */
    case 0x00330008: return 0xff000000u|~dst;                  /* DSTINVERT  */
    case 0x00F00021: return pat;                               /* PATCOPY    */
    case 0x00FB0A09: return 0xff000000u|(pat|dst);             /* PATPAINT   */
    case 0x005A0049: return 0xff000000u|(dst^pat);             /* PATINVERT  */
    default: return src; /* unsupported: fall back to SRCCOPY */
    }
}

/* ── CreateCompatibleBitmap ─────────────────────────────────────────────── */
__attribute__((ms_abi))
HANDLE CreateCompatibleBitmap(HANDLE dc, int width, int height)
{
    PE_STATE_GUARD;
    /* dc is informational (colour depth); we always create 32-bpp rasters. */
    if (dc && !pe_dc_lookup(dc)) return NULL;
    if (width <= 0 || height <= 0) { SetLastError(87); return NULL; }
    struct pe_raster *r = pe_raster_new(width, height, false);
    if (!r) return NULL;
    struct pe_gui_object *o = pe_gui_new(PE_GDI_BITMAP);
    if (!o) { pe_raster_free(&(struct pe_gui_object){ .raster = r }); return NULL; }
    o->raster = r;
    int stride = width * 4;
    o->u.bitmap = (struct pe_bitmap){ 0, width, height, stride, 1, 32, NULL };
    return pe_gui_handle(o);
}

/* ── SetStretchBltMode ───────────────────────────────────────────────────── */
static int pe_stretch_mode; /* global; no per-DC field yet */
__attribute__((ms_abi))
int SetStretchBltMode(HANDLE dc, int mode)
{
    PE_STATE_GUARD;
    if (dc && !pe_dc_lookup(dc)) return 0;
    int old = pe_stretch_mode;
    pe_stretch_mode = mode;
    return old ? old : 1;
}
__attribute__((ms_abi))
int GetStretchBltMode(HANDLE dc)
{
    PE_STATE_GUARD;
    if (dc && !pe_dc_lookup(dc)) return 0;
    return pe_stretch_mode ? pe_stretch_mode : 1;
}

/* ── PatBlt ──────────────────────────────────────────────────────────────── */
__attribute__((ms_abi))
BOOL PatBlt(HANDLE dc, int x, int y, int w, int h, DWORD rop)
{
    PE_STATE_GUARD;
    struct pe_raster *dst = pe_memdc_raster(dc);
    if (!dst) return 0;
    struct pe_gui_object *d = pe_dc_lookup(dc);
    if (!d) return 0;
    struct pe_gui_object *br = pe_gui_lookup(d->u.dc.brush, PE_GDI_BRUSH);
    uint32_t pat = br ? pe_bgr_to_argb(br->u.brush.color) : 0xff000000u;
    if (br && br->u.brush.style != 0) pat = 0xff000000u; /* non-solid → black */
    int x1 = x < 0 ? 0 : x, y1 = y < 0 ? 0 : y;
    int x2 = x + w; if (x2 > dst->width) x2 = dst->width;
    int y2 = y + h; if (y2 > dst->height) y2 = dst->height;
    for (int py = y1; py < y2; py++)
        for (int px = x1; px < x2; px++) {
            if (d->clip_active && !pe_region_contains(&d->region, px, py)) continue;
            uint32_t dv = pe_raster_get(dst, px, py);
            pe_raster_set(dst, px, py, pe_rop(dv, dv, pat, rop));
        }
    return 1;
}

/* ── BitBlt ──────────────────────────────────────────────────────────────── */
__attribute__((ms_abi))
BOOL BitBlt(HANDLE hdst, int dx, int dy, int w, int h,
            HANDLE hsrc, int sx, int sy, DWORD rop)
{
    PE_STATE_GUARD;
    struct pe_raster *dst = pe_memdc_raster(hdst);
    if (!dst) return 0;
    /* Source may be NULL for rops that don't use src (PATCOPY, BLACKNESS, …) */
    struct pe_raster *src = NULL;
    if (hsrc) {
        struct pe_gui_object *sd = pe_dc_lookup(hsrc);
        if (!sd) return 0;
        if (sd->type == PE_GDI_MEMDC) {
            struct pe_gui_object *sb = pe_gui_lookup(sd->u.dc.bitmap, PE_GDI_BITMAP);
            if (sb) src = sb->raster;
        }
        /* Window DCs: no raster available; only src-less rops allowed. */
    }
    struct pe_gui_object *d = pe_dc_lookup(hdst);
    if (!d) return 0;
    struct pe_gui_object *br = pe_gui_lookup(d->u.dc.brush, PE_GDI_BRUSH);
    uint32_t pat = br ? pe_bgr_to_argb(br->u.brush.color) : 0xff000000u;
    if (br && br->u.brush.style != 0) pat = 0xff000000u;
    int x1 = dx < 0 ? 0 : dx, y1 = dy < 0 ? 0 : dy;
    int x2 = dx + w; if (x2 > dst->width) x2 = dst->width;
    int y2 = dy + h; if (y2 > dst->height) y2 = dst->height;
    for (int py = y1; py < y2; py++)
        for (int px = x1; px < x2; px++) {
            if (d->clip_active && !pe_region_contains(&d->region, px, py)) continue;
            uint32_t dv = pe_raster_get(dst, px, py);
            uint32_t sv = dv;
            if (src) {
                int qx = sx + (px - dx), qy = sy + (py - dy);
                if (qx >= 0 && qy >= 0 && qx < src->width && qy < src->height)
                    sv = pe_raster_get(src, qx, qy);
            }
            pe_raster_set(dst, px, py, pe_rop(dv, sv, pat, rop));
        }
    return 1;
}

/* ── StretchBlt ──────────────────────────────────────────────────────────── */
__attribute__((ms_abi))
BOOL StretchBlt(HANDLE hdst, int dx, int dy, int dw, int dh,
                HANDLE hsrc, int sx, int sy, int sw, int sh, DWORD rop)
{
    /* Fast path: same size → delegate to BitBlt */
    if (dw == sw && dh == sh)
        return BitBlt(hdst, dx, dy, dw, dh, hsrc, sx, sy, rop);
    PE_STATE_GUARD;
    struct pe_raster *dst = pe_memdc_raster(hdst);
    if (!dst) return 0;
    struct pe_raster *src = NULL;
    if (hsrc) {
        struct pe_gui_object *sd = pe_dc_lookup(hsrc);
        if (!sd) return 0;
        if (sd->type == PE_GDI_MEMDC) {
            struct pe_gui_object *sb = pe_gui_lookup(sd->u.dc.bitmap, PE_GDI_BITMAP);
            if (sb) src = sb->raster;
        }
    }
    struct pe_gui_object *d = pe_dc_lookup(hdst);
    if (!d) return 0;
    struct pe_gui_object *br = pe_gui_lookup(d->u.dc.brush, PE_GDI_BRUSH);
    uint32_t pat = br ? pe_bgr_to_argb(br->u.brush.color) : 0xff000000u;
    if (br && br->u.brush.style != 0) pat = 0xff000000u;
    int x1 = dx < 0 ? 0 : dx, y1 = dy < 0 ? 0 : dy;
    int x2 = dx + dw; if (x2 > dst->width) x2 = dst->width;
    int y2 = dy + dh; if (y2 > dst->height) y2 = dst->height;
    for (int py = y1; py < y2; py++)
        for (int px = x1; px < x2; px++) {
            if (d->clip_active && !pe_region_contains(&d->region, px, py)) continue;
            uint32_t dv = pe_raster_get(dst, px, py);
            uint32_t sv = dv;
            if (src && sw > 0 && sh > 0) {
                /* Nearest-neighbour */
                int qx = sx + (px - dx) * sw / dw;
                int qy = sy + (py - dy) * sh / dh;
                if (qx >= 0 && qy >= 0 && qx < src->width && qy < src->height)
                    sv = pe_raster_get(src, qx, qy);
            }
            pe_raster_set(dst, px, py, pe_rop(dv, sv, pat, rop));
        }
    return 1;
}

/* ── GetDIBits ───────────────────────────────────────────────────────────── */
__attribute__((ms_abi))
int GetDIBits(HANDLE dc, HANDLE hbm, unsigned startScan, unsigned scanLines,
              void *bits, void *info, unsigned usage)
{
    PE_STATE_GUARD;
    (void)dc; (void)usage;
    struct pe_gui_object *b = pe_gui_lookup(hbm, PE_GDI_BITMAP);
    if (!b || !b->raster) { SetLastError(87); return 0; }
    struct pe_raster *r = b->raster;
    /* Fill in BITMAPINFOHEADER (40 bytes) if info is provided. */
    if (info) {
        uint32_t hdr; memcpy(&hdr, info, 4);
        if (hdr >= 40) {
            int32_t iw = r->width, ih = -(int32_t)r->height; /* top-down */
            uint16_t planes = 1, bpp = 32;
            uint32_t comp = 0, sizeImg = (uint32_t)r->width * r->height * 4;
            memcpy((char*)info+4,  &iw, 4);
            memcpy((char*)info+8,  &ih, 4);
            memcpy((char*)info+12, &planes, 2);
            memcpy((char*)info+14, &bpp, 2);
            memcpy((char*)info+16, &comp, 4);
            memcpy((char*)info+20, &sizeImg, 4);
        }
    }
    if (!bits || scanLines == 0) return (int)r->height;
    if (startScan >= (unsigned)r->height) return 0;
    unsigned end = startScan + scanLines;
    if (end > (unsigned)r->height) end = (unsigned)r->height;
    uint32_t *out = (uint32_t *)bits;
    for (unsigned row = startScan; row < end; row++)
        for (int col = 0; col < r->width; col++) {
            uint32_t px = pe_raster_get(r, col, (int)row);
            /* Convert ARGB → BGRA (DIB format) */
            *out++ = ((px & 0xff) << 16) | (px & 0xff00u) | ((px >> 16) & 0xff) | (px & 0xff000000u);
        }
    return (int)(end - startScan);
}

/* ── SetDIBits ───────────────────────────────────────────────────────────── */
__attribute__((ms_abi))
int SetDIBits(HANDLE dc, HANDLE hbm, unsigned startScan, unsigned scanLines,
              const void *bits, const void *info, unsigned usage)
{
    PE_STATE_GUARD;
    (void)dc; (void)usage;
    struct pe_gui_object *b = pe_gui_lookup(hbm, PE_GDI_BITMAP);
    if (!b || !b->raster) { SetLastError(87); return 0; }
    if (!bits || !info) { SetLastError(87); return 0; }
    struct pe_raster *r = b->raster;
    uint32_t hdr; memcpy(&hdr, info, 4);
    uint16_t bpp = 32; if (hdr >= 14) memcpy(&bpp, (const char*)info+14, 2);
    if (bpp != 32 && bpp != 24) { SetLastError(50); return 0; }
    if (startScan >= (unsigned)r->height) return 0;
    unsigned end = startScan + scanLines;
    if (end > (unsigned)r->height) end = (unsigned)r->height;
    size_t stride = bpp == 32 ? (size_t)r->width * 4 :
                                (((size_t)r->width * 3 + 3) & ~3u);
    const unsigned char *in = (const unsigned char *)bits;
    for (unsigned row = startScan; row < end; row++) {
        const unsigned char *src = in + (row - startScan) * stride;
        for (int col = 0; col < r->width; col++) {
            uint32_t px;
            if (bpp == 32) {
                uint32_t raw; memcpy(&raw, src + col*4, 4);
                /* BGRA → ARGB */
                px = 0xff000000u | ((raw&0xff)<<16) | (raw&0xff00u) | ((raw>>16)&0xff);
            } else {
                const unsigned char *p = src + col*3;
                px = 0xff000000u | ((uint32_t)p[2]<<16) | ((uint32_t)p[1]<<8) | p[0];
            }
            pe_raster_set(r, col, (int)row, px);
        }
    }
    return (int)(end - startScan);
}
