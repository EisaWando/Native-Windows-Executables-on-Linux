/* ReBarWindow32 band storage and layout. Bands keep their child window,
 * minimum child size and explicit width; the bar lays bands out top to
 * bottom on insert and on WM_SIZE, and RB_GETBARHEIGHT reports the total
 * band height so hosts can size the bar from its child controls.
 * REBARBANDINFOW field offsets (Windows x64): fMask 4, fStyle 8,
 * lpText 24, hwndChild 40, cxMinChild 48, cyMinChild 52, cx 56,
 * wID 72, cxIdeal 88, lParam 96; sizeof is at least 104. */
struct pe_rebar_band {
	HANDLE child;
	unsigned cx_min, cy_min, cx, width, height;
	DWORD style;
	unsigned id;
	intptr_t data;
};

struct pe_rebar {
	HANDLE hwnd, tooltips;
	struct pe_rebar_band *bands;
	unsigned count, capacity, tooltip_count;
	bool laying_out, has_info;
	DWORD info_mask;
	struct pe_rebar *next;
};
static struct pe_rebar *pe_rebars;

static struct pe_rebar *pe_rebar_get(HANDLE hwnd, bool create)
{
	for (struct pe_rebar *r = pe_rebars; r; r = r->next)
		if (r->hwnd == hwnd)
			return r;
	if (!create)
		return NULL;
	struct pe_rebar *r = pe_raw_map(sizeof(*r));
	if (!r) {
		SetLastError(8);
		return NULL;
	}
	r->hwnd = hwnd;
	r->next = pe_rebars;
	pe_rebars = r;
	return r;
}
static void pe_rebar_free(HANDLE hwnd)
{
	struct pe_rebar **p = &pe_rebars;
	while (*p && (*p)->hwnd != hwnd)
		p = &(*p)->next;
	struct pe_rebar *r = *p;
	if (!r)
		return;
	*p = r->next;
	if (r->bands)
		raw_syscall3(SYS_munmap, (long)r->bands,
			     r->capacity * sizeof(*r->bands), 0);
	raw_syscall3(SYS_munmap, (long)r, sizeof(*r), 0);
}
static struct pe_rebar_band *pe_rebar_band(struct pe_rebar *r, unsigned index)
{
	return index < r->count ? &r->bands[index] : NULL;
}
static void pe_rebar_layout(struct pe_rebar *r)
{
	struct pe_window *w = pe_window_lookup(r->hwnd, false);
	if (!w || r->laying_out)
		return;
	r->laying_out = true;
	int y = 0, bar_width = w->width > 0 ? w->width : 0;
	for (unsigned i = 0; i < r->count; i++) {
		struct pe_rebar_band *b = &r->bands[i];
		struct pe_window *child =
			b->child ? pe_window_lookup(b->child, false) : NULL;
		int height = (int)b->cy_min;
		if (child && child->height > height)
			height = child->height;
		if (height < 1)
			height = 24;
		int width = b->cx ? (int)b->cx : bar_width;
		if (width < 0)
			width = 0;
		b->height = (unsigned)height;
		b->width = (unsigned)width;
		if (child) {
			SetWindowPos(child->handle, NULL, 0, y, width, height,
				     4 | 0x10);
			if (w->dead)
				break;
		}
		y += height;
	}
	r->laying_out = false;
}
static int pe_rebar_total_height(struct pe_rebar *r)
{
	int y = 0;
	for (unsigned i = 0; i < r->count; i++)
		y += (int)r->bands[i].height;
	return y;
}
static __attribute__((ms_abi)) intptr_t pe_control_proc(HANDLE, DWORD,
							uintptr_t, intptr_t);
