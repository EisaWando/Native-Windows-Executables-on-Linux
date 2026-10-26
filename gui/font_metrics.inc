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

static struct {
	uint16_t face[32];
	int height, weight, extra;
	int line_height;
	int16_t width[256];
	bool valid;
} pe_font_char_cache;

__attribute__((ms_abi)) BOOL GetTextExtentExPointW(HANDLE dc,
						   const uint16_t *text,
						   int count,
						   int max_extent,
						   int *fit_out,
						   int *dx_out,
						   struct pe_text_size *out)
{
	if (!out || count < 0 || (!text && count)) {
		SetLastError(87);
		return 0;
	}
	if (!count) {
		out->x = out->y = 0;
		if (fit_out)
			*fit_out = 0;
		return 1;
	}
	if (!fit_out && !dx_out)
		return GetTextExtentPoint32W(dc, text, count, out);

	struct pe_logfont f;
	int extra;
	if (!pe_font_snapshot(dc, &f, &extra))
		return 0;
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return 0;

	bool match = pe_font_char_cache.valid &&
		     pe_font_char_cache.height == f.height &&
		     pe_font_char_cache.weight == f.weight &&
		     pe_font_char_cache.extra == extra;
	if (match) {
		for (int i = 0; i < 32; i++) {
			if (pe_font_char_cache.face[i] != f.face[i]) {
				match = false;
				break;
			}
			if (!f.face[i])
				break;
		}
	}
	if (!match) {
		pe_font_char_cache.valid = true;
		pe_font_char_cache.height = f.height;
		pe_font_char_cache.weight = f.weight;
		pe_font_char_cache.extra = extra;
		pe_font_char_cache.line_height = 0;
		for (int i = 0; i < 32; i++)
			pe_font_char_cache.face[i] = f.face[i];
		for (int i = 0; i < 256; i++)
			pe_font_char_cache.width[i] = -1;
	}

	int64_t cum = 0;
	int fit = 0;
	for (int i = 0; i < count; i++) {
		uint16_t ch = text[i];
		int w = -1;
		if (ch < 256 && pe_font_char_cache.width[ch] >= 0)
			w = pe_font_char_cache.width[ch];
		else {
			struct pg_packet p = { .op = PG_MEASURE,
					       .a = extra,
					       .c = f.height };
			int bytes = WideCharToMultiByte(65001, 0, &ch, 1,
							p.text,
							PG_TEXT - 130,
							NULL, NULL);
			if (!bytes)
				return 0;
			p.length = bytes;
			p.text[bytes] = 0;
			if (!WideCharToMultiByte(65001, 0, f.face, -1,
						 p.text + bytes + 1,
						 PG_TEXT - bytes - 1,
						 NULL, NULL))
				return 0;
			if (!pe_ui_rpc(u, &p))
				return 0;
			w = p.a;
			pe_font_char_cache.line_height = p.b;
			if (ch < 256)
				pe_font_char_cache.width[ch] = w;
		}
		cum += w;
		if (cum > INT32_MAX || cum < INT32_MIN) {
			SetLastError(534);
			return 0;
		}
		if (dx_out)
			dx_out[i] = (int)cum;
		if (max_extent >= 0 && cum <= (int64_t)max_extent)
			fit = i + 1;
	}
	out->x = (int32_t)cum;
	out->y = pe_font_char_cache.line_height;
	if (fit_out)
		*fit_out = (max_extent >= 0) ? fit : 0;
	return 1;
}

__attribute__((ms_abi)) BOOL GetTextExtentExPointA(HANDLE dc,
						   const char *text,
						   int count,
						   int max_extent,
						   int *fit_out,
						   int *dx_out,
						   struct pe_text_size *out)
{
	if (count < 0 || (!text && count) || !out) {
		SetLastError(87);
		return 0;
	}
	if (!count) {
		out->x = out->y = 0;
		if (fit_out)
			*fit_out = 0;
		return 1;
	}
	int units = MultiByteToWideChar(65001, 0, text, count, NULL, 0);
	if (!units)
		return 0;
	uint16_t *wide = (uint16_t *)pe_raw_map((size_t)units * 2);
	if (!wide) {
		SetLastError(8);
		return 0;
	}
	MultiByteToWideChar(65001, 0, text, count, wide, units);

	int *wide_dx = NULL;
	if (dx_out) {
		wide_dx = (int *)pe_raw_map((size_t)units * sizeof(int));
		if (!wide_dx) {
			raw_syscall3(SYS_munmap, (long)wide,
				     (size_t)units * 2, 0);
			SetLastError(8);
			return 0;
		}
	}
	int wide_fit = 0;
	BOOL res = GetTextExtentExPointW(dc, wide, units, max_extent,
					 fit_out ? &wide_fit : NULL,
					 wide_dx, out);
	if (res) {
		if (units == count) {
			if (dx_out) {
				for (int i = 0; i < count; i++)
					dx_out[i] = wide_dx[i];
			}
			if (fit_out)
				*fit_out = wide_fit;
		} else {
			int w_idx = 0;
			const unsigned char *p = (const unsigned char *)text;
			const unsigned char *end = p + count;
			int byte_idx = 0;
			int bytes_fitted = 0;
			while (p < end && w_idx < units) {
				int char_bytes = 1;
				if (*p >= 0xf0)
					char_bytes = 4;
				else if (*p >= 0xe0)
					char_bytes = 3;
				else if (*p >= 0xc0)
					char_bytes = 2;
				if (p + char_bytes > end)
					char_bytes = (int)(end - p);

				int w_cum = wide_dx ? wide_dx[w_idx] : 0;
				for (int b = 0;
				     b < char_bytes && byte_idx < count;
				     b++, byte_idx++) {
					if (dx_out)
						dx_out[byte_idx] = w_cum;
				}
				if (w_idx < wide_fit)
					bytes_fitted = byte_idx;
				p += char_bytes;
				w_idx++;
			}
			if (fit_out)
				*fit_out = bytes_fitted;
		}
	}
	if (wide_dx)
		raw_syscall3(SYS_munmap, (long)wide_dx,
			     (size_t)units * sizeof(int), 0);
	raw_syscall3(SYS_munmap, (long)wide, (size_t)units * 2, 0);
	return res;
}
