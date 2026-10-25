struct pe_caret_point {
	int32_t x, y;
};
/* Per-message-queue caret metadata. Pixel inversion/blink rendering is pending. */
static DWORD pe_caret_blink = 530;
__attribute__((ms_abi)) unsigned GetCaretBlinkTime(void)
{
	return __atomic_load_n(&pe_caret_blink, __ATOMIC_RELAXED);
}
__attribute__((ms_abi)) BOOL SetCaretBlinkTime(unsigned milliseconds)
{
	__atomic_store_n(&pe_caret_blink, milliseconds, __ATOMIC_RELAXED);
	return 1;
}
__attribute__((ms_abi)) BOOL CreateCaret(HANDLE hwnd, HANDLE bitmap, int width,
					 int height)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	if (width < 0 || height < 0) {
		SetLastError(87);
		return 0;
	}
	if ((uintptr_t)bitmap > 1) {
		struct pe_gui_object *b = pe_gui_lookup(bitmap, PE_GDI_BITMAP);
		if (!b)
			return 0;
		width = b->u.bitmap.width;
		height = b->u.bitmap.height;
	}
	struct pe_ui *u = w->ui;
	u->caret = hwnd;
	u->caret_bitmap = bitmap;
	u->caret_x = u->caret_y = 0;
	u->caret_width = width ? width : 1;
	u->caret_height = height ? height : 1;
	u->caret_hidden = 1;
	return 1;
}
__attribute__((ms_abi)) BOOL DestroyCaret(void)
{
	PE_STATE_GUARD;
	struct pe_ui *u = pe_ui_current();
	if (!u || !u->caret) {
		SetLastError(5);
		return 0;
	}
	u->caret = u->caret_bitmap = NULL;
	u->caret_hidden = 0;
	return 1;
}
static BOOL pe_caret_visibility(HANDLE hwnd, bool show)
{
	PE_STATE_GUARD;
	struct pe_ui *u = pe_ui_current();
	if (!u || !u->caret || (hwnd && hwnd != u->caret)) {
		SetLastError(5);
		return 0;
	}
	if (show) {
		if (u->caret_hidden > 0)
			u->caret_hidden--;
	} else if (u->caret_hidden < INT32_MAX)
		u->caret_hidden++;
	return 1;
}
__attribute__((ms_abi)) BOOL ShowCaret(HANDLE hwnd)
{
	return pe_caret_visibility(hwnd, true);
}
__attribute__((ms_abi)) BOOL HideCaret(HANDLE hwnd)
{
	return pe_caret_visibility(hwnd, false);
}
__attribute__((ms_abi)) BOOL SetCaretPos(int x, int y)
{
	PE_STATE_GUARD;
	struct pe_ui *u = pe_ui_current();
	if (!u || !u->caret) {
		SetLastError(5);
		return 0;
	}
	u->caret_x = x;
	u->caret_y = y;
	return 1;
}
__attribute__((ms_abi)) BOOL GetCaretPos(struct pe_caret_point *point)
{
	if (!point) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_ui *u = pe_ui_current();
	if (!u || !u->caret) {
		SetLastError(5);
		return 0;
	}
	point->x = u->caret_x;
	point->y = u->caret_y;
	return 1;
}