static __attribute__((ms_abi)) intptr_t pe_rebar_proc(HANDLE hwnd, DWORD msg,
						      uintptr_t wp, intptr_t lp)
{
	struct pe_rebar *r = pe_rebar_get(hwnd, false);
	if (msg == 0x82) { /* WM_NCDESTROY */
		pe_rebar_free(hwnd);
		return pe_control_proc(hwnd, msg, wp, lp);
	}
	bool read_only = msg == 0x403 || msg == 0x40c || msg == 0x41b ||
			 msg == 0x409 || msg == 0x41c || msg == 0x410 ||
			 msg == 0x40d || msg == 0x40e;
	if (read_only && !r)
		return 0;
	if (!r) {
		if (msg != 0x401 && msg != 0x40a && msg != 0x404 &&
		    msg != 0x40b)
			return pe_control_proc(hwnd, msg, wp, lp);
		r = pe_rebar_get(hwnd, true);
		if (!r)
			return 0;
	}
	if (msg == 0x404) { /* RB_SETBARINFO */
		if (!lp || *(const uint32_t *)lp < 16)
			return 0;
		r->has_info = true;
		r->info_mask = *(const uint32_t *)((const char *)lp + 4);
		return 1;
	}
	if (msg == 0x403) { /* RB_GETBARINFO */
		if (!lp || *(const uint32_t *)lp < 16)
			return 0;
		*(uint32_t *)lp = 16;
		*(uint32_t *)((char *)lp + 4) = r->has_info ? r->info_mask : 0;
		*(HANDLE *)((char *)lp + 8) = NULL;
		return 1;
	}
	if (msg == 0x401 || msg == 0x40a) { /* RB_INSERTBAND A/W */
		if (!lp)
			return 0;
		const char *band = (const void *)lp;
		unsigned cb = *(const uint32_t *)band;
		if (cb < 88)
			return 0;
		unsigned index = wp > r->count ? r->count : (unsigned)wp;
		if (r->count == r->capacity) {
			unsigned cap = r->capacity ? r->capacity * 2 : 8;
			struct pe_rebar_band *v =
				pe_raw_map(cap * sizeof(*v));
			if (!v)
				return 0;
			if (r->count)
				memcpy(v, r->bands,
				       r->count * sizeof(*v));
			if (r->bands)
				raw_syscall3(SYS_munmap, (long)r->bands,
					     r->capacity * sizeof(*v), 0);
			r->bands = v;
			r->capacity = cap;
		}
		memmove(r->bands + index + 1, r->bands + index,
			(r->count - index) * sizeof(*r->bands));
		struct pe_rebar_band *b = &r->bands[index];
		*b = (struct pe_rebar_band){ 0 };
		b->style = *(const uint32_t *)(band + 8);
		b->child = *(HANDLE *)(band + 40);
		b->cx_min = *(const uint32_t *)(band + 48);
		b->cy_min = *(const uint32_t *)(band + 52);
		b->cx = *(const uint32_t *)(band + 56);
		b->id = *(const uint32_t *)(band + 72);
		b->data = *(const intptr_t *)(band + 96);
		r->count++;
		pe_rebar_layout(r);
		return 1;
	}
	if (msg == 0x402) { /* RB_DELETEBAND */
		if (wp >= r->count)
			return 0;
		struct pe_rebar_band *b = &r->bands[wp];
		if (b->child)
			ShowWindow(b->child, 0);
		memmove(r->bands + wp, r->bands + wp + 1,
			(r->count - wp - 1) * sizeof(*r->bands));
		r->count--;
		pe_rebar_layout(r);
		return 1;
	}
	if (msg == 0x40b) { /* RB_SETBANDINFOW */
		if (!lp || wp >= r->count)
			return 0;
		const char *band = (const void *)lp;
		unsigned cb = *(const uint32_t *)band;
		if (cb < 88)
			return 0;
		struct pe_rebar_band *b = &r->bands[wp];
		DWORD mask = *(const uint32_t *)(band + 4);
		if (mask & 0x08)
			b->style = *(const uint32_t *)(band + 8);
		if (mask & 0x10)
			b->child = *(HANDLE *)(band + 40);
		if (mask & 0x20) {
			b->cx_min = *(const uint32_t *)(band + 48);
			b->cy_min = *(const uint32_t *)(band + 52);
		}
		if (mask & 0x40)
			b->cx = *(const uint32_t *)(band + 56);
		if (mask & 0x200)
			b->data = *(const intptr_t *)(band + 96);
		pe_rebar_layout(r);
		return 1;
	}
	if (msg == 0x41c || msg == 0x406) { /* RB_GETBANDINFO W */
		struct pe_rebar_band *b = pe_rebar_band(r, (unsigned)wp);
		if (!b || !lp || *(const uint32_t *)lp < 88)
			return 0;
		char *band = (void *)lp;
		DWORD mask = *(uint32_t *)(band + 4);
		*(uint32_t *)(band + 8) = b->style;
		if (mask & 0x10)
			*(HANDLE *)(band + 40) = b->child;
		if (mask & 0x20) {
			*(uint32_t *)(band + 48) = b->cx_min;
			*(uint32_t *)(band + 52) = b->cy_min;
		}
		if (mask & 0x40)
			*(uint32_t *)(band + 56) = b->cx;
		if (mask & 0x200)
			*(intptr_t *)(band + 96) = b->data;
		return 1;
	}
	if (msg == 0x40c) /* RB_GETBANDCOUNT */
		return r->count;
	if (msg == 0x41b) /* RB_GETBARHEIGHT */
		return pe_rebar_total_height(r);
	if (msg == 0x40d) /* RB_GETROWCOUNT */
		return r->count ? 1 : 0;
	if (msg == 0x40e) /* RB_GETROWHEIGHT */
		return r->count ? (int)r->bands[0].height : 0;
	if (msg == 0x409) { /* RB_GETRECT */
		struct pe_rebar_band *b = pe_rebar_band(r, (unsigned)wp);
		if (!b || !lp)
			return 0;
		int top = 0;
		for (unsigned i = 0; i < (unsigned)wp; i++)
			top += (int)r->bands[i].height;
		struct pe_rect *rect = (void *)lp;
		rect->left = 0;
		rect->top = top;
		rect->right = (int)b->width;
		rect->bottom = top + (int)b->height;
		return 1;
	}
	if (msg == 0x410) { /* RB_IDTOINDEX */
		for (unsigned i = 0; i < r->count; i++)
			if (r->bands[i].id == (unsigned)wp)
				return (intptr_t)i;
		return -1;
	}
	if (msg == 0x411) { /* RB_GETTOOLTIPS */
		return (intptr_t)r->tooltips;
	}
	if (msg == 0x5 || msg == 0x47) { /* WM_SIZE / WM_WINDOWPOSCHANGED */
		pe_rebar_layout(r);
		return 0;
	}
	return pe_control_proc(hwnd, msg, wp, lp);
}
