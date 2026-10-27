#define PE_MENU_HEIGHT 24
struct pe_menu_item {
	DWORD type, state, id;
	HANDLE submenu;
	uintptr_t data;
	uint16_t *text;
	HANDLE checked, unchecked, bitmap;
};
struct pe_menu {
	struct pe_menu_item *items;
	unsigned count, capacity;
	bool popup;
	HANDLE owner;
};
struct pe_menu_info {
	DWORD size, mask, type, state, id;
	HANDLE submenu, checked, unchecked;
	uintptr_t data;
	uint16_t *text;
	DWORD chars;
	HANDLE bitmap;
};
_Static_assert(sizeof(struct pe_menu_info) == 80, "MENUITEMINFOW ABI");
static struct pe_menu *pe_menu_lookup(HANDLE h)
{
	struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_MENU);
	return o ? o->menu : NULL;
}
__attribute__((ms_abi)) HANDLE CreateMenu(void)
{
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_new(PE_GUI_MENU);
	if (!o)
		return NULL;
	o->menu = pe_raw_map(sizeof(*o->menu));
	if (!o->menu) {
		pe_gui_drop(o);
		SetLastError(8);
		return NULL;
	}
	return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreatePopupMenu(void)
{
	PE_STATE_GUARD;
	HANDLE h = CreateMenu();
	if (h)
		pe_menu_lookup(h)->popup = true;
	return h;
}
static struct pe_menu_item *pe_menu_find(HANDLE menu, unsigned value,
					 BOOL position, unsigned depth)
{
	if (depth > 32)
		return NULL;
	struct pe_menu *m = pe_menu_lookup(menu);
	if (!m)
		return NULL;
	if (position)
		return value < m->count ? &m->items[value] : NULL;
	for (unsigned i = 0; i < m->count; i++) {
		if (m->items[i].id == value)
			return &m->items[i];
		if (m->items[i].submenu) {
			struct pe_menu_item *found = pe_menu_find(
				m->items[i].submenu, value, 0, depth + 1);
			if (found)
				return found;
		}
	}
	return NULL;
}
static bool pe_menu_has(HANDLE menu, HANDLE wanted, unsigned depth)
{
	if (menu == wanted)
		return true;
	if (depth > 32)
		return true;
	struct pe_menu *m = pe_menu_lookup(menu);
	if (!m)
		return false;
	for (unsigned i = 0; i < m->count; i++)
		if (m->items[i].submenu &&
		    pe_menu_has(m->items[i].submenu, wanted, depth + 1))
			return true;
	return false;
}
/* Called under the state lock. The owning thread uploads on its next pump. */
static void pe_menus_dirty(void)
{
	for (struct pe_window *w = pe_windows; w; w = w->next)
		if (!w->dead && w->menu)
			w->menu_dirty = true;
}
static BOOL pe_menu_apply(HANDLE menu, struct pe_menu_item *item,
			  const struct pe_menu_info *info)
{
	uint16_t *text = NULL;
	if ((info->mask & (0x40 | 0x10)) && !(info->type & (0x4 | 0x100))) {
		text = pe_control_copy(info->text);
		if (!text) {
			SetLastError(8);
			return 0;
		}
	}
	if ((info->mask & 4) && info->submenu &&
	    (!pe_menu_lookup(info->submenu) ||
	     pe_menu_has(info->submenu, menu, 0))) {
		if (text)
			raw_syscall3(SYS_munmap, (long)text,
				     (lstrlenW(text) + 1) * 2, 0);
		SetLastError(87);
		return 0;
	}
	if (info->mask & (0x100 | 0x10))
		item->type = info->type;
	if (info->mask & 1)
		item->state = info->state;
	if (info->mask & 2)
		item->id = info->id;
	if (info->mask & 4)
		item->submenu = info->submenu;
	if (info->mask & 8) {
		item->checked = info->checked;
		item->unchecked = info->unchecked;
	}
	if (info->mask & 0x20)
		item->data = info->data;
	if (info->mask & 0x80)
		item->bitmap = info->bitmap;
	if (text) {
		if (item->text)
			raw_syscall3(SYS_munmap, (long)item->text,
				     (lstrlenW(item->text) + 1) * 2, 0);
		item->text = text;
	}
	return 1;
}
__attribute__((ms_abi)) BOOL InsertMenuItemW(HANDLE h, unsigned value,
					     BOOL position,
					     const struct pe_menu_info *info)
{
	if (!info || info->size != 80) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_menu *m = pe_menu_lookup(h);
	if (!m)
		return 0;
	unsigned index = value;
	if (!position) {
		index = m->count;
		for (unsigned i = 0; i < m->count; i++)
			if (m->items[i].id == value) {
				index = i;
				break;
			}
	}
	if (index > m->count)
		index = m->count;
	pe_menus_dirty();
	struct pe_menu_item item = { 0 };
	if (!pe_menu_apply(h, &item, info))
		return 0;
	if (m->count == m->capacity) {
		unsigned cap = m->capacity ? m->capacity * 2 : 16;
		struct pe_menu_item *items = pe_raw_map(cap * sizeof(*items));
		if (!items) {
			if (item.text)
				raw_syscall3(SYS_munmap, (long)item.text,
					     (lstrlenW(item.text) + 1) * 2, 0);
			SetLastError(8);
			return 0;
		}
		if (m->count)
			memcpy(items, m->items, m->count * sizeof(*items));
		if (m->items)
			raw_syscall3(SYS_munmap, (long)m->items,
				     m->capacity * sizeof(*items), 0);
		m->items = items;
		m->capacity = cap;
	}
	memmove(m->items + index + 1, m->items + index,
		(m->count - index) * sizeof(*m->items));
	m->items[index] = item;
	m->count++;
	return 1;
}
__attribute__((ms_abi)) BOOL AppendMenuW(HANDLE h, unsigned flags, uintptr_t id,
					 const uint16_t *text)
{
	struct pe_menu_info info = { .size = 80,
				     .mask = 0x143,
				     .type = flags & ~0x49bu,
				     .state = flags & 0xb,
				     .id = id,
				     .text = (uint16_t *)text };
	if (flags & 0x10) {
		info.mask |= 4;
		info.submenu = (HANDLE)id;
		info.id = 0;
	}
	return InsertMenuItemW(h, UINT32_MAX, 1, &info);
}
__attribute__((ms_abi)) BOOL SetMenuItemInfoW(HANDLE h, unsigned value,
					      BOOL position,
					      const struct pe_menu_info *info)
{
	if (!info || info->size != 80) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_menu_item *item = pe_menu_find(h, value, position, 0);
	if (!item) {
		SetLastError(1456);
		return 0;
	}
	pe_menus_dirty();
	return pe_menu_apply(h, item, info);
}
__attribute__((ms_abi)) BOOL GetMenuItemInfoW(HANDLE h, unsigned value,
					      BOOL position,
					      struct pe_menu_info *info)
{
	if (!info || info->size != 80) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_menu_item *item = pe_menu_find(h, value, position, 0);
	if (!item) {
		SetLastError(1456);
		return 0;
	}
	if (info->mask & (0x100 | 0x10))
		info->type = item->type;
	if (info->mask & 1)
		info->state = item->state;
	if (info->mask & 2)
		info->id = item->id;
	if (info->mask & 4)
		info->submenu = item->submenu;
	if (info->mask & 8) {
		info->checked = item->checked;
		info->unchecked = item->unchecked;
	}
	if (info->mask & 0x20)
		info->data = item->data;
	if (info->mask & 0x80)
		info->bitmap = item->bitmap;
	if (info->mask & (0x40 | 0x10)) {
		unsigned len = item->text ? lstrlenW(item->text) : 0, n = len;
		if (info->text && info->chars) {
			if (n >= info->chars)
				n = info->chars - 1;
			if (n)
				memcpy(info->text, item->text, n * 2);
			info->text[n] = 0;
		}
		info->chars = info->text ? n : len;
	}
	return 1;
}
__attribute__((ms_abi)) int GetMenuItemCount(HANDLE h)
{
	PE_STATE_GUARD;
	struct pe_menu *m = pe_menu_lookup(h);
	return m ? m->count : -1;
}
__attribute__((ms_abi)) HANDLE GetSubMenu(HANDLE h, int index)
{
	PE_STATE_GUARD;
	struct pe_menu *m = pe_menu_lookup(h);
	return m && index >= 0 && (unsigned)index < m->count ?
		       m->items[index].submenu :
		       NULL;
}
__attribute__((ms_abi)) unsigned GetMenuItemID(HANDLE h, int index)
{
	PE_STATE_GUARD;
	struct pe_menu *m = pe_menu_lookup(h);
	return m && index >= 0 && (unsigned)index < m->count &&
			       !m->items[index].submenu ?
		       m->items[index].id :
		       UINT32_MAX;
}
__attribute__((ms_abi)) HANDLE GetMenu(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	return w ? w->menu : NULL;
}
__attribute__((ms_abi)) BOOL SetMenu(HANDLE hwnd, HANDLE menu)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	if (menu && !pe_menu_lookup(menu))
		return 0;
	w->menu = menu;
	w->menu_dirty = true;
	if (menu)
		pe_menu_lookup(menu)->owner = hwnd;
	pe_invalidate_full(w);
	return 1;
}
__attribute__((ms_abi)) unsigned CheckMenuItem(HANDLE h, unsigned id,
					       unsigned flags)
{
	PE_STATE_GUARD;
	pe_menus_dirty();
	struct pe_menu_item *item = pe_menu_find(h, id, flags & 0x400, 0);
	if (!item)
		return UINT32_MAX;
	unsigned old = item->state & 8;
	item->state = (item->state & ~8u) | (flags & 8);
	return old;
}
__attribute__((ms_abi)) unsigned EnableMenuItem(HANDLE h, unsigned id,
						unsigned flags)
{
	PE_STATE_GUARD;
	pe_menus_dirty();
	struct pe_menu_item *item = pe_menu_find(h, id, flags & 0x400, 0);
	if (!item)
		return UINT32_MAX;
	unsigned old = item->state & 3;
	item->state = (item->state & ~3u) | (flags & 3);
	return old;
}
static BOOL pe_menu_destroy(HANDLE h, unsigned depth)
{
	if (depth > 32)
		return 0;
	struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_MENU);
	if (!o)
		return 0;
	struct pe_menu *m = o->menu;
	for (unsigned i = 0; i < m->count; i++) {
		if (m->items[i].submenu)
			pe_menu_destroy(m->items[i].submenu, depth + 1);
		if (m->items[i].text)
			raw_syscall3(SYS_munmap, (long)m->items[i].text,
				     (lstrlenW(m->items[i].text) + 1) * 2, 0);
	}
	if (m->items)
		raw_syscall3(SYS_munmap, (long)m->items,
			     m->capacity * sizeof(*m->items), 0);
	raw_syscall3(SYS_munmap, (long)m, sizeof(*m), 0);
	pe_gui_drop(o);
	return 1;
}
__attribute__((ms_abi)) BOOL DestroyMenu(HANDLE h)
{
	PE_STATE_GUARD;
	for (struct pe_window *w = pe_windows; w; w = w->next)
		if (w->menu == h) {
			w->menu = NULL;
			w->menu_dirty = true;
		}
	return pe_menu_destroy(h, 0);
}
static bool pe_menu_word(const unsigned char *data, size_t size, size_t *pos,
			 uint16_t *out)
{
	if (*pos > size || size - *pos < 2)
		return false;
	memcpy(out, data + *pos, 2);
	*pos += 2;
	return true;
}
static bool pe_menu_dword(const unsigned char *data, size_t size, size_t *pos,
			  uint32_t *out)
{
	if (*pos > size || size - *pos < 4)
		return false;
	memcpy(out, data + *pos, 4);
	*pos += 4;
	return true;
}
static HANDLE pe_menu_parse(const unsigned char *data, size_t size, size_t *pos,
			    bool extended, unsigned depth)
{
	if (depth > 32) {
		SetLastError(1812);
		return NULL;
	}
	HANDLE menu = depth ? CreatePopupMenu() : CreateMenu();
	if (!menu)
		return NULL;
	unsigned total = 0;
	for (;;) {
		if (++total > 65536)
			break;
		uint32_t type = 0, state = 0, id = 0;
		uint16_t flags = 0;
		if (extended) {
			*pos = (*pos + 3) & ~3ULL;
			if (!pe_menu_dword(data, size, pos, &type) ||
			    !pe_menu_dword(data, size, pos, &state) ||
			    !pe_menu_dword(data, size, pos, &id) ||
			    !pe_menu_word(data, size, pos, &flags))
				break;
		} else {
			if (!pe_menu_word(data, size, pos, &flags))
				break;
			if (!(flags & 16)) {
				uint16_t value;
				if (!pe_menu_word(data, size, pos, &value))
					break;
				id = value;
			}
			type = flags & 0x960;
			state = flags & 0xb;
		}
		size_t start = *pos;
		uint16_t c;
		do {
			if (!pe_menu_word(data, size, pos, &c))
				goto failed;
		} while (c);
		bool popup = flags & (extended ? 1 : 16);
		HANDLE child = NULL;
		if (popup) {
			if (extended) {
				uint32_t help;
				*pos = (*pos + 3) & ~3ULL;
				if (!pe_menu_dword(data, size, pos, &help))
					break;
			}
			child = pe_menu_parse(data, size, pos, extended,
					      depth + 1);
			if (!child)
				break;
		}
		struct pe_menu_info info = { .size = 80,
					     .mask = 0x147,
					     .type = type,
					     .state = state,
					     .id = id,
					     .submenu = child,
					     .text = (uint16_t *)(data +
								  start) };
		if (!InsertMenuItemW(menu, UINT32_MAX, 1, &info)) {
			if (child)
				DestroyMenu(child);
			break;
		}
		if (flags & 128)
			return menu;
	}
failed:
	DestroyMenu(menu);
	SetLastError(1812);
	return NULL;
}
__attribute__((ms_abi)) HANDLE LoadMenuW(HANDLE module, const uint16_t *name)
{
	HANDLE resource = FindResourceW(module, name, (void *)4);
	if (!resource)
		return NULL;
	struct pe_resource_data d;
	if (!pe_resource_data(module, resource, &d) || d.size < 4)
		return NULL;
	unsigned char *data = pe_raw_map(d.size);
	if (!data) {
		SetLastError(8);
		return NULL;
	}
	HANDLE menu = NULL;
	if (diagnostic_image_read(pe_main_base + d.rva, data, d.size)) {
		uint16_t version, offset;
		memcpy(&version, data, 2);
		memcpy(&offset, data + 2, 2);
		size_t pos = 4 + offset;
		if (version <= 1 && pos <= d.size)
			menu = pe_menu_parse(data, d.size, &pos, version == 1,
					     0);
		else
			SetLastError(1812);
	}
	raw_syscall3(SYS_munmap, (long)data, d.size, 0);
	return menu;
}
struct pe_accel {
	uint8_t flags, pad;
	uint16_t key, command;
};
struct pe_accel_table {
	unsigned count;
	struct pe_accel *entries;
};
__attribute__((ms_abi)) HANDLE LoadAcceleratorsW(HANDLE module,
						 const uint16_t *name)
{
	HANDLE r = FindResourceW(module, name, (void *)9);
	if (!r)
		return NULL;
	struct pe_resource_data d;
	if (!pe_resource_data(module, r, &d) || !d.size || d.size % 8) {
		SetLastError(1812);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_new(24);
	if (!o)
		return NULL;
	struct pe_accel_table *a = pe_raw_map(sizeof(*a));
	if (!a) {
		pe_gui_drop(o);
		return NULL;
	}
	a->count = d.size / 8;
	a->entries = pe_raw_map(a->count * sizeof(*a->entries));
	if (!a->entries) {
		raw_syscall3(SYS_munmap, (long)a, sizeof(*a), 0);
		pe_gui_drop(o);
		return NULL;
	}
	for (unsigned i = 0; i < a->count; i++) {
		uint16_t words[4];
		if (!diagnostic_image_read(pe_main_base + d.rva + i * 8, words,
					   8)) {
			raw_syscall3(SYS_munmap, (long)a->entries,
				     a->count * sizeof(*a->entries), 0);
			raw_syscall3(SYS_munmap, (long)a, sizeof(*a), 0);
			pe_gui_drop(o);
			return NULL;
		}
		a->entries[i] = (struct pe_accel){ words[0] & 0x7f, 0, words[1],
						   words[2] };
	}
	o->accelerator = a;
	return pe_gui_handle(o);
}
__attribute__((ms_abi)) int TranslateAcceleratorW(HANDLE hwnd, HANDLE table,
						  const struct pe_msg *msg)
{
	if (!msg || !pe_window_lookup(hwnd, true))
		return 0;
	struct pe_accel found = { 0 };
	bool match = false;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *o = pe_gui_lookup(table, 24);
		if (!o)
			return 0;
		struct pe_ui *u = pe_ui_current();
		for (unsigned i = 0; i < o->accelerator->count; i++) {
			struct pe_accel a = o->accelerator->entries[i];
			bool virtual = !!(a.flags & 1);
			if (a.key != msg->wparam ||
			    (virtual ? (msg->message != 0x100 &&
					msg->message != 0x104) :
				       msg->message != 0x102))
				continue;
			if (virtual && (!!(a.flags & 4) != !!u->keys[16] ||
					!!(a.flags & 8) != !!u->keys[17] ||
					!!(a.flags & 16) != !!u->keys[18]))
				continue;
			found = a;
			match = true;
			break;
		}
	}
	if (!match)
		return 0;
	SendMessageW(hwnd, 0x111, found.command | 0x10000u, 0);
	return 1;
}
__attribute__((ms_abi)) BOOL AdjustWindowRectEx(struct pe_rect *rect,
						DWORD style, BOOL menu,
						DWORD ex)
{
	(void)style;
	(void)ex;
	if (!rect) {
		SetLastError(87);
		return 0;
	}
	if (menu)
		rect->top -= PE_MENU_HEIGHT;
	return 1;
}
__attribute__((ms_abi)) BOOL AdjustWindowRect(struct pe_rect *rect, DWORD style,
					      BOOL menu)
{
	return AdjustWindowRectEx(rect, style, menu, 0);
}
static int pe_window_menu_height(struct pe_window *w)
{
	return w->menu && !(w->style & 0x40000000u) ? PE_MENU_HEIGHT : 0;
}
static bool pe_menu_wire(struct pe_window *w, HANDLE menu, unsigned depth)
{
	if (depth > 32)
		return false;
	unsigned count;
	{
		PE_STATE_GUARD;
		struct pe_menu *m = pe_menu_lookup(menu);
		if (!m)
			return false;
		count = m->count;
	}
	for (unsigned i = 0; i < count; i++) {
		struct pg_packet p = { .op = PG_MENU_ITEM,
				       .id = (uintptr_t)w->handle,
				       .a = i,
				       .d = (uintptr_t)menu };
		HANDLE child;
		{
			PE_STATE_GUARD;
			struct pe_menu *m = pe_menu_lookup(menu);
			if (!m || i >= m->count)
				return false;
			struct pe_menu_item *item = &m->items[i];
			child = item->submenu;
			p.b = item->id;
			p.c = (uintptr_t)child;
			p.flags = item->state | item->type;
			if (item->text &&
			    !WideCharToMultiByte(65001, 0, item->text, -1,
						 p.text, PG_TEXT, NULL, NULL))
				return false;
		}
		if (!pe_ui_rpc(w->ui, &p))
			return false;
		if (child && !pe_menu_wire(w, child, depth + 1))
			return false;
	}
	return true;
}
static bool pe_menu_sync(struct pe_window *w)
{
	if (!w->realized)
		return true;
	struct pg_packet p = { .op = PG_MENU_BEGIN,
			       .id = (uintptr_t)w->handle,
			       .a = (uintptr_t)w->menu,
			       .b = pe_window_menu_height(w) };
	if (!pe_ui_rpc(w->ui, &p))
		return false;
	if (w->menu && !pe_menu_wire(w, w->menu, 0))
		return false;
	p = (struct pg_packet){ .op = PG_MENU_END, .id = (uintptr_t)w->handle };
	if (!pe_ui_rpc(w->ui, &p))
		return false;
	w->menu_dirty = false;
	return true;
}
__attribute__((ms_abi)) BOOL DrawMenuBar(HANDLE hwnd)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	return w ? pe_menu_sync(w) : 0;
}
static bool pe_menu_location(HANDLE h, unsigned id, BOOL position,
			     struct pe_menu **parent, unsigned *index,
			     unsigned depth)
{
	if (depth > 32)
		return false;
	struct pe_menu *m = pe_menu_lookup(h);
	if (!m)
		return false;
	if (position) {
		if (id >= m->count)
			return false;
		*parent = m;
		*index = id;
		return true;
	}
	for (unsigned i = 0; i < m->count; i++) {
		if (m->items[i].id == id) {
			*parent = m;
			*index = i;
			return true;
		}
		if (m->items[i].submenu &&
		    pe_menu_location(m->items[i].submenu, id, 0, parent, index,
				     depth + 1))
			return true;
	}
	return false;
}
static BOOL pe_menu_remove(HANDLE h, unsigned id, unsigned flags, bool destroy)
{
	PE_STATE_GUARD;
	struct pe_menu *m;
	unsigned index;
	if (!pe_menu_location(h, id, flags & 0x400, &m, &index, 0)) {
		SetLastError(1456);
		return 0;
	}
	struct pe_menu_item item = m->items[index];
	memmove(m->items + index, m->items + index + 1,
		(m->count - index - 1) * sizeof(*m->items));
	m->count--;
	pe_menus_dirty();
	if (item.text)
		raw_syscall3(SYS_munmap, (long)item.text,
			     (lstrlenW(item.text) + 1) * 2, 0);
	if (destroy && item.submenu)
		DestroyMenu(item.submenu);
	return 1;
}
__attribute__((ms_abi)) BOOL RemoveMenu(HANDLE h, unsigned id, unsigned flags)
{
	return pe_menu_remove(h, id, flags, false);
}
__attribute__((ms_abi)) BOOL DeleteMenu(HANDLE h, unsigned id, unsigned flags)
{
	return pe_menu_remove(h, id, flags, true);
}
__attribute__((ms_abi)) BOOL InsertMenuW(HANDLE h, unsigned position,
					 unsigned flags, uintptr_t id,
					 const uint16_t *text)
{
	struct pe_menu_info info = { .size = 80,
				     .mask = 0x143,
				     .type = flags & ~0x49bu,
				     .state = flags & 0xb,
				     .id = id,
				     .text = (uint16_t *)text };
	if (flags & 16) {
		info.mask |= 4;
		info.submenu = (HANDLE)id;
		info.id = 0;
	}
	return InsertMenuItemW(h, position, flags & 0x400, &info);
}
__attribute__((ms_abi)) BOOL ModifyMenuW(HANDLE h, unsigned position,
					 unsigned flags, uintptr_t id,
					 const uint16_t *text)
{
	struct pe_menu_info info = { .size = 80,
				     .mask = 0x147,
				     .type = flags & ~0x49bu,
				     .state = flags & 0xb,
				     .id = id,
				     .text = (uint16_t *)text };
	if (flags & 16) {
		info.submenu = (HANDLE)id;
		info.id = 0;
	}
	return SetMenuItemInfoW(h, position, flags & 0x400, &info);
}
__attribute__((ms_abi)) BOOL AppendMenuA(HANDLE h, unsigned flags, uintptr_t id,
					 const char *text)
{
	uint16_t wide[1024];
	if (text) {
		if (!MultiByteToWideChar(65001, 8, text, -1, wide, 1024))
			return 0;
		return AppendMenuW(h, flags, id, wide);
	}
	return AppendMenuW(h, flags, id, NULL);
}
__attribute__((ms_abi)) unsigned GetMenuState(HANDLE h, unsigned id,
					      unsigned flags)
{
	PE_STATE_GUARD;
	struct pe_menu_item *item = pe_menu_find(h, id, flags & 0x400, 0);
	if (!item)
		return UINT32_MAX;
	return item->type | item->state |
	       (item->submenu ?
			(16 | ((GetMenuItemCount(item->submenu) & 255) << 8)) :
			0);
}
__attribute__((ms_abi)) int GetMenuStringW(HANDLE h, unsigned id, uint16_t *out,
					   int cap, unsigned flags)
{
	if (cap < 0) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_menu_item *item = pe_menu_find(h, id, flags & 0x400, 0);
	if (!item)
		return 0;
	unsigned n = item->text ? lstrlenW(item->text) : 0;
	if (!out || !cap)
		return n;
	if (n >= (unsigned)cap)
		n = cap - 1;
	if (n)
		memcpy(out, item->text, n * 2);
	out[n] = 0;
	return n;
}
__attribute__((ms_abi)) BOOL CheckMenuRadioItem(HANDLE h, unsigned first,
						unsigned last,
						unsigned selected,
						unsigned flags)
{
	PE_STATE_GUARD;
	pe_menus_dirty();
	bool found = false;
	for (uint64_t i = first; i <= last && i - first < 65536; i++) {
		struct pe_menu_item *item =
			pe_menu_find(h, i, flags & 0x400, 0);
		if (item) {
			item->type |= 0x200;
			item->state = (item->state & ~8u) |
				      (i == selected ? 8 : 0);
			found = true;
		}
	}
	return found;
}
__attribute__((ms_abi)) BOOL SetMenuItemBitmaps(HANDLE h, unsigned id,
						unsigned flags,
						HANDLE unchecked,
						HANDLE checked)
{
	PE_STATE_GUARD;
	pe_menus_dirty();
	struct pe_menu_item *item = pe_menu_find(h, id, flags & 0x400, 0);
	if (!item)
		return 0;
	item->unchecked = unchecked;
	item->checked = checked;
	return 1;
}
__attribute__((ms_abi)) HANDLE
CreateAcceleratorTableW(const struct pe_accel *entries, int count)
{
	if (!entries || count < 1 || count > 65536) {
		SetLastError(87);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_new(24);
	if (!o)
		return NULL;
	struct pe_accel_table *a = pe_raw_map(sizeof(*a));
	if (!a) {
		pe_gui_drop(o);
		return NULL;
	}
	a->count = count;
	a->entries = pe_raw_map(count * sizeof(*entries));
	if (!a->entries) {
		raw_syscall3(SYS_munmap, (long)a, sizeof(*a), 0);
		pe_gui_drop(o);
		return NULL;
	}
	memcpy(a->entries, entries, count * sizeof(*entries));
	o->accelerator = a;
	return pe_gui_handle(o);
}
__attribute__((ms_abi)) BOOL DestroyAcceleratorTable(HANDLE table)
{
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_lookup(table, 24);
	if (!o)
		return 0;
	raw_syscall3(SYS_munmap, (long)o->accelerator->entries,
		     o->accelerator->count * sizeof(struct pe_accel), 0);
	raw_syscall3(SYS_munmap, (long)o->accelerator, sizeof(*o->accelerator),
		     0);
	pe_gui_drop(o);
	return 1;
}
