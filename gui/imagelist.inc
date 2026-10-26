/* Original image lists: generation-checked handles, copied ARGB pixels. */
#define PE_GUI_IMAGELIST 25
struct pe_imagelist {
	int width, height, grow, count, capacity;
	unsigned flags;
	DWORD background;
	uint32_t **images;
};
static struct pe_imagelist *pe_iml(HANDLE h)
{
	struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_IMAGELIST);
	return o ? o->imagelist : NULL;
}
static bool pe_iml_room(struct pe_imagelist *l, int count)
{
	if (count <= l->capacity)
		return true;
	if (count < 0 || count > 65536) {
		SetLastError(8);
		return false;
	}
	int cap = l->capacity ? l->capacity : 4;
	while (cap < count) {
		int next = cap + (l->grow > cap ? l->grow : cap);
		cap = next > 65536 ? 65536 : next;
	}
	uint32_t **items = pe_raw_map((size_t)cap * sizeof(*items));
	if (!items) {
		SetLastError(8);
		return false;
	}
	if (l->count)
		memcpy(items, l->images, (size_t)l->count * sizeof(*items));
	if (l->images)
		raw_syscall3(SYS_munmap, (long)l->images,
			     (size_t)l->capacity * sizeof(*items), 0);
	l->images = items;
	l->capacity = cap;
	return true;
}
static void pe_iml_clear(struct pe_imagelist *l)
{
	for (int i = 0; i < l->count; i++)
		raw_syscall3(SYS_munmap, (long)l->images[i],
			     (size_t)l->width * l->height * 4, 0);
	l->count = 0;
}
__attribute__((ms_abi)) HANDLE ImageList_Create(int width, int height,
						unsigned flags, int initial,
						int grow)
{
	unsigned color = flags & 0xfe;
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
	    initial < 0 || grow < 0 || initial > 65536 || grow > 65536) {
		SetLastError(87);
		return NULL;
	}
	if ((flags & ~0xffu) || !(color == 0 || color == 4 || color == 8 ||
				  color == 16 || color == 24 || color == 32)) {
		SetLastError(50);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_new(PE_GUI_IMAGELIST);
	if (!o)
		return NULL;
	struct pe_imagelist *l = pe_raw_map(sizeof(*l));
	if (!l) {
		pe_gui_drop(o);
		SetLastError(8);
		return NULL;
	}
	*l = (struct pe_imagelist){ .width = width,
				    .height = height,
				    .flags = flags,
				    .grow = grow,
				    .background = UINT32_MAX };
	o->imagelist = l;
	if (initial && !pe_iml_room(l, initial)) {
		raw_syscall3(SYS_munmap, (long)l, sizeof(*l), 0);
		pe_gui_drop(o);
		return NULL;
	}
	return pe_gui_handle(o);
}
__attribute__((ms_abi)) BOOL ImageList_Destroy(HANDLE h)
{
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_IMAGELIST);
	if (!o)
		return 0;
	struct pe_imagelist *l = o->imagelist;
	pe_iml_clear(l);
	if (l->images)
		raw_syscall3(SYS_munmap, (long)l->images,
			     (size_t)l->capacity * sizeof(*l->images), 0);
	raw_syscall3(SYS_munmap, (long)l, sizeof(*l), 0);
	pe_gui_drop(o);
	return 1;
}
__attribute__((ms_abi)) int ImageList_GetImageCount(HANDLE h)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	return l ? l->count : 0;
}
__attribute__((ms_abi)) BOOL ImageList_GetIconSize(HANDLE h, int *width,
						   int *height)
{
	if (!width || !height) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return 0;
	*width = l->width;
	*height = l->height;
	return 1;
}
__attribute__((ms_abi)) BOOL ImageList_SetIconSize(HANDLE h, int width,
						   int height)
{
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return 0;
	pe_iml_clear(l);
	l->width = width;
	l->height = height;
	return 1;
}
__attribute__((ms_abi)) DWORD ImageList_SetBkColor(HANDLE h, DWORD color)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return UINT32_MAX;
	DWORD old = l->background;
	l->background = color;
	return old;
}
__attribute__((ms_abi)) DWORD ImageList_GetBkColor(HANDLE h)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	return l ? l->background : UINT32_MAX;
}
static int pe_iml_add(HANDLE h, HANDLE bitmap, HANDLE mask, bool keyed,
		      DWORD key)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return -1;
	struct pe_gui_object *o = pe_gui_lookup(bitmap, PE_GDI_BITMAP);
	if (!o)
		return -1;
	struct pe_raster *src = pe_raster_resource(o);
	if (!src)
		return -1;
	struct pe_raster *msk = NULL;
	if (mask) {
		struct pe_gui_object *m = pe_gui_lookup(mask, PE_GDI_BITMAP);
		if (!m)
			return -1;
		msk = pe_raster_resource(m);
		if (!msk)
			return -1;
	}
	int count = src->width / l->width;
	if (!count || src->height < l->height ||
	    (msk && (msk->width < src->width || msk->height < l->height)) ||
	    count > 65536 - l->count) {
		SetLastError(87);
		return -1;
	}
	if (!pe_iml_room(l, l->count + count))
		return -1;
	int first = l->count;
	uint32_t match = key == 0xff000000u ?
				 pe_raster_get(src, 0, 0) & 0xffffff :
				 pe_color_rgb(key) & 0xffffff;
	for (int i = 0; i < count; i++) {
		size_t bytes = (size_t)l->width * l->height * 4;
		uint32_t *data = pe_raw_map(bytes);
		if (!data) {
			while (l->count > first)
				raw_syscall3(SYS_munmap,
					     (long)l->images[--l->count], bytes,
					     0);
			SetLastError(8);
			return -1;
		}
		for (int y = 0; y < l->height; y++)
			for (int x = 0; x < l->width; x++) {
				int sx = i * l->width + x;
				uint32_t pixel = pe_raster_get(src, sx, y);
				if ((l->flags & 1) &&
				    ((keyed && (pixel & 0xffffff) == match) ||
				     (msk &&
				      (pe_raster_get(msk, sx, y) & 0xffffff))))
					pixel = 0;
				data[(size_t)y * l->width + x] = pixel;
			}
		l->images[l->count++] = data;
	}
	return first;
}
__attribute__((ms_abi)) int ImageList_Add(HANDLE h, HANDLE bitmap, HANDLE mask)
{
	return pe_iml_add(h, bitmap, mask, false, 0);
}
__attribute__((ms_abi)) int ImageList_AddMasked(HANDLE h, HANDLE bitmap,
						DWORD mask)
{
	return pe_iml_add(h, bitmap, NULL, true, mask);
}
__attribute__((ms_abi)) BOOL ImageList_Remove(HANDLE h, int index)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return 0;
	if (index == -1) {
		pe_iml_clear(l);
		return 1;
	}
	if (index < 0 || index >= l->count) {
		SetLastError(87);
		return 0;
	}
	raw_syscall3(SYS_munmap, (long)l->images[index],
		     (size_t)l->width * l->height * 4, 0);
	memmove(l->images + index, l->images + index + 1,
		(size_t)(l->count - index - 1) * sizeof(*l->images));
	l->count--;
	return 1;
}
__attribute__((ms_abi)) int ImageList_ReplaceIcon(HANDLE h, int index,
						  HANDLE icon)
{
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return -1;
	if (index < -1 || index >= l->count) {
		SetLastError(87);
		return -1;
	}
	struct pe_gui_object *o = pe_gui_lookup(icon, PE_GUI_ICON);
	if (!o)
		return -1;
	struct pe_raster *src = pe_raster_resource(o);
	if (!src)
		return -1;
	if (index == -1 && !pe_iml_room(l, l->count + 1))
		return -1;
	uint32_t *data = pe_raw_map((size_t)l->width * l->height * 4);
	if (!data) {
		SetLastError(8);
		return -1;
	}
	for (int y = 0; y < l->height; y++)
		for (int x = 0; x < l->width; x++)
			data[(size_t)y * l->width + x] = pe_raster_get(
				src, (int64_t)x * src->width / l->width,
				(int64_t)y * src->height / l->height);
	if (index == -1)
		index = l->count++;
	else
		raw_syscall3(SYS_munmap, (long)l->images[index],
			     (size_t)l->width * l->height * 4, 0);
	l->images[index] = data;
	return index;
}
__attribute__((ms_abi)) HANDLE ImageList_GetIcon(HANDLE h, int index,
						 unsigned flags)
{
	if (flags & ~1u) {
		SetLastError(50);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_imagelist *l = pe_iml(h);
	if (!l)
		return NULL;
	if (index < 0 || index >= l->count) {
		SetLastError(87);
		return NULL;
	}
	struct pe_raster *r = pe_raster_new(l->width, l->height, true);
	if (!r)
		return NULL;
	struct pe_gui_object *o = pe_gui_new(PE_GUI_ICON);
	if (!o) {
		pe_raster_free(&(struct pe_gui_object){ .raster = r });
		return NULL;
	}
	memcpy(r->pixels, l->images[index], (size_t)l->width * l->height * 4);
	o->raster = r;
	o->image.width = l->width;
	o->image.height = l->height;
	return pe_gui_handle(o);
}
static uint32_t pe_iml_mix(uint32_t a, uint32_t b, unsigned alpha)
{
	uint32_t out = 0xff000000u;
	for (unsigned shift = 0; shift < 24; shift += 8)
		out |= ((((a >> shift) & 255) * alpha +
			 ((b >> shift) & 255) * (255 - alpha) + 127) /
			255)
		       << shift;
	return out;
}
__attribute__((ms_abi)) BOOL ImageList_DrawEx(HANDLE h, int index, HANDLE dc,
					      int x, int y, int dx, int dy,
					      DWORD background,
					      DWORD foreground, unsigned style)
{
	if (style & ~0x2037u) {
		SetLastError(50);
		return 0;
	}
	uint32_t *source;
	int width, height;
	bool has_mask;
	DWORD bk;
	{
		PE_STATE_GUARD;
		struct pe_imagelist *l = pe_iml(h);
		if (!l)
			return 0;
		if (index < 0 || index >= l->count || dx < 0 || dy < 0) {
			SetLastError(87);
			return 0;
		}
		width = l->width;
		height = l->height;
		has_mask = !!(l->flags & 1);
		bk = background == 0xff000000u ? l->background : background;
		source = pe_raw_map((size_t)width * height * 4);
		if (!source) {
			SetLastError(8);
			return 0;
		}
		memcpy(source, l->images[index], (size_t)width * height * 4);
	}
	if (!dx)
		dx = width;
	if (!dy)
		dy = height;
	if (dx > 4096 || dy > 4096 || x > INT32_MAX - dx ||
	    y > INT32_MAX - dy) {
		raw_syscall3(SYS_munmap, (long)source,
			     (size_t)width * height * 4, 0);
		SetLastError(87);
		return 0;
	}
	if (foreground == 0xff000000u)
		foreground = GetSysColor(13);
	bool ok = true;
	struct pe_raster *target = NULL;
	bool guard = pe_state_enter();
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		ok = false;
	else if (d->type == PE_GDI_MEMDC) {
		struct pe_gui_object *b =
			pe_gui_lookup(d->u.dc.bitmap, PE_GDI_BITMAP);
		if (b)
			target = pe_raster_resource(b);
		if (!target)
			ok = false;
	} else
		pe_state_leave(&guard);
	for (int row = 0; ok && row < dy; row++)
		for (int col = 0; ok && col < dx; col++) {
			if (!(style & 0x2000) &&
			    (col >= width || row >= height))
				continue;
			int sx = style & 0x2000 ? (int64_t)col * width / dx :
						  col,
			    sy = style & 0x2000 ? (int64_t)row * height / dy :
						  row;
			uint32_t pixel = source[(size_t)sy * width + sx];
			unsigned alpha = pixel >> 24;
			if (style & 0x10) {
				pixel = alpha ? 0xff000000u : 0xffffffffu;
				alpha = 255;
			} else if (style & 0x20)
				alpha = 255;
			int tx = x + col, ty = y + row;
			if (target &&
			    (tx < 0 || ty < 0 || tx >= target->width ||
			     ty >= target->height))
				continue;
			if ((style & 6) && has_mask && alpha) {
				uint32_t blend;
				if (foreground == UINT32_MAX) {
					if (target)
						blend = pe_raster_get(target,
								      tx, ty);
					else {
						DWORD color =
							GetPixel(dc, tx, ty);
						if (color == UINT32_MAX) {
							ok = false;
							break;
						}
						blend = pe_color_rgb(color);
					}
				} else
					blend = pe_color_rgb(foreground);
				pixel = pe_iml_mix(blend, pixel,
						   style & 4 ? 128 : 64);
			}
			bool opaque_background = !(style & 1 && has_mask) &&
						 bk != UINT32_MAX;
			uint32_t under = 0;
			if (opaque_background)
				under = pe_color_rgb(bk) | 0xff000000u;
			else if (!alpha)
				continue;
			else if (alpha < 255)
				under = target ? pe_raster_get(target, tx, ty) :
						 pe_color_rgb(
							 GetPixel(dc, tx, ty));
			if (alpha < 255)
				pixel = pe_iml_mix(pixel, under, alpha);
			if (target)
				pe_raster_set(target, tx, ty, pixel);
			else {
				struct pg_packet p = { .op = PG_PIXEL,
						       .a = tx,
						       .b = ty,
						       .color = pixel &
								0xffffff };
				ok = pe_draw(dc, &p);
			}
		}
	pe_state_leave(&guard);
	raw_syscall3(SYS_munmap, (long)source, (size_t)width * height * 4, 0);
	return ok;
}
__attribute__((ms_abi)) BOOL ImageList_Draw(HANDLE h, int index, HANDLE dc,
					    int x, int y, unsigned style)
{
	return ImageList_DrawEx(h, index, dc, x, y, 0, 0, 0xff000000u,
				0xff000000u, style);
}
__attribute__((ms_abi)) HANDLE ImageList_LoadImageW(HANDLE instance,
						    const uint16_t *name,
						    int width, int grow,
						    DWORD mask, unsigned type,
						    unsigned flags)
{
	if (type > 2 || (flags & ~0xa040u)) {
		SetLastError(50);
		return NULL;
	}
	HANDLE image = LoadImageW(instance, name, type, 0, 0, flags);
	if (!image)
		return NULL;
	HANDLE result = NULL;
	int old_error;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *o =
			pe_gui_lookup(image, type == 0 ? PE_GDI_BITMAP :
					     type == 1 ? PE_GUI_ICON :
							 PE_GUI_CURSOR);
		struct pe_raster *r = o ? pe_raster_resource(o) : NULL;
		if (r) {
			if (!width)
				width = GetSystemMetrics(11);
			result = ImageList_Create(width, r->height, 0x21, 0,
						  grow);
			if (result) {
				int added =
					type ? ImageList_ReplaceIcon(result, -1,
								     image) :
					mask == UINT32_MAX ?
					       ImageList_Add(result, image,
							     NULL) :
					       ImageList_AddMasked(result,
								   image, mask);
				if (added < 0) {
					ImageList_Destroy(result);
					result = NULL;
				}
			}
		}
		old_error = GetLastError();
	}
	if (!(flags & 0x8000)) {
		if (type == 0)
			DeleteObject(image);
		else if (type == 1)
			DestroyIcon(image);
		else
			DestroyCursor(image);
	}
	SetLastError(old_error);
	return result;
}
__attribute__((ms_abi)) HANDLE ImageList_LoadImageA(HANDLE instance,
						    const char *name, int width,
						    int grow, DWORD mask,
						    unsigned type,
						    unsigned flags)
{
	uint16_t wide[512];
	const uint16_t *resource = (const uint16_t *)name;
	if ((uintptr_t)name >= 65536) {
		if (!MultiByteToWideChar(65001, 0, name, -1, wide, 512))
			return NULL;
		resource = wide;
	}
	return ImageList_LoadImageW(instance, resource, width, grow, mask, type,
				    flags);
}
