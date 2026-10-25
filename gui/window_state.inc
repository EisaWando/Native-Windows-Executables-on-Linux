/* Logical window state exists independently of the native presentation surface. */
struct pe_windowpos {
	HANDLE hwnd, after;
	int32_t x, y, width, height;
	DWORD flags;
};
_Static_assert(sizeof(struct pe_windowpos) == 40, "WINDOWPOS ABI");
__attribute__((ms_abi)) BOOL SetWindowPos(HANDLE hwnd, HANDLE after, int x,
					  int y, int width, int height,
					  DWORD flags)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	struct pe_windowpos pos = { hwnd, after, x, y, width, height, flags };
	if (!(flags & 0x400))
		w->proc(hwnd, 0x46, 0, (intptr_t)&pos);
	if (w->dead)
		return 0;
	x = pos.flags & 2 ? w->x : pos.x;
	y = pos.flags & 2 ? w->y : pos.y;
	width = pos.flags & 1 ? w->width : pos.width;
	height = pos.flags & 1 ? w->height : pos.height;
	if (x == INT32_MIN)
		x = 80;
	if (y == INT32_MIN)
		y = 80;
	if (width == INT32_MIN)
		width = 640;
	if (height == INT32_MIN)
		height = 400;
	if (w->realized) {
		if (width > PG_MAX_DIM || height > PG_MAX_DIM) {
			SetLastError(8);
			return 0;
		}
		struct pg_packet p = { .op = PG_POSITION,
				       .id = (uintptr_t)hwnd,
				       .a = x,
				       .b = y,
				       .c = width > 0 ? width : 1,
				       .d = height > 0 ? height : 1,
				       .flags = pos.flags };
		if (!pe_ui_rpc(w->ui, &p))
			return 0;
	}
	bool moved = x != w->x || y != w->y,
	     resized = width != w->width || height != w->height;
	{
		PE_STATE_GUARD;
		w->x = x;
		w->y = y;
		w->width = width;
		w->height = height;
		if (resized) {
			w->invalid = true;
			w->erase = true;
		}
	}
	if (pos.flags & 0x40) {
		ShowWindow(hwnd, 5);
		if (!w->visible)
			return 0;
	} else if (pos.flags & 0x80)
		ShowWindow(hwnd, 0);
	pos.x = x;
	pos.y = y;
	pos.width = width;
	pos.height = height;
	w->proc(hwnd, 0x47, 0, (intptr_t)&pos);
	if (!w->dead && moved)
		w->proc(hwnd, 3, 0,
			(uint16_t)x | ((uint32_t)(uint16_t)y << 16));
	if (!w->dead && resized)
		w->proc(hwnd, 5, 0,
			(uint16_t)width | ((uint32_t)(uint16_t)(height - pe_window_menu_height(w)) << 16));
	return !w->dead;
}
__attribute__((ms_abi)) BOOL MoveWindow(HANDLE hwnd, int x, int y, int width,
					int height, BOOL repaint)
{
	BOOL ok = SetWindowPos(hwnd, NULL, x, y, width, height,
			       4 | 16 | (repaint ? 0 : 8));
	if (ok && repaint)
		UpdateWindow(hwnd);
	return ok;
}
__attribute__((ms_abi)) BOOL IsWindowEnabled(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	return w && !(w->style & 0x08000000u);
}
__attribute__((ms_abi)) HANDLE GetFocus(void)
{
	struct pe_ui *u = pe_ui_current();
	return u ? u->focus : NULL;
}
__attribute__((ms_abi)) HANDLE SetFocus(HANDLE hwnd)
{
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return NULL;
	struct pe_window *w = hwnd ? pe_window_lookup(hwnd, true) : NULL;
	if (hwnd && (!w || !IsWindowEnabled(hwnd)))
		return NULL;
	HANDLE old = u->focus;
	if (old == hwnd)
		return old;
	u->focus = hwnd;
	if (old && IsWindow(old))
		SendMessageW(old, 8, (uintptr_t)hwnd, 0);
	if (hwnd && IsWindow(hwnd))
		SendMessageW(hwnd, 7, (uintptr_t)old, 0);
	return old;
}
__attribute__((ms_abi)) BOOL EnableWindow(HANDLE hwnd, BOOL enable)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	BOOL disabled = !!(w->style & 0x08000000u);
	{
		PE_STATE_GUARD;
		if (enable)
			w->style &= ~0x08000000u;
		else
			w->style |= 0x08000000u;
	}
	if (!enable && w->ui->focus == hwnd)
		SetFocus(NULL);
	if (disabled == !!enable)
		w->proc(hwnd, 0xa, !!enable, 0);
	return disabled;
}
__attribute__((ms_abi)) HANDLE GetActiveWindow(void)
{
	struct pe_ui *u = pe_ui_current();
	return u ? u->active : NULL;
}
__attribute__((ms_abi)) HANDLE SetActiveWindow(HANDLE hwnd)
{
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return NULL;
	if (hwnd && !pe_window_lookup(hwnd, true))
		return NULL;
	HANDLE old = u->active;
	u->active = hwnd;
	if (old == hwnd)
		return old;
	if (old && IsWindow(old))
		SendMessageW(old, 6, 0, (intptr_t)hwnd);
	if (hwnd && IsWindow(hwnd))
		SendMessageW(hwnd, 6, 1, (intptr_t)old);
	return old;
}
__attribute__((ms_abi)) void DragAcceptFiles(HANDLE hwnd, BOOL accept)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (w) {
		if (accept)
			w->exstyle |= 16;
		else
			w->exstyle &= ~16u;
	}
}
__attribute__((ms_abi)) HANDLE GetParent(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	return w ? w->parent : NULL;
}
__attribute__((ms_abi)) int32_t GetWindowLongW(HANDLE hwnd, int index)
{
	return (int32_t)GetWindowLongPtrW(hwnd, index);
}
__attribute__((ms_abi)) int32_t SetWindowLongW(HANDLE hwnd, int index,
					       int32_t value)
{
	return (int32_t)SetWindowLongPtrW(hwnd, index, value);
}
