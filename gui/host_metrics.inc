/* Fontconfig/FreeType measurements are independent of X11 display connection. */
static void native_font_query(struct pg_packet *p)
{
	struct surface temp = { 0 };
	if (p->length >= PG_TEXT - 1) {
		p->status = 87;
		return;
	}
	const char *family = p->op == PG_FONT_METRICS ? p->text :
							p->text + p->length + 1;
	int64_t request = p->c;
	unsigned height = request < 0 ? (request < -256 ? 256 : -request) :
					(request > 256 ? 256 : request);
	if (!height)
		height = 12;
	p->status = font(&temp, family, height);
	if (p->status)
		return;
	FT_Size_Metrics m = temp.face->size->metrics;
	int ascent = (m.ascender + 63) >> 6, descent = (-m.descender + 63) >> 6;
	if (p->op == PG_FONT_METRICS) {
		int average = 0;
		if (!FT_Load_Char(temp.face, 'x', FT_LOAD_DEFAULT))
			average = temp.face->glyph->advance.x >> 6;
		struct pg_text_metrics metrics = {
			.height = ascent + descent,
			.ascent = ascent,
			.descent = descent,
			.internal = ascent + descent > (int)height ?
					    ascent + descent - height :
					    0,
			.external = (m.height >> 6) > ascent + descent ?
					    (m.height >> 6) - ascent - descent :
					    0,
			.average = average,
			.maximum = m.max_advance >> 6,
			.weight = p->a ? p->a : 400,
			.aspect_x = 96,
			.aspect_y = 96,
			.first = 32,
			.last = 0xffff,
			.fallback = 0xfffd,
			.space = 32,
			.italic = !!(p->flags & 1),
			.underline = !!(p->flags & 2),
			.strike = !!(p->flags & 4),
			.pitch = (FT_IS_FIXED_WIDTH(temp.face) ? 0x30 : 0x21) |
				 (FT_IS_SCALABLE(temp.face) ? 6 : 0),
			.charset = 1
		};
		memcpy(p->text, &metrics, sizeof(metrics));
	} else {
		const unsigned char *cursor = (void *)p->text,
				    *end = cursor + p->length;
		int64_t width = 0;
		int extra = p->a;
		while (cursor < end) {
			uint32_t cp = next_utf8(&cursor, end);
			if (FT_Load_Char(temp.face, cp, FT_LOAD_DEFAULT)) {
				p->status = 13;
				break;
			}
			width += (temp.face->glyph->advance.x >> 6) +
				 (int64_t)extra;
			if (width > INT32_MAX || width < INT32_MIN) {
				p->status = 534;
				break;
			}
		}
		p->a = width;
		p->b = ascent + descent;
	}
	FT_Done_Face(temp.face);
}

/* Fetch a bounded chunk of a FreeType glyph coverage bitmap for memory DCs.
 * The host remains display-independent: no X11 surface is needed. */
static void native_font_glyph(struct pg_packet *p)
{
    struct surface temp={0};
    uint32_t cp=(uint32_t)p->a, offset=(uint32_t)p->b;
    int64_t requested=p->c;
    unsigned height=requested<0?(requested < -256 ? 256 : -requested):(requested>256?256:requested);
    if (!height) height=12;
    p->status=font(&temp,p->text,height);
    if (p->status) return;
    if (FT_Load_Char(temp.face,cp,FT_LOAD_RENDER)) { p->status=13; FT_Done_Face(temp.face); return; }
    FT_GlyphSlot g=temp.face->glyph;
    FT_Bitmap *bm=&g->bitmap;
    uint64_t total=(uint64_t)bm->width*bm->rows;
    if (total>65536 || offset>total || (bm->pixel_mode!=FT_PIXEL_MODE_GRAY && bm->pixel_mode!=FT_PIXEL_MODE_MONO && total)) {
        p->status=50;FT_Done_Face(temp.face);return;
    }
    p->a=bm->width;p->b=bm->rows;p->c=g->bitmap_left;p->d=g->bitmap_top;
    p->color=(uint32_t)(g->advance.x>>6);p->background=(uint32_t)((temp.face->size->metrics.ascender+63)>>6);p->flags=total;
    size_t n=total-offset; if (n>PG_TEXT-1) n=PG_TEXT-1;
    for (size_t i=0;i<n;i++) {
        size_t pos=offset+i;unsigned row=pos/bm->width,col=pos%bm->width;
        const unsigned char *line=bm->buffer+(bm->pitch>=0?(long)row*bm->pitch:(long)(bm->rows-1-row)*(-bm->pitch));
        p->text[i]=bm->pixel_mode==FT_PIXEL_MODE_MONO ? ((line[col/8] & (0x80u>>(col%8)))?255:0) : line[col];
    }
    p->length=n;
    FT_Done_Face(temp.face);
}
