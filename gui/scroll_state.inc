/* Logical scrollbar state only. Native thumb/arrow drawing, input tracking and
 * nonclient geometry are pending; no PE callbacks execute under the state lock. */
struct pe_scroll_info {
	DWORD size, mask;
	int32_t min, max;
	DWORD page;
	int32_t pos, track;
};
struct pe_scroll_bar {
	int32_t min, max, pos, track;
	DWORD page;
	unsigned disabled;
};
struct pe_scrollbars {
	struct pe_scroll_bar bar[3];
};
_Static_assert(sizeof(struct pe_scroll_info) == 28, "SCROLLINFO ABI");
static void pe_scroll_clear(struct pe_window *w)
{
	if (w->scrollbars) {
		raw_syscall3(SYS_munmap, (long)w->scrollbars,
			     sizeof(*w->scrollbars), 0);
		w->scrollbars = NULL;
	}
}
static struct pe_scroll_bar *pe_scroll_get(struct pe_window *w, int bar)
{
	if (bar < 0 || bar > 2) {
		SetLastError(87);
		return NULL;
	}
	if (!w->scrollbars) {
		w->scrollbars = pe_raw_map(sizeof(*w->scrollbars));
		if (!w->scrollbars) {
			SetLastError(8);
			return NULL;
		}
		for (unsigned i = 0; i < 3; i++)
			w->scrollbars->bar[i].max = 100;
	}
	return &w->scrollbars->bar[bar];
}
static void pe_scroll_clamp(struct pe_scroll_bar *b)
{
	uint64_t extent = (int64_t)b->max - b->min + 1;
	if ((uint64_t)b->page > extent)
		b->page = extent;
	int64_t high = (int64_t)b->max - (b->page ? b->page - 1 : 0);
	if (high < b->min)
		high = b->min;
	if (b->pos < b->min)
		b->pos = b->min;
	if (b->pos > high)
		b->pos = high;
	b->track =
		b->pos; /* no user-driven thumb tracking until native scroll input */
}
static void pe_scroll_visible(struct pe_window *w, int bar, bool visible)
{
	if (bar == 2) {
		w->visible = visible;
		w->style = visible ? w->style | 0x10000000u :
				     w->style & ~0x10000000u;
	} else {
		DWORD bit = bar ? 0x00200000u : 0x00100000u;
		w->style = visible ? w->style | bit : w->style & ~bit;
	}
}
static bool pe_scroll_validate(const struct pe_scroll_info *info, bool get)
{
	if (!info || (info->size != 28 && info->size != 24) ||
	    (info->mask & ~31u) ||
	    (get && info->size == 24 && (info->mask & 16))) {
		SetLastError(87);
		return false;
	}
	return true;
}
__attribute__((ms_abi)) BOOL GetScrollInfo(HANDLE hwnd, int bar,
					   struct pe_scroll_info *info)
{
	if (!pe_scroll_validate(info, true))
		return 0;
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	if (!b)
		return 0;
	if (info->mask & 1) {
		info->min = b->min;
		info->max = b->max;
	}
	if (info->mask & 2)
		info->page = b->page;
	if (info->mask & 4)
		info->pos = b->pos;
	if (info->mask & 16)
		info->track = b->track;
	return 1;
}
__attribute__((ms_abi)) int SetScrollInfo(HANDLE hwnd, int bar,
					  const struct pe_scroll_info *info,
					  BOOL redraw)
{
	if (!pe_scroll_validate(info, false))
		return 0;
	if ((info->mask & 1) && info->min > info->max) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	if (!b)
		return 0;
	if (info->mask & 1) {
		b->min = info->min;
		b->max = info->max;
	}
	if (info->mask & 2)
		b->page = info->page;
	if (info->mask & 4)
		b->pos = info->pos;
	pe_scroll_clamp(b);
	if (info->mask & 3) {
		uint64_t extent = (int64_t)b->max - b->min + 1;
		bool needed = extent > 1 && (uint64_t)b->page < extent;
		b->disabled = needed ? 0 : 3;
		pe_scroll_visible(w, bar, needed || (info->mask & 8));
	}
	if (redraw)
		pe_invalidate_full(w);
	return b->pos;
}
__attribute__((ms_abi)) int GetScrollPos(HANDLE hwnd, int bar)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	return b ? b->pos : 0;
}
__attribute__((ms_abi)) int SetScrollPos(HANDLE hwnd, int bar, int pos,
					 BOOL redraw)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	if (!b)
		return 0;
	int old = b->pos;
	b->pos = pos;
	pe_scroll_clamp(b);
	if (redraw)
		pe_invalidate_full(w);
	return old;
}
__attribute__((ms_abi)) BOOL GetScrollRange(HANDLE hwnd, int bar, int *min,
					    int *max)
{
	if (!min || !max) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	if (!b)
		return 0;
	*min = b->min;
	*max = b->max;
	return 1;
}
__attribute__((ms_abi)) BOOL SetScrollRange(HANDLE hwnd, int bar, int min,
					    int max, BOOL redraw)
{
	if (min > max || (int64_t)max - min > INT32_MAX) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_scroll_bar *b = pe_scroll_get(w, bar);
	if (!b)
		return 0;
	b->min = min;
	b->max = max;
	pe_scroll_clamp(b);
	pe_scroll_visible(w, bar, min != max);
	if (redraw)
		pe_invalidate_full(w);
	return 1;
}
__attribute__((ms_abi)) BOOL ShowScrollBar(HANDLE hwnd, int bar, BOOL show)
{
	if (bar < 0 || bar > 3) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	if (bar == 2 && !(w->style & 0x40000000u)) {
		SetLastError(87);
		return 0;
	}
	if (bar == 3) {
		pe_scroll_visible(w, 0, show);
		pe_scroll_visible(w, 1, show);
	} else
		pe_scroll_visible(w, bar, show);
	pe_invalidate_full(w);
	return 1;
}
__attribute__((ms_abi)) BOOL EnableScrollBar(HANDLE hwnd, unsigned bar,
					     unsigned arrows)
{
	if (bar > 3 || arrows > 3) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	bool changed = false;
	int first = bar == 3 ? 0 : bar, last = bar == 3 ? 1 : bar;
	for (int i = first; i <= last; i++) {
		struct pe_scroll_bar *b = pe_scroll_get(w, i);
		if (!b)
			return 0;
		if (b->disabled != arrows) {
			b->disabled = arrows;
			changed = true;
		}
	}
	if (changed)
		pe_invalidate_full(w);
	return changed;
}
