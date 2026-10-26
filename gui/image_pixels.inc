/* Owned 32-bit pixels. Resource DIBs are copied/decoded, never modified in PE. */
struct pe_raster {
	int width, height;
	bool bottom_up, alpha;
	uint32_t *pixels;
};
static void pe_raster_free(struct pe_gui_object *o)
{
	if (!o->raster)
		return;
	struct pe_raster *r = o->raster;
	raw_syscall3(SYS_munmap, (long)r->pixels,
		     (size_t)r->width * r->height * 4, 0);
	raw_syscall3(SYS_munmap, (long)r, sizeof(*r), 0);
	o->raster = NULL;
}
static struct pe_raster *pe_raster_new(int width, int height, bool alpha)
{
	if (width < 1 || height < 1 || width > 4096 || height > 4096) {
		SetLastError(87);
		return NULL;
	}
	struct pe_raster *r = pe_raw_map(sizeof(*r));
	if (!r) {
		SetLastError(8);
		return NULL;
	}
	r->pixels = pe_raw_map((size_t)width * height * 4);
	if (!r->pixels) {
		raw_syscall3(SYS_munmap, (long)r, sizeof(*r), 0);
		SetLastError(8);
		return NULL;
	}
	r->width = width;
	r->height = height;
	r->alpha = alpha;
	return r;
}
static uint32_t pe_raster_get(const struct pe_raster *r, int x, int y)
{
	uint32_t v = r->pixels[(size_t)(r->bottom_up ? r->height - 1 - y : y) *
				       r->width +
			       x];
	return r->alpha ? v : v | 0xff000000u;
}
static void pe_raster_set(struct pe_raster *r, int x, int y, uint32_t pixel)
{
	r->pixels[(size_t)(r->bottom_up ? r->height - 1 - y : y) * r->width + x] =
		pixel;
}
static uint32_t pe_dib_component(uint32_t pixel, uint32_t mask)
{
	if (!mask)
		return 0;
	unsigned shift = __builtin_ctz(mask);
	uint32_t max = mask >> shift;
	return (uint64_t)((pixel & mask) >> shift) * 255 / max;
}
static struct pe_raster *pe_raster_resource(struct pe_gui_object *o)
{
	if (o->raster)
		return o->raster;
	if (!o->image.data || o->image.bytes < 40) {
		SetLastError(50);
		return NULL;
	}
	size_t bytes = o->image.bytes;
	unsigned char *data = pe_raw_map(bytes);
	if (!data) {
		SetLastError(8);
		return NULL;
	}
	if (!diagnostic_image_read(o->image.data, data, bytes)) {
		SetLastError(1812);
		goto bad;
	}
	if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
	    data[3] == 'G') {
		SetLastError(50);
		goto bad;
	}
	uint32_t header, compression, used;
	int32_t width, height;
	uint16_t planes, bpp;
	memcpy(&header, data, 4);
	memcpy(&width, data + 4, 4);
	memcpy(&height, data + 8, 4);
	memcpy(&planes, data + 12, 2);
	memcpy(&bpp, data + 14, 2);
	memcpy(&compression, data + 16, 4);
	memcpy(&used, data + 32, 4);
	bool icon = o->type == PE_GUI_ICON || o->type == PE_GUI_CURSOR;
	if (header < 40 || header > bytes || width <= 0 || width > 4096 ||
	    height == INT32_MIN || !height || planes != 1) {
		SetLastError(1812);
		goto bad;
	}
	bool top = height < 0;
	if (top)
		height = -height;
	if (icon) {
		if (height & 1) {
			SetLastError(1812);
			goto bad;
		}
		height /= 2;
	}
	if (height > 4096 ||
	    !(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 ||
	      bpp == 32) ||
	    (compression != 0 && compression != 3 && compression != 6)) {
		SetLastError(50);
		goto bad;
	}
	size_t offset = header;
	uint32_t masks[4] = { 0xff0000, 0xff00, 0xff, 0 };
	if (bpp == 16) {
		masks[0] = 0x7c00;
		masks[1] = 0x3e0;
		masks[2] = 31;
	}
	if (compression) {
		if (bpp != 16 && bpp != 32) {
			SetLastError(50);
			goto bad;
		}
		size_t where = header == 40 ? 40 : 40;
		unsigned n = compression == 6 ? 4 : 3;
		if (bytes < where + n * 4 ||
		    (header != 40 && header < where + n * 4)) {
			SetLastError(1812);
			goto bad;
		}
		memcpy(masks, data + where, n * 4);
		if (header == 40)
			offset += n * 4;
		if (header >= 56)
			memcpy(&masks[3], data + 52, 4);
		if (!masks[0] || !masks[1] || !masks[2]) {
			SetLastError(1812);
			goto bad;
		}
	}
	unsigned colors = bpp <= 8 ? (used ? used : 1u << bpp) : 0;
	if (colors > (1u << (bpp <= 8 ? bpp : 0)) || offset > bytes ||
	    colors > (bytes - offset) / 4) {
		SetLastError(1812);
		goto bad;
	}
	size_t palette = offset;
	offset += (size_t)colors * 4;
	size_t stride = (((size_t)width * bpp + 31) / 32) * 4;
	if (offset > bytes || (size_t)height > (bytes - offset) / stride) {
		SetLastError(1812);
		goto bad;
	}
	struct pe_raster *r = pe_raster_new(width, height, icon);
	if (!r)
		goto bad;
	bool any_alpha = false;
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {
			const unsigned char *row =
				data + offset +
				(size_t)(top ? y : height - 1 - y) * stride;
			uint32_t pixel = 0, color;
			if (bpp <= 8) {
				unsigned index =
					bpp == 8 ? row[x] :
					bpp == 4 ? (row[x / 2] >>
						    (x & 1 ? 0 : 4)) &
							   15 :
						   (row[x / 8] >> (7 - x % 8)) &
							   1;
				if (index >= colors) {
					pe_raster_free(&(struct pe_gui_object){
						.raster = r });
					SetLastError(1812);
					goto bad;
				}
				memcpy(&color, data + palette + index * 4, 4);
				color = (color & 0xffffff) | 0xff000000u;
			} else if (bpp == 24) {
				const unsigned char *p = row + x * 3;
				color = 0xff000000u | ((uint32_t)p[2] << 16) |
					((uint32_t)p[1] << 8) | p[0];
			} else {
				memcpy(&pixel, row + (size_t)x * (bpp / 8),
				       bpp / 8);
				color = (pe_dib_component(pixel, masks[0])
					 << 16) |
					(pe_dib_component(pixel, masks[1])
					 << 8) |
					pe_dib_component(pixel, masks[2]);
				uint32_t alpha =
					masks[3] ? pe_dib_component(pixel,
								    masks[3]) :
					icon && bpp == 32 ? pixel >> 24 :
							    255;
				if (icon && bpp == 32 && alpha)
					any_alpha = true;
				color |= alpha << 24;
			}
			r->pixels[(size_t)y * width + x] = color;
		}
	size_t mask_offset = offset + stride * height,
	       mask_stride = ((width + 31u) / 32) * 4;
	bool have_mask = mask_offset <= bytes &&
			 (size_t)height <= (bytes - mask_offset) / mask_stride;
	if (icon)
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++) {
				uint32_t *pixel =
					&r->pixels[(size_t)y * width + x];
				if (bpp == 32 && !any_alpha)
					*pixel |= 0xff000000u;
				if (have_mask &&
				    (data[mask_offset +
					  (size_t)(height - 1 - y) * mask_stride +
					  x / 8] &
				     (128 >> (x % 8))))
					*pixel = 0;
			}
	o->raster = r;
	if (o->type == PE_GDI_BITMAP)
		o->u.bitmap = (struct pe_bitmap){ 0, width, height,   width * 4,
						  1, 32,    r->pixels };
	raw_syscall3(SYS_munmap, (long)data, bytes, 0);
	return r;
