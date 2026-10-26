struct pe_enumlogfont {
	struct pe_logfont font;
	uint16_t full[64], style[32], script[32];
};
struct pe_newtextmetric {
	struct pg_text_metrics metric;
	DWORD flags, em, cell, average, signature[6];
};
_Static_assert(sizeof(struct pe_enumlogfont) == 348, "ENUMLOGFONTEXW ABI");
_Static_assert(sizeof(struct pe_newtextmetric) == 100, "NEWTEXTMETRICEXW ABI");
typedef int(__attribute__((ms_abi)) *
	    pe_font_enum_callback)(const struct pe_enumlogfont *,
				   const struct pe_newtextmetric *, DWORD,
				   intptr_t);
static void pe_enum_font_name(uint16_t *out, int cap, const char *input)
{
	int n = MultiByteToWideChar(65001, 0, input, -1, NULL, 0);
	if (n <= cap) {
		if (n)
			MultiByteToWideChar(65001, 0, input, -1, out, cap);
		return;
	}
	uint16_t temp[512];
	if (n > 512)
		return;
	MultiByteToWideChar(65001, 0, input, -1, temp, 512);
	memcpy(out, temp, (cap - 1) * 2);
	out[cap - 1] = 0;
}
__attribute__((ms_abi)) int EnumFontFamiliesExW(HANDLE dc,
						struct pe_logfont *filter,
						pe_font_enum_callback callback,
						intptr_t parameter, DWORD flags)
{
	if (!filter || !callback || flags || filter->pitch) {
		SetLastError(87);
		return 0;
	}
	{
		PE_STATE_GUARD;
		if (!pe_dc_lookup(dc))
			return 0;
	}
	struct pe_logfont request = *filter;
	request.face[31] = 0;
	struct pg_packet p = { .op = PG_FONT_ENUM_BEGIN,
			       .c = request.height,
			       .flags = request.charset };
	if (!WideCharToMultiByte(65001, 0, request.face, -1, p.text, PG_TEXT,
				 NULL, NULL))
		return 0;
	struct pe_ui *u = pe_ui_current();
	if (!u || !pe_ui_rpc(u, &p))
		return 0;
	int token = p.a, last = 0;
	DWORD saved = GetLastError();
	for (;;) {
		p = (struct pg_packet){ .op = PG_FONT_ENUM_NEXT, .a = token };
		if (!pe_ui_rpc(u, &p)) {
			last = 0;
			saved = GetLastError();
			break;
		}
		if (!p.b)
			break;
		struct pg_font_enum_record r;
		memcpy(&r, p.text, sizeof(r));
		struct pe_enumlogfont font = { 0 };
		struct pe_newtextmetric metric = { 0 };
		font.font.height = r.metrics.height;
		font.font.width = r.metrics.average;
		font.font.weight = r.metrics.weight;
		font.font.italic = r.metrics.italic;
		font.font.charset = r.metrics.charset;
		font.font.pitch = r.metrics.pitch;
		pe_enum_font_name(font.font.face, 32, r.family);
		pe_enum_font_name(font.full, 64, r.full);
		pe_enum_font_name(font.style, 32, r.style);
		pe_enum_font_name(font.script, 32, r.script);
		metric.metric = r.metrics;
		metric.flags = r.ntm_flags;
		metric.em = r.em;
		metric.cell = r.metrics.height;
		metric.average = r.metrics.average;
		memcpy(metric.signature, r.signature, sizeof(metric.signature));
		last = callback(&font, &metric, r.font_type, parameter);
		saved = GetLastError();
		if (!last)
			break;
	}
	p = (struct pg_packet){ .op = PG_FONT_ENUM_END, .a = token };
	pe_ui_rpc(u, &p);
	SetLastError(saved);
	return last;
}
