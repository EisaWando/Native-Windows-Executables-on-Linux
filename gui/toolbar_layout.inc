/* ToolbarWindow32 metrics, button records and TB_* message handling.
 * Button size defaults to 24x24; TB_SETBUTTONSIZE (WM_USER+31) accepts the
 * size in wParam and, like applications that send MAKELONG there, in lParam
 * when wParam is zero. c->button_size is the TBBUTTON record size set by
 * TB_BUTTONSTRUCTSIZE (WM_USER+30, 32 bytes for Windows x64: iBitmap 0,
 * idCommand 4, fsState 8, fsStyle 9, bReserved 10, dwData 16, iString 24).
 * Button records live in c->buttons, which is the state of record for
 * states, commands and client rectangles. */
static void pe_toolbar_metric(struct pe_control *c, int *cx, int *cy)
{
	*cx = c->tb_cx > 0 ? c->tb_cx : 24;
	*cy = c->tb_cy > 0 ? c->tb_cy : 24;
}
static int pe_toolbar_rows(struct pe_control *c)
{
	return c->tb_rows > 0 ? c->tb_rows : 1;
}
static unsigned pe_toolbar_record_size(struct pe_control *c)
{
	return c->button_size >= 16 ? c->button_size : 32;
}
static unsigned char *pe_toolbar_button(struct pe_control *c, unsigned index)
{
	if (!c->buttons || index >= c->button_count)
		return NULL;
	return c->buttons + (size_t)index * pe_toolbar_record_size(c);
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
/* Insert one TBBUTTON record at index (clamped to the end). */
static BOOL pe_toolbar_insert(struct pe_control *c, unsigned index, const void *rec)
{
	unsigned size = pe_toolbar_record_size(c);
	if (index > c->button_count)
		index = c->button_count;
	if (c->button_count + 1 > 65536)
		return 0;
	unsigned char *buttons = pe_raw_map((c->button_count + 1) * (size_t)size);
	if (!buttons) {
		SetLastError(8);
		return 0;
	}
	if (c->button_count) {
		memcpy(buttons, c->buttons, (size_t)index * size);
		if (c->button_count > index)
			memcpy(buttons + (index + 1) * (size_t)size,
			       c->buttons + (size_t)index * size,
			       (c->button_count - index) * (size_t)size);
		raw_syscall3(SYS_munmap, (long)c->buttons,
			     c->button_count * (size_t)size, 0);
	}
	memset(buttons + (size_t)index * size, 0, size);
	if (rec)
		memcpy(buttons + (size_t)index * size, rec, size);
	c->buttons = buttons;
	c->button_count++;
	return 1;
}
static unsigned char pe_toolbar_state(const void *rec)
{
	return *(const unsigned char *)((const char *)rec + 8);
}
static void pe_toolbar_set_state(void *rec, unsigned char mask, BOOL set)
{
	unsigned char *state = (unsigned char *)((char *)rec + 8);
	if (set)
		*state |= mask;
	else
		*state &= (unsigned char)~mask;
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
		if (!lp || !pe_toolbar_button(c, (unsigned)wp))
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
	if (msg == 0x414 || msg == 0x444) /* TB_ADDBUTTONS A/W */
		return pe_control_proc(hwnd, msg, wp, lp);
	if (msg == 0x415 || msg == 0x445) { /* TB_INSERTBUTTON A/W */
		if (!lp)
			return 0;
		return pe_toolbar_insert(c, (unsigned)wp, (const void *)lp);
	}
	if (msg == 0x416) { /* TB_DELETEBUTTON */
		unsigned size = pe_toolbar_record_size(c);
		if (!pe_toolbar_button(c, (unsigned)wp))
			return 0;
		unsigned char *buttons = pe_raw_map(c->buttons ?
				(c->button_count - 1) * (size_t)size : 0);
		if (c->button_count > 1 && !buttons) {
			SetLastError(8);
			return 0;
		}
		if (buttons) {
			memcpy(buttons, c->buttons, (size_t)wp * size);
			memcpy(buttons + (size_t)wp * size,
			       c->buttons + ((size_t)wp + 1) * size,
			       (c->button_count - wp - 1) * (size_t)size);
			raw_syscall3(SYS_munmap, (long)c->buttons,
				     c->button_count * (size_t)size, 0);
		}
		c->buttons = buttons;
		c->button_count--;
		return 1;
	}
	if (msg == 0x411) { /* TB_SETSTATE */
		unsigned char *rec = pe_toolbar_button(c, (unsigned)wp);
		if (!rec)
			return 0;
		*(unsigned char *)(rec + 8) = (unsigned char)lp;
		return 1;
	}
	if (msg == 0x412) { /* TB_GETSTATE */
		unsigned char *rec = pe_toolbar_button(c, (unsigned)wp);
		return rec ? (intptr_t)pe_toolbar_state(rec) : -1;
	}
	if (msg == 0x401 || msg == 0x402 || msg == 0x404) { /* TB_ENABLE/CHECK/HIDEBUTTON */
		unsigned char *rec = pe_toolbar_button(c, (unsigned)wp);
		if (!rec)
			return 0;
		unsigned char mask = msg == 0x401 ? 0x04 : msg == 0x402 ? 0x01 : 0x08;
		pe_toolbar_set_state(rec, mask, lp != 0);
		return 1;
	}
	if (msg == 0x419) { /* TB_COMMANDTOINDEX */
		for (unsigned i = 0; i < c->button_count; i++) {
			unsigned char *rec = pe_toolbar_button(c, i);
			if (*(int *)(rec + 4) == (int)(int32_t)wp)
				return (intptr_t)i;
		}
		return -1;
	}
	if (msg == 0x430 || msg == 0x431) { /* TB_SETIMAGELIST / TB_GETIMAGELIST */
		HANDLE old = c->image;
		if (msg == 0x430)
			c->image = (HANDLE)lp;
		return (intptr_t)old;
	}
	if (msg == 0x454 || msg == 0x455) { /* TB_SET/GETEXTENDEDSTYLE */
		DWORD old = c->tab_extended_style;
		if (msg == 0x454)
			c->tab_extended_style = wp ?
				(old & ~(DWORD)wp) | ((DWORD)lp & (DWORD)wp) :
				(DWORD)lp;
		return old;
	}
	if (msg == 0x457) { /* TB_SETPADDING */
		int v = (int)wp;
		if (!v)
			v = (int)lp;
		c->tb_pad = (int16_t)(v & 0xffff);
		return 1;
	}
	if (msg == 0x427) { /* TB_SETROWS */
		int rows = (int16_t)(wp & 0xffff);
		if (rows > 0)
			c->tb_rows = rows;
		return 1;
	}
	if (msg == 0x428) /* TB_GETROWS */
		return pe_toolbar_rows(c);
	if (msg == 0x453) { /* TB_GETMAXSIZE */
		int width, height;
		if (!lp)
			return 0;
		pe_toolbar_autosize_size(c, &width, &height);
		((int32_t *)lp)[0] = width;
		((int32_t *)lp)[1] = height;
		return 1;
	}
	if (msg == 0x201 || msg == 0x202) { /* WM_LBUTTONDOWN / WM_LBUTTONUP */
		int x = (int16_t)(lp & 0xffff), y = (int16_t)(lp >> 16);
		unsigned hit = c->button_count;
		for (unsigned i = 0; i < c->button_count; i++) {
			struct pe_rect r;
			pe_toolbar_button_rect(c, i, &r);
			if (x >= r.left && x < r.right && y >= r.top &&
			    y < r.bottom) {
				hit = i;
				break;
			}
		}
		if (msg == 0x201) {
			c->selection = (int)hit < (int)c->button_count ? (int)hit : -1;
			return 0;
		}
		if ((int)hit != c->selection || !w->parent)
			return 0;
		unsigned char *rec = pe_toolbar_button(c, hit);
		unsigned char state = rec ? pe_toolbar_state(rec) : 0;
		if (!rec || (state & 0x08) || !(state & 0x04))
			return 0;
		/* ToolbarWindow32 reports button activation as WM_COMMAND with
		 * the button command id; modern common controls do not send the
		 * obsolete TBN_* notifications for this. */
		int id = *(int *)(rec + 4);
		SendMessageW(w->parent, 0x111, (uintptr_t)(uint16_t)id,
			     (intptr_t)hwnd);
		return 0;
	}
	return pe_control_proc(hwnd, msg, wp, lp);
}