bad:
	raw_syscall3(SYS_munmap, (long)data, bytes, 0);
	return NULL;
}
__attribute__((ms_abi)) HANDLE CreateBitmap(int width, int height,
					    unsigned planes, unsigned bpp,
					    const void *bits)
{
	if (planes != 1 || (bpp != 1 && bpp != 24 && bpp != 32)) {
		SetLastError(50);
		return NULL;
	}
	struct pe_raster *r = pe_raster_new(width, height, false);
	if (!r)
		return NULL;
	size_t stride = (((size_t)width * bpp + 15) / 16) * 2;
	if (bits)
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++) {
				const unsigned char *p =
					(const unsigned char *)bits +
					y * stride;
				uint32_t color;
				if (bpp == 1)
					color = (p[x / 8] & (128 >> (x % 8))) ?
							0xffffffffu :
							0xff000000u;
				else {
					p += (size_t)x * (bpp / 8);
					color = 0xff000000u |
						((uint32_t)p[2] << 16) |
						((uint32_t)p[1] << 8) | p[0];
				}
				r->pixels[(size_t)y * width + x] = color;
			}
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_new(PE_GDI_BITMAP);
	if (!o) {
		pe_raster_free(&(struct pe_gui_object){ .raster = r });
		return NULL;
	}
	o->raster = r;
	o->u.bitmap =
		(struct pe_bitmap){ 0, width, height, stride, 1, bpp, NULL };
	return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreateDIBSection(HANDLE dc, const void *info,
						unsigned usage, void **bits,
						HANDLE section, DWORD offset)
{
	if (bits)
		*bits = NULL;
	if (!info || !bits || usage || section || offset) {
		SetLastError(87);
		return NULL;
	}
	uint32_t header, compression;
	int32_t w, h;
	uint16_t planes, bpp;
	memcpy(&header, info, 4);
	memcpy(&w, (const char *)info + 4, 4);
	memcpy(&h, (const char *)info + 8, 4);
	memcpy(&planes, (const char *)info + 12, 2);
	memcpy(&bpp, (const char *)info + 14, 2);
	memcpy(&compression, (const char *)info + 16, 4);
	if (header < 40 || planes != 1 || bpp != 32 || compression ||
	    h == INT32_MIN) {
		SetLastError(50);
		return NULL;
	}
	PE_STATE_GUARD;
	if (dc && !pe_dc_lookup(dc))
		return NULL;
	struct pe_raster *r = pe_raster_new(w, h < 0 ? -h : h, false);
	if (!r)
		return NULL;
	struct pe_gui_object *o = pe_gui_new(PE_GDI_BITMAP);
	if (!o) {
		pe_raster_free(&(struct pe_gui_object){ .raster = r });
		return NULL;
	}
	r->bottom_up = h > 0;
	o->raster = r;
	o->u.bitmap =
		(struct pe_bitmap){ 0, w, r->height, w * 4, 1, 32, r->pixels };
	*bits = r->pixels;
	return pe_gui_handle(o);
}
