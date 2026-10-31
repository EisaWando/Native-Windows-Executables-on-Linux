/* ToolbarWindow32 metrics, button geometry and TB_AUTOSIZE.
 * Button size defaults to 24x24; TB_SETBUTTONSIZE (WM_USER+31) accepts the
 * size in wParam and, like the applications that send MAKELONG there, in
 * lParam when wParam is zero. c->button_size stays the TBBUTTON record size
 * set by TB_BUTTONSTRUCTSIZE (WM_USER+30). */
static void pe_toolbar_metric(struct pe_control *c, int *cx, int *cy)
{
	*cx = c->tb_cx > 0 ? c->tb_cx : 24;
	*cy = c->tb_cy > 0 ? c->tb_cy : 24;
}
static int pe_toolbar_rows(struct pe_control *c)
{
	return c->tb_rows > 0 ? c->tb_rows : 1;
}
static void pe_toolbar_autosize_size(struct pe_control *c, int *width, int *height)
{
	int cx, cy, pad = c->tb_pad > 0 ? c->tb_pad : 0;
	int rows = pe_toolbar_rows(c);
	pe_toolbar_metric(c, &cx, &cy);
	unsigned per_row = (c->button_count + (unsigned)rows - 1) / (unsigned)rows;
	if (!per_row)
		per_row = 1;
	*width = (int)per_row * cx + 2 * pad;
	*height = rows * cy + 2 * pad;
	if (*width < 1)
		*width = 1;
	if (*height < 1)
		*height = 1;
}
static void pe_toolbar_button_rect(struct pe_control *c, unsigned index,
				   struct pe_rect *r)
{
	int cx, cy, pad = c->tb_pad > 0 ? c->tb_pad : 0;
	int rows = pe_toolbar_rows(c);
	pe_toolbar_metric(c, &cx, &cy);
	unsigned per_row = (c->button_count + (unsigned)rows - 1) / (unsigned)rows;
	if (!per_row)
		per_row = 1;
	unsigned row = index / per_row, col = index % per_row;
	r->left = pad + (int)col * cx;
	r->top = pad + (int)row * cy;
	r->right = r->left + cx;
	r->bottom = r->top + cy;
}
static __attribute__((ms_abi)) intptr_t pe_control_proc(HANDLE, DWORD,
							uintptr_t, intptr_t);
static __attribute__((ms_abi)) intptr_t pe_toolbar_proc(HANDLE hwnd, DWORD msg,
							uintptr_t wp,
							intptr_t lp)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	struct pe_control *c = w ? w->control : NULL;
	if (msg == 0x421 && c) { /* TB_AUTOSIZE */
		int width, height;
		pe_toolbar_autosize_size(c, &width, &height);
		/* CCS_NOPARENTALIGN applications keep their position. */
		SetWindowPos(hwnd, NULL, w->x, w->y, width, height,
			     4 | 0x10 | 0x02);
		return 0;
	}
	if (!c)
		return pe_control_proc(hwnd, msg, wp, lp);
	if (msg == 0x41d) { /* TB_GETITEMRECT */
		if (!lp || wp >= c->button_count)
			return 0;
		pe_toolbar_button_rect(c, (unsigned)wp, (struct pe_rect *)lp);
		return 1;
	}
	if (msg == 0x41f) { /* TB_SETBUTTONSIZE */
		int v = (int)wp;
		if (!v)
			v = (int)lp;
		if (!((v & 0xffff) && (v >> 16)))
			return 0;
		c->tb_cx = (int16_t)(v & 0xffff);
		c->tb_cy = (int16_t)(v >> 16);
		return 1;
	}
	if (msg == 0x420) { /* TB_SETBITMAPSIZE */
		c->tb_bitmap_cx = (int16_t)(wp & 0xffff);
		c->tb_bitmap_cy = (int16_t)(wp >> 16);
		return 1;
	}
	if (msg == 0x43a) { /* TB_GETBUTTONSIZE */
		int cx, cy;
		pe_toolbar_metric(c, &cx, &cy);
		return (uint16_t)cx | ((uint32_t)(uint16_t)cy << 16);
	}
	if (msg == 0x423) /* TB_GETTOOLTIPS: no tooltip window is created */
		return 0;
	return pe_control_proc(hwnd, msg, wp, lp);
}
