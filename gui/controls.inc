/* Headless system/common controls: actual state, no pixels or display handles. */
struct pe_control_item {
	uint16_t *text;
	intptr_t data;
};
struct pe_control {
	HANDLE font, image, buddy;
	uint16_t *text;
	unsigned text_bytes;
	struct pe_control_item *items;
	unsigned count, capacity;
	int selection;
	int check, state, min, max, position;
	int *parts;
	unsigned part_count;
	unsigned button_size;
	unsigned char *buttons;
	unsigned button_count;
};
static __attribute__((ms_abi)) intptr_t pe_control_proc(HANDLE, DWORD,
							uintptr_t, intptr_t);
static struct pe_gui_class *pe_builtin_class(HANDLE instance,
					     const uint16_t *name)
{
	(void)instance;
	static const char *const names[] = {
		"Button",	    "Static",	       "Edit",
		"ListBox",	    "ComboBox",	       "ToolbarWindow32",
		"ReBarWindow32",    "SysTabControl32", "msctls_statusbar32",
		"tooltips_class32", "SysListView32",   "msctls_updown32", "ScrollBar"
	};
	static struct pe_gui_class *classes;
	PE_STATE_GUARD;
	if (!classes) {
		classes = pe_raw_map(sizeof(*classes) * 13);
		if (!classes) {
			SetLastError(8);
			return NULL;
		}
		for (unsigned i = 0; i < 13; i++) {
			struct pe_gui_class *c = &classes[i];
			c->atom = 0xa000 + i;
			c->builtin = i + 1;
			c->data.size = 80;
			c->data.wndproc = pe_control_proc;
			c->data.window_extra = 4096;
			unsigned n = 0;
			while (names[i][n]) {
				c->name[n] = names[i][n];
				n++;
			}
			c->data.class_name = c->name;
		}
	}
	for (unsigned i = 0; i < 13; i++)
		if ((uintptr_t)name < 65536 ?
			    (uintptr_t)name == classes[i].atom :
			    utf16_ascii_equal_ci(name, names[i]))
			return &classes[i];
	return NULL;
}
__attribute__((ms_abi)) BOOL InitCommonControlsEx(const DWORD *init)
{
	if (!init || init[0] != 8 || (init[1] & ~0xffffu)) {
		SetLastError(87);
		return 0;
	}
	static const uint16_t button[] = { 'B', 'u', 't', 't', 'o', 'n', 0 };
	return pe_builtin_class(NULL, button) != NULL;
}
__attribute__((ms_abi)) void InitCommonControls(void)
{
	DWORD init[] = { 8, 0xff };
	InitCommonControlsEx(init);
}
static struct pe_control *pe_control_get(struct pe_window *w)
{
	if (!w->control) {
		w->control = pe_raw_map(sizeof(*w->control));
		if (w->control) {
			w->control->selection = -1;
			w->control->max = w->cls->builtin == 12 ? 100 : 0;
			w->control->button_size = 32;
		}
	}
	return w->control;
}
static uint16_t *pe_control_copy(const uint16_t *text)
{
	if (!text)
		text = (const uint16_t[]){ 0 };
	size_t bytes = (lstrlenW(text) + 1) * 2;
	if (bytes > 2 * 1024 * 1024)
		return NULL;
	uint16_t *p = pe_raw_map(bytes);
	if (p)
		memcpy(p, text, bytes);
	return p;
}
static intptr_t pe_control_text(struct pe_window *w, const uint16_t *text)
{
	struct pe_control *c = pe_control_get(w);
	if (!c)
		return 0;
	uint16_t *copy = pe_control_copy(text);
	if (!copy)
		return 0;
	if (c->text)
		raw_syscall3(SYS_munmap, (long)c->text, c->text_bytes, 0);
	c->text = copy;
	c->text_bytes = (lstrlenW(copy) + 1) * 2;
	return 1;
}
static void pe_control_reset(struct pe_control *c)
{
	for (unsigned i = 0; i < c->count; i++)
		if (c->items[i].text)
			raw_syscall3(SYS_munmap, (long)c->items[i].text,
				     (lstrlenW(c->items[i].text) + 1) * 2, 0);
	c->count = 0;
	c->selection = -1;
}
static intptr_t pe_control_insert(struct pe_control *c, int index,
				  const uint16_t *text, intptr_t data)
{
	if (index < 0 || (unsigned)index > c->count)
		index = c->count;
	uint16_t *copy = pe_control_copy(text);
	if (!copy)
		return -2;
	if (c->count == c->capacity) {
		unsigned cap = c->capacity ? c->capacity * 2 : 16;
		struct pe_control_item *items =
			pe_raw_map(cap * sizeof(*items));
		if (!items) {
			raw_syscall3(SYS_munmap, (long)copy,
				     (lstrlenW(copy) + 1) * 2, 0);
			return -2;
		}
		if (c->count)
			memcpy(items, c->items, c->count * sizeof(*items));
		if (c->items)
			raw_syscall3(SYS_munmap, (long)c->items,
				     c->capacity * sizeof(*items), 0);
		c->items = items;
		c->capacity = cap;
	}
	memmove(c->items + index + 1, c->items + index,
		(c->count - index) * sizeof(*c->items));
	c->items[index] = (struct pe_control_item){ copy, data };
	c->count++;
	return index;
}
static intptr_t pe_control_delete(struct pe_control *c, unsigned index)
{
	if (index >= c->count)
		return -1;
	uint16_t *text = c->items[index].text;
	if (text)
		raw_syscall3(SYS_munmap, (long)text, (lstrlenW(text) + 1) * 2,
			     0);
	memmove(c->items + index, c->items + index + 1,
		(c->count - index - 1) * sizeof(*c->items));
	c->count--;
	if (c->selection >= (int)c->count)
		c->selection = -1;
	return c->count;
}
static __attribute__((ms_abi)) intptr_t pe_control_proc(HANDLE hwnd, DWORD msg,
							uintptr_t wp,
							intptr_t lp)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	PE_STATE_GUARD;
	struct pe_control *c = pe_control_get(w);
	if (!c) {
		SetLastError(8);
		return 0;
	}
	unsigned kind = w->cls->builtin;
	if (msg == 0x81)
		return 1;
	if (msg == 1)
		return 0;
	if (msg == 0xc)
		return pe_control_text(w, (const uint16_t *)lp);
	const uint16_t *text = c->text ? c->text : w->title;
	if (msg == 0xe)
		return lstrlenW(text);
	if (msg == 0xd) {
		if (!wp || !lp)
			return 0;
		size_t n = lstrlenW(text);
		if (n >= wp)
			n = wp - 1;
		memcpy((void *)lp, text, n * 2);
		((uint16_t *)lp)[n] = 0;
		return n;
	}
	if (msg == 0x30) {
		c->font = (HANDLE)wp;
		return 0;
	}
	if (msg == 0x31)
		return (intptr_t)c->font;
	if (msg == 0x87)
		return kind == 3 ? 0x89 : kind == 1 ? 0x2000 : 0;
	if (kind == 1) {
		if (msg == 0xf0)
			return c->check;
		if (msg == 0xf1) {
			c->check = wp;
			return 0;
		}
		if (msg == 0xf2)
			return c->state | c->check;
		if (msg == 0xf3) {
			c->state = wp ? 4 : 0;
			return 0;
		}
		if (msg == 0xf5) {
			if (w->parent) {
				pe_state_leave(&pe_guard);
				return SendMessageW(w->parent, 0x111,
						    (uintptr_t)w->menu,
						    (intptr_t)hwnd);
			}
			return 0;
		}
	}
	if (kind == 2) {
		if (msg == 0x170 || msg == 0x172) {
			HANDLE old = c->image;
			c->image = (HANDLE)(msg == 0x170 ? wp : lp);
			return (intptr_t)old;
		}
		if (msg == 0x171 || msg == 0x173)
			return (intptr_t)c->image;
	}
	if (kind == 3) {
		if (msg == 0xb1) {
			int len = lstrlenW(text);
			c->min = (int)wp < 0 ? len : (int)wp;
			c->max = (int)lp < 0 ? len : (int)lp;
			return 0;
		}
		if (msg == 0xb0) {
			if (wp)
				*(DWORD *)wp = c->min;
			if (lp)
				*(DWORD *)lp = c->max;
			return (uint16_t)c->min |
			       ((uint32_t)(uint16_t)c->max << 16);
		}
	}
	if (kind == 4 || kind == 5) {
		if (msg == (kind == 4 ? 0x180 : 0x143))
			return pe_control_insert(c, -1, (const uint16_t *)lp,
						 0);
		if (msg == (kind == 4 ? 0x181 : 0x14a))
			return pe_control_insert(c, (int)wp,
						 (const uint16_t *)lp, 0);
		if (msg == (kind == 4 ? 0x182 : 0x144))
			return pe_control_delete(c, wp);
		if (msg == (kind == 4 ? 0x184 : 0x14b)) {
			pe_control_reset(c);
			return 0;
		}
		if (msg == (kind == 4 ? 0x18b : 0x146))
			return c->count;
		if (msg == (kind == 4 ? 0x186 : 0x14e)) {
			c->selection = (int)wp >= 0 && wp < c->count ? (int)wp :
								       -1;
			return c->selection;
		}
		if (msg == (kind == 4 ? 0x188 : 0x147))
			return c->selection;
		if (msg == (kind == 4 ? 0x18a : 0x149))
			return wp < c->count ? lstrlenW(c->items[wp].text) : -1;
		if (msg == (kind == 4 ? 0x189 : 0x148)) {
			if (wp >= c->count || !lp)
				return -1;
			unsigned n = lstrlenW(c->items[wp].text);
			memcpy((void *)lp, c->items[wp].text, (n + 1) * 2);
			return n;
		}
		if (msg == (kind == 4 ? 0x19a : 0x151)) {
			if (wp >= c->count)
				return -1;
			c->items[wp].data = lp;
			return 0;
		}
		if (msg == (kind == 4 ? 0x199 : 0x150))
			return wp < c->count ? c->items[wp].data : -1;
	}
	if (kind == 6) {
		if (msg == 0x41e) {
			if (wp >= 20 && wp <= 128)
				c->button_size = wp;
			return 0;
		}
		if (msg == 0x444 || msg == 0x414) {
			if (!lp || wp > 4096)
				return 0;
			unsigned total = c->button_count + wp;
			unsigned char *buttons =
				pe_raw_map(total * c->button_size);
			if (!buttons)
				return 0;
			if (c->buttons) {
				memcpy(buttons, c->buttons,
				       c->button_count * c->button_size);
				raw_syscall3(SYS_munmap, (long)c->buttons,
					     c->button_count * c->button_size,
					     0);
			}
			memcpy(buttons + c->button_count * c->button_size,
			       (void *)lp, wp * c->button_size);
			c->buttons = buttons;
			c->button_count = total;
			return 1;
		}
		if (msg == 0x418)
			return c->button_count;
		if (msg == 0x417) {
			if (wp >= c->button_count || !lp)
				return 0;
			memcpy((void *)lp, c->buttons + wp * c->button_size,
			       c->button_size);
			return 1;
		}
		if (msg == 0x43a)
			return 24 | (24 << 16);
		if (msg == 0x453 && lp) {
			((int32_t *)lp)[0] = c->button_count * 24;
			((int32_t *)lp)[1] = 24;
			return 1;
		}
		if (msg == 0x430 || msg == 0x436) {
			HANDLE old = c->image;
			c->image = (HANDLE)lp;
			return (intptr_t)old;
		}
	}
	if (kind == 7) {
		if (msg == 0x40a || msg == 0x401) {
			if (!lp)
				return 0;
			return pe_control_insert(c, wp, NULL, 0) >= 0;
		}
		if (msg == 0x402)
			return pe_control_delete(c, wp) >= 0;
		if (msg == 0x40c)
			return c->count;
		if (msg == 0x41b)
			return 28;
		if (msg == 0x404)
			return lp != 0;
	}
	if (kind == 8) {
		if (msg == 0x133e) {
			if (!lp)
				return -1;
			const unsigned char *item = (void *)lp;
			const uint16_t *name =
				(*(DWORD *)item & 1) ?
					*(const uint16_t **)(item + 16) :
					NULL;
			return pe_control_insert(c, wp, name, 0);
		}
		if (msg == 0x1304)
			return c->count;
		if (msg == 0x1308)
			return pe_control_delete(c, wp) >= 0;
		if (msg == 0x1309) {
			pe_control_reset(c);
			return 1;
		}
		if (msg == 0x130b)
			return c->selection;
		if (msg == 0x130c) {
			int old = c->selection;
			c->selection = wp < c->count ? (int)wp : -1;
			return old;
		}
		if (msg == 0x1328 && lp) {
			struct pe_rect *r = (void *)lp;
			int d = wp ? 1 : -1;
			r->left -= 2 * d;
			r->top -= 24 * d;
			r->right += 2 * d;
			r->bottom += 2 * d;
			return 0;
		}
	}
	if (kind == 9) {
		if (msg == 0x404) {
			if (wp > 256 || (!lp && wp))
				return 0;
			int *parts = wp ? pe_raw_map(wp * 4) : NULL;
			if (wp && !parts)
				return 0;
			if (wp)
				memcpy(parts, (void *)lp, wp * 4);
			if (c->parts)
				raw_syscall3(SYS_munmap, (long)c->parts,
					     c->part_count * 4, 0);
			c->parts = parts;
			c->part_count = wp;
			return 1;
		}
		if (msg == 0x406) {
			unsigned n = wp < c->part_count ? wp : c->part_count;
			if (lp && n)
				memcpy((void *)lp, c->parts, n * 4);
			return c->part_count;
		}
		if (msg == 0x40b) {
			unsigned index = wp & 255;
			while (c->count <= index)
				if (pe_control_insert(c, -1, NULL, 0) < 0)
					return 0;
			uint16_t *copy = pe_control_copy((void *)lp);
			if (!copy)
				return 0;
			uint16_t *old = c->items[index].text;
			raw_syscall3(SYS_munmap, (long)old,
				     (lstrlenW(old) + 1) * 2, 0);
			c->items[index].text = copy;
			return 1;
		}
		if (msg == 0x40d || msg == 0x40c) {
			unsigned index = wp & 255;
			if (index >= c->count)
				return 0;
			unsigned n = lstrlenW(c->items[index].text);
			if (msg == 0x40d && lp)
				memcpy((void *)lp, c->items[index].text,
				       (n + 1) * 2);
			return n;
		}
	}
	if (kind == 10) {
		if (msg == 0x432) {
			c->count++;
			return 1;
		}
		if (msg == 0x433) {
			if (c->count)
				c->count--;
			return 0;
		}
		if (msg == 0x40d)
			return c->count;
	}
	if (kind == 11) {
		if (msg == 0x1004)
			return c->count;
		if (msg == 0x104d) {
			if (!lp)
				return -1;
			return pe_control_insert(
				c, *(int32_t *)((char *)lp + 4),
				(*(DWORD *)lp & 1) ?
					*(uint16_t **)((char *)lp + 24) :
					NULL,
				0);
		}
		if (msg == 0x1009) {
			pe_control_reset(c);
			return 1;
		}
		if (msg == 0x1008)
			return pe_control_delete(c, wp) >= 0;
	}
	if (kind == 12) {
		if (msg == 0x465) {
			c->min = (int16_t)(lp >> 16);
			c->max = (int16_t)lp;
			return 0;
		}
		if (msg == 0x466)
			return (uint16_t)c->max |
			       ((uint32_t)(uint16_t)c->min << 16);
		if (msg == 0x46f) {
			c->min = wp;
			c->max = lp;
			return 0;
		}
		if (msg == 0x470) {
			if (wp)
				*(int *)wp = c->min;
			if (lp)
				*(int *)lp = c->max;
			return 0;
		}
		if (msg == 0x467 || msg == 0x471) {
			int old = c->position,
			    v = msg == 0x467 ? (int16_t)lp : (int)lp;
			if (v < c->min)
				v = c->min;
			if (v > c->max)
				v = c->max;
			c->position = v;
			return old;
		}
		if (msg == 0x468 || msg == 0x472) {
			if (msg == 0x472 && lp)
				*(BOOL *)lp = 0;
			return c->position;
		}
		if (msg == 0x469) {
			HANDLE old = c->buddy;
			c->buddy = (HANDLE)wp;
			return (intptr_t)old;
		}
		if (msg == 0x46a)
			return (intptr_t)c->buddy;
	}
	pe_state_leave(&pe_guard);
	return DefWindowProcW(hwnd, msg, wp, lp);
}
static void pe_control_clear(struct pe_window *w)
{
	struct pe_control *c = w->control;
	if (!c)
		return;
	/* Tooltip count describes external tool records, not allocated list items. */
	if (w->cls->builtin == 10)
		c->count = 0;
	pe_control_reset(c);
	if (c->items)
		raw_syscall3(SYS_munmap, (long)c->items,
			     c->capacity * sizeof(*c->items), 0);
	if (c->text)
		raw_syscall3(SYS_munmap, (long)c->text, c->text_bytes, 0);
	if (c->parts)
		raw_syscall3(SYS_munmap, (long)c->parts, c->part_count * 4, 0);
	if (c->buttons)
		raw_syscall3(SYS_munmap, (long)c->buttons,
			     c->button_count * c->button_size, 0);
	raw_syscall3(SYS_munmap, (long)c, sizeof(*c), 0);
	w->control = NULL;
}

static const uint16_t *pe_control_caption(struct pe_window *w)
{
	return w->control && w->control->text ? w->control->text : w->title;
}
