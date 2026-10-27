/* Disjoint, half-open client/device rectangles. All callers hold the state lock.
 * Operations build a temporary result so allocation failure never loses state. */
struct pe_rect { int32_t left, top, right, bottom; };
struct pe_region { struct pe_rect *rects; size_t count, capacity; };
static void pe_region_clear(struct pe_region *r)
{
    if (r->rects) raw_syscall3(SYS_munmap, (long)r->rects,
                             r->capacity * sizeof(*r->rects), 0);
    *r = (struct pe_region){0};
}
static bool pe_rect_nonempty(struct pe_rect r)
{ return r.left < r.right && r.top < r.bottom; }
static struct pe_rect pe_rect_intersect(struct pe_rect a, struct pe_rect b)
{
    return (struct pe_rect){ a.left > b.left ? a.left : b.left,
        a.top > b.top ? a.top : b.top, a.right < b.right ? a.right : b.right,
        a.bottom < b.bottom ? a.bottom : b.bottom };
}
static bool pe_region_append(struct pe_region *r, struct pe_rect box)
{
    if (!pe_rect_nonempty(box)) return true;
    if (r->count == r->capacity) {
        size_t cap = r->capacity ? r->capacity * 2 : 8;
        if (cap > SIZE_MAX / sizeof(box)) { SetLastError(8); return false; }
        struct pe_rect *p = pe_raw_map(cap * sizeof(box));
        if (!p) { SetLastError(8); return false; }
        if (r->count) memcpy(p, r->rects, r->count * sizeof(box));
        if (r->rects) raw_syscall3(SYS_munmap, (long)r->rects, r->capacity * sizeof(box), 0);
        r->rects = p; r->capacity = cap;
    }
    r->rects[r->count++] = box;
    return true;
}
static bool pe_region_copy(struct pe_region *dst, const struct pe_region *src)
{
    struct pe_region t = {0};
    for (size_t i = 0; i < src->count; i++)
        if (!pe_region_append(&t, src->rects[i])) { pe_region_clear(&t); return false; }
    pe_region_clear(dst); *dst = t; return true;
}
static bool pe_region_rect(struct pe_region *dst, struct pe_rect box)
{
    struct pe_region t = {0};
    if (!pe_region_append(&t, box)) return false;
    pe_region_clear(dst); *dst = t; return true;
}
static bool pe_region_cut(struct pe_region *r, struct pe_rect cut)
{
    struct pe_region t = {0};
    for (size_t i = 0; i < r->count; i++) {
        struct pe_rect a = r->rects[i], b = pe_rect_intersect(a, cut);
        if (!pe_rect_nonempty(b)) {
            if (!pe_region_append(&t, a)) goto fail;
        } else if (!pe_region_append(&t, (struct pe_rect){a.left,a.top,a.right,b.top}) ||
                   !pe_region_append(&t, (struct pe_rect){a.left,b.bottom,a.right,a.bottom}) ||
                   !pe_region_append(&t, (struct pe_rect){a.left,b.top,b.left,b.bottom}) ||
                   !pe_region_append(&t, (struct pe_rect){b.right,b.top,a.right,b.bottom})) goto fail;
    }
    pe_region_clear(r); *r = t; return true;
fail:
    pe_region_clear(&t); return false;
}
/* RGN_AND/OR/XOR/DIFF/COPY = 1..5, including aliased operands. */
static bool pe_region_combine(struct pe_region *dst, const struct pe_region *a,
                              const struct pe_region *b, int mode)
{
    struct pe_region t = {0}, extra = {0};
    if (mode == 1) {
        for (size_t i = 0; i < a->count; i++)
            for (size_t j = 0; j < b->count; j++)
                if (!pe_region_append(&t, pe_rect_intersect(a->rects[i],b->rects[j]))) goto fail;
    } else {
        if (!pe_region_copy(&t, a)) goto fail;
        if (mode == 3 || mode == 4)
            for (size_t j = 0; j < b->count; j++)
                if (!pe_region_cut(&t, b->rects[j])) goto fail;
        if (mode == 2 || mode == 3) {
            if (!pe_region_copy(&extra, b)) goto fail;
            for (size_t i = 0; i < a->count; i++)
                if (!pe_region_cut(&extra, a->rects[i])) goto fail;
            for (size_t j = 0; j < extra.count; j++)
                if (!pe_region_append(&t, extra.rects[j])) goto fail;
        }
    }
    pe_region_clear(&extra); pe_region_clear(dst); *dst = t; return true;
fail:
    pe_region_clear(&extra); pe_region_clear(&t); return false;
}
static int pe_region_box(const struct pe_region *r, struct pe_rect *box)
{
    *box = (struct pe_rect){0};
    if (!r->count) return 1;
    *box = r->rects[0];
    uint64_t area = 0;
    for (size_t i = 0; i < r->count; i++) {
        struct pe_rect a = r->rects[i];
        if (a.left < box->left) box->left = a.left;
        if (a.top < box->top) box->top = a.top;
        if (a.right > box->right) box->right = a.right;
        if (a.bottom > box->bottom) box->bottom = a.bottom;
        area += (uint64_t)((int64_t)a.right-a.left) * ((int64_t)a.bottom-a.top);
    }
    return area == (uint64_t)((int64_t)box->right-box->left) *
                              ((int64_t)box->bottom-box->top) ? 2 : 3;
}
static bool pe_region_contains(const struct pe_region *r, int x, int y)
{
    for (size_t i = 0; i < r->count; i++) {
        struct pe_rect a = r->rects[i];
        if (x >= a.left && x < a.right && y >= a.top && y < a.bottom) return true;
    }
    return false;
}
