/* Original Fontconfig snapshot enumeration. Independent snapshots allow callbacks
 * to reenter enumeration; OS/2 codepage bits determine Windows charset coverage. */
#include FT_TRUETYPE_TABLES_H
struct native_font_enum {
	struct native_font_enum *next;
	FcFontSet *set;
	FT_Face face;
	unsigned id, index, charset;
	int filter, height;
	char family[128];
};
static struct native_font_enum *native_font_enums;
static unsigned native_font_enum_id;
static const struct {
	unsigned charset, bit;
	const char *script;
} native_charsets[] = {
	{ 0, 0, "Western" },	  { 238, 1, "Central European" },
	{ 204, 2, "Cyrillic" },	  { 161, 3, "Greek" },
	{ 162, 4, "Turkish" },	  { 177, 5, "Hebrew" },
	{ 178, 6, "Arabic" },	  { 186, 7, "Baltic" },
	{ 163, 8, "Vietnamese" }, { 222, 16, "Thai" },
	{ 128, 17, "Japanese" },  { 134, 18, "Chinese" },
	{ 129, 19, "Korean" },	  { 136, 20, "Chinese" },
	{ 130, 21, "Korean" },	  { 77, 29, "Macintosh" },
	{ 2, 31, "Symbol" }
};
static void native_enum_string(char *out, size_t cap, const char *s)
{
	if (!s)
		s = "";
	size_t n = strlen(s);
	if (n >= cap)
		n = cap - 1;
	memcpy(out, s, n);
	out[n] = 0;
}
static const char *native_enum_name(FcPattern *p, const char *field)
{
	FcChar8 *s = NULL;
	return FcPatternGetString(p, field, 0, &s) == FcResultMatch ?
		       (const char *)s :
		       "";
}
static void native_font_enumerate(struct pg_packet *p)
{
	if (p->op == PG_FONT_ENUM_BEGIN) {
		struct native_font_enum *e = calloc(1, sizeof(*e));
		FcPattern *pattern = FcPatternCreate();
		FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_FULLNAME,
							FC_STYLE, FC_FILE,
							FC_INDEX, NULL);
		if (!e || !pattern || !objects) {
			free(e);
			if (pattern)
				FcPatternDestroy(pattern);
			if (objects)
				FcObjectSetDestroy(objects);
			p->status = 8;
			return;
		}
		e->set = FcFontList(NULL, pattern, objects);
		FcPatternDestroy(pattern);
		FcObjectSetDestroy(objects);
		if (!e->set) {
			free(e);
			p->status = 8;
			return;
		}
		if (native_font_enum_id == UINT32_MAX) {
			FcFontSetDestroy(e->set);
			free(e);
			p->status = 8;
			return;
		}
		e->id = ++native_font_enum_id;
		e->filter = p->flags;
		e->height = p->c;
		if (e->height < 0)
			e->height = e->height < -256 ? 256 : -e->height;
		if (e->height <= 0)
			e->height = 16;
		if (e->height > 256)
			e->height = 256;
		native_enum_string(e->family, sizeof(e->family), p->text);
		e->next = native_font_enums;
		native_font_enums = e;
		p->a = e->id;
		return;
	}
	struct native_font_enum **link = &native_font_enums;
	while (*link && (*link)->id != (unsigned)p->a)
		link = &(*link)->next;
	struct native_font_enum *e = *link;
	if (!e) {
		p->status = 6;
		return;
	}
	if (p->op == PG_FONT_ENUM_END) {
		*link = e->next;
		if (e->face)
			FT_Done_Face(e->face);
		FcFontSetDestroy(e->set);
		free(e);
		return;
	}
	p->b = 0;
	while (e->index < (unsigned)e->set->nfont) {
		FcPattern *pattern = e->set->fonts[e->index];
		if (!e->face) {
			const char *family =
				native_enum_name(pattern, FC_FAMILY);
			bool skip =
				*e->family &&
				FcStrCmpIgnoreCase((const FcChar8 *)family,
						   (const FcChar8 *)e->family);
			if (!skip && !*e->family && e->filter == 1) {
				for (unsigned j = 0; j < e->index; j++)
					if (!FcStrCmpIgnoreCase(
						    (const FcChar8 *)family,
						    (const FcChar8 *)
							    native_enum_name(
								    e->set->fonts
									    [j],
								    FC_FAMILY))) {
						skip = true;
						break;
					}
			}
			if (skip) {
				e->index++;
				continue;
			}
			int index = 0;
			FcPatternGetInteger(pattern, FC_INDEX, 0, &index);
			if (FT_New_Face(ft, native_enum_name(pattern, FC_FILE),
					index, &e->face)) {
				e->index++;
				continue;
			}
			if (FT_Set_Pixel_Sizes(e->face, 0, e->height) &&
			    (!e->face->num_fixed_sizes ||
			     FT_Select_Size(e->face, 0))) {
				FT_Done_Face(e->face);
				e->face = NULL;
				e->index++;
				continue;
			}
			e->charset = 0;
		}
		TT_OS2 *os2 = FT_Get_Sfnt_Table(e->face, ft_sfnt_os2);
		uint32_t codepages = os2 && os2->version != 0xffff &&
						     os2->version >= 1 ?
					     os2->ulCodePageRange1 :
					     0;
		if (!codepages && FT_Get_Char_Index(e->face, 'A') &&
		    FT_Get_Char_Index(e->face, 'z'))
			codepages = 1;
		for (; e->charset <
		       sizeof(native_charsets) / sizeof(*native_charsets);) {
			unsigned ch = e->charset++;
			if (!(codepages & (1u << native_charsets[ch].bit)) ||
			    (e->filter != 1 &&
			     e->filter != (int)native_charsets[ch].charset))
				continue;
			struct pg_font_enum_record r = { 0 };
			FT_Size_Metrics m = e->face->size->metrics;
			r.metrics.ascent = (m.ascender + 63) >> 6;
			r.metrics.descent = (-m.descender + 63) >> 6;
			r.metrics.height = r.metrics.ascent + r.metrics.descent;
			r.metrics.internal =
				r.metrics.height > e->height ?
					r.metrics.height - e->height :
					0;
			r.metrics.external =
				(m.height >> 6) > r.metrics.height ?
					(m.height >> 6) - r.metrics.height :
					0;
			if (!FT_Load_Char(e->face, 'x', FT_LOAD_DEFAULT))
				r.metrics.average = e->face->glyph->advance.x >>
						    6;
			r.metrics.maximum = m.max_advance >> 6;
			r.metrics.weight = e->face->style_flags &
							   FT_STYLE_FLAG_BOLD ?
						   700 :
						   400;
			r.metrics.italic =
				!!(e->face->style_flags & FT_STYLE_FLAG_ITALIC);
			r.metrics.aspect_x = r.metrics.aspect_y = 96;
			r.metrics.first = 32;
			r.metrics.last = 0xffff;
			r.metrics.fallback = '?';
			r.metrics.space = 32;
			r.metrics.charset = native_charsets[ch].charset;
			r.metrics.pitch =
				(FT_IS_FIXED_WIDTH(e->face) ? 0x30 : 0x21) |
				(FT_IS_SCALABLE(e->face) ? 6 : 0);
			r.ntm_flags = (r.metrics.italic ? 1 : 0) |
				      (r.metrics.weight >= 700 ? 0x20 : 0);
			if (!r.ntm_flags)
				r.ntm_flags = 0x40;
			r.em = e->face->units_per_EM;
			r.font_type = FT_IS_SCALABLE(e->face) ? 4 : 1;
			if (os2) {
				r.signature[0] = os2->ulUnicodeRange1;
				r.signature[1] = os2->ulUnicodeRange2;
				r.signature[2] = os2->ulUnicodeRange3;
				r.signature[3] = os2->ulUnicodeRange4;
				if (os2->version >= 1 &&
				    os2->version != 0xffff) {
					r.signature[4] = os2->ulCodePageRange1;
					r.signature[5] = os2->ulCodePageRange2;
				}
			}
			native_enum_string(r.family, sizeof(r.family),
					   native_enum_name(pattern,
							    FC_FAMILY));
			native_enum_string(r.full, sizeof(r.full),
					   native_enum_name(pattern,
							    FC_FULLNAME));
			native_enum_string(r.style, sizeof(r.style),
					   native_enum_name(pattern, FC_STYLE));
			native_enum_string(r.script, sizeof(r.script),
					   native_charsets[ch].script);
			memcpy(p->text, &r, sizeof(r));
			p->b = 1;
			return;
		}
		FT_Done_Face(e->face);
		e->face = NULL;
		e->index++;
	}
}
