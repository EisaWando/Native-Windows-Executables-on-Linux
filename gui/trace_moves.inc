/* BRIDGE_TRACE_MOVES=1: geometry and layout-message trace (call, caller, hwnd,
 * class, rect) for CreateWindowExW, MoveWindow, SetWindowPos, DeferWindowPos,
 * ShowWindow and WM_SIZE/WM_WINDOWPOSCHANGED/WM_NCCALCSIZE. */
static bool pe_trace_moves(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *e = pe_host_env("BRIDGE_TRACE_MOVES");
		enabled = e && e[0] == '1' && e[1] == 0;
	}
	return enabled != 0;
}
static void pe_trace_int(int v)
{
	if (v < 0) {
		raw_write("-", 1);
		raw_uint((uint32_t)(-(uint64_t)v));
	} else
		raw_uint((uint32_t)v);
}
static void pe_trace_class(struct pe_window *w)
{
	raw_write("\"", 1);
	if (w)
		for (unsigned i = 0; i < 255 && w->cls->name[i]; i++) {
			uint16_t ch = w->cls->name[i];
			char c = ch >= 32 && ch <= 126 ? (char)ch : '?';
			raw_write(&c, 1);
		}
	raw_write("\"", 1);
}
static void pe_trace_geometry(const char *call, void *ret, HANDLE hwnd, int x,
			      int y, int cx, int cy, DWORD flags)
{
	if (!pe_trace_moves())
		return;
	raw_puts("[TRACE] ");
	raw_puts(call);
	raw_puts(" ret=");
	raw_hex((uintptr_t)ret);
	raw_puts(" hwnd=");
	raw_hex((uintptr_t)hwnd);
	raw_puts(" class=");
	pe_trace_class(pe_window_lookup(hwnd, false));
	raw_puts(" rect=");
	pe_trace_int(x);
	raw_puts(",");
	pe_trace_int(y);
	raw_puts(",");
	pe_trace_int(cx);
	raw_puts(",");
	pe_trace_int(cy);
	raw_puts(" flags=");
	raw_hex(flags);
	raw_puts("\n");
}
static void pe_trace_window(const char *call, void *ret, struct pe_window *w)
{
	if (!w)
		return;
	pe_trace_geometry(call, ret, w->handle, w->x, w->y, w->width, w->height,
			  w->style);
}
static bool pe_trace_layout_class(struct pe_window *w)
{
	if (!w)
		return false;
	const uint16_t *n = w->cls->name;
	const char *const names[] = { "ToolbarWindow32", "ReBarWindow32",
				      "msctls_statusbar32", "SysTabControl32" };
	for (unsigned i = 0; i < 4; i++) {
		unsigned j = 0;
		while (n[j] && names[i][j] == (uint16_t)names[i][j])
			j++;
		if (!n[j] && !names[i][j])
			return true;
	}
	return false;
}
static void pe_trace_applied(struct pe_window *w, void *ret)
{
	if (!w || !pe_trace_moves() || !pe_trace_layout_class(w))
		return;
	raw_puts("[TRACE] applied ret=");
	raw_hex((uintptr_t)ret);
	raw_puts(" hwnd=");
	raw_hex((uintptr_t)w->handle);
	raw_puts(" class=");
	pe_trace_class(w);
	raw_puts(" rect=");
	pe_trace_int(w->x);
	raw_puts(",");
	pe_trace_int(w->y);
	raw_puts(",");
	pe_trace_int(w->width);
	raw_puts(",");
	pe_trace_int(w->height);
	raw_puts("\n");
}
/* Return values of the layout messages hosts branch on. */
static void pe_trace_return(DWORD msg, intptr_t result)
{
	if (msg < 0x400 || msg > 0x4ff)
		return;
	if (msg != 0x403 && msg != 0x40c && msg != 0x41b && msg != 0x409 &&
	    msg != 0x41c && msg != 0x410 && msg != 0x40d && msg != 0x40e &&
	    msg != 0x41d && msg != 0x43a && msg != 0x423 && msg != 0x418 &&
	    msg != 0x404 && msg != 0x406 && msg != 0x407 && msg != 0x40a)
		return;
	if (!pe_trace_moves())
		return;
	raw_puts("[TRACE] ret msg=");
	raw_hex(msg);
	raw_puts(" result=");
	raw_hex((uintptr_t)result);
	raw_puts("\n");
}
static void pe_trace_message(const char *where, void *ret, HANDLE hwnd,
			     DWORD msg, uintptr_t wp, intptr_t lp)
{
	struct pe_window *trace_window = pe_window_lookup(hwnd, false);
	if (msg != 5 && msg != 0x47 && msg != 0x83 &&
	    !pe_trace_layout_class(trace_window))
		return;
	if (!pe_trace_moves())
		return;
	raw_puts("[TRACE] ");
	raw_puts(where);
	raw_puts(" ret=");
	raw_hex((uintptr_t)ret);
	raw_puts(" hwnd=");
	raw_hex((uintptr_t)hwnd);
	raw_puts(" class=");
	pe_trace_class(trace_window);
	raw_puts(" msg=");
	raw_hex(msg);
	raw_puts(" wp=");
	raw_hex(wp);
	raw_puts(" lp=");
	raw_hex((uintptr_t)lp);
	raw_puts("\n");
}
