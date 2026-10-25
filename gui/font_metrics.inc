/* Real substituted-font measurements, transported through the native helper.
 * No display is opened until a surface is actually requested. No complex shaping. */
static bool pe_font_snapshot(HANDLE dc, struct pe_logfont *font, int *extra)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return false;
	struct pe_gui_object *f = pe_gui_lookup(d->u.dc.font, PE_GDI_FONT);
	if (!f)
		return false;
	*font = f->u.font;
	*extra = d->u.dc.character_extra;
	return true;
}
__attribute__((ms_abi)) BOOL GetTextMetricsW(HANDLE dc,
					     struct pg_text_metrics *out)
{
	if (!out) {
		SetLastError(87);
		return 0;
	}
	struct pe_logfont f;
	int extra;
	if (!pe_font_snapshot(dc, &f, &extra))
		return 0;
	struct pg_packet p = { .op = PG_FONT_METRICS,
			       .a = f.weight,
			       .c = f.height,
			       .flags = !!f.italic | ((!!f.underline) << 1) |
					((!!f.strikeout) << 2) };
	if (!WideCharToMultiByte(65001, 0, f.face, -1, p.text, PG_TEXT, NULL,
				 NULL))
		return 0;
	struct pe_ui *u = pe_ui_current();
	if (!u || !pe_ui_rpc(u, &p))
		return 0;
	memcpy(out, p.text, sizeof(*out));
	return 1;
}
__attribute__((ms_abi)) BOOL GetTextMetricsA(HANDLE dc, void *out)
{
	if (!out) {
		SetLastError(87);
		return 0;
	}
	struct pg_text_metrics w;
	if (!GetTextMetricsW(dc, &w))
		return 0;
	unsigned char *a = out;
	memset(a, 0, 56);
	memcpy(a, &w, 44);
	a[44] = w.first < 256 ? w.first : '?';
	a[45] = w.last < 256 ? w.last : 255;
	a[46] = w.fallback < 256 ? w.fallback : '?';
	a[47] = w.space;
	memcpy(a + 48, &w.italic, 5);
	return 1;
}
struct pe_text_size {
	int32_t x, y;
};
__attribute__((ms_abi)) BOOL GetTextExtentPoint32W(HANDLE dc,
						   const uint16_t *text,
						   int count,
						   struct pe_text_size *out)
{
	if (!out || count < 0 || (!text && count)) {
		SetLastError(87);
		return 0;
	}
	struct pe_logfont f;
	int extra;
	if (!pe_font_snapshot(dc, &f, &extra))
		return 0;
	out->x = out->y = 0;
	if (!count)
		return 1;
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return 0;
	int64_t width = 0;
	for (int pos = 0; pos < count;) {
		int units = count - pos > 160 ? 160 : count - pos;
		if (units < count - pos && text[pos + units - 1] >= 0xd800 &&
		    text[pos + units - 1] <= 0xdbff)
			units--;
		struct pg_packet p = { .op = PG_MEASURE,
				       .a = extra,
				       .c = f.height };
		int bytes = WideCharToMultiByte(65001, 0, text + pos, units,
						p.text, PG_TEXT - 130, NULL,
						NULL);
		if (!bytes)
			return 0;
		p.length = bytes;
		p.text[bytes] = 0;
		if (!WideCharToMultiByte(65001, 0, f.face, -1,
					 p.text + bytes + 1,
					 PG_TEXT - bytes - 1, NULL, NULL))
			return 0;
		if (!pe_ui_rpc(u, &p))
			return 0;
		width += p.a;
		if (width > INT32_MAX || width < INT32_MIN) {
			SetLastError(534);
			return 0;
		}
		out->y = p.b;
		pos += units;
	}
	out->x = width;
	return 1;
}
__attribute__((ms_abi)) BOOL GetTextExtentPoint32A(HANDLE dc, const char *text,
						   int count,
						   struct pe_text_size *out)
{
	if (count < 0 || (!text && count) || !out) {
		SetLastError(87);
		return 0;
	}
	if (!count)
		return GetTextExtentPoint32W(dc, NULL, 0, out);
	int units = MultiByteToWideChar(65001, 0, text, count, NULL, 0);
	if (!units)
		return 0;
	uint16_t *wide = pe_raw_map((size_t)units * 2);
	if (!wide) {
		SetLastError(8);
		return 0;
	}
	MultiByteToWideChar(65001, 0, text, count, wide, units);
	BOOL result = GetTextExtentPoint32W(dc, wide, units, out);
	raw_syscall3(SYS_munmap, (long)wide, (size_t)units * 2, 0);
	return result;
}
__attribute__((ms_abi)) BOOL GetTextExtentPointW(HANDLE dc,
						 const uint16_t *text,
						 int count,
						 struct pe_text_size *out)
{
	return GetTextExtentPoint32W(dc, text, count, out);
}
__attribute__((ms_abi)) BOOL GetTextExtentPointA(HANDLE dc, const char *text,
						 int count,
						 struct pe_text_size *out)
{
	return GetTextExtentPoint32A(dc, text, count, out);
}
