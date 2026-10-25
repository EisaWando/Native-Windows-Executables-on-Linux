/* Bounds-checked PE32+ type/name/language resource traversal. Main image only. */
struct pe_resource_root {
	uintptr_t base;
	uint32_t size;
};
struct pe_resource_entry {
	uint32_t name, offset;
};
struct pe_resource_data {
	uint32_t rva, size, codepage, reserved;
};
static bool pe_resource_root(HANDLE module, struct pe_resource_root *r)
{
	if (module && (uintptr_t)module != pe_main_base) {
		SetLastError(126);
		return false;
	}
	uint32_t nt, count, dd[2];
	uint16_t optional;
	if (!diagnostic_image_read(pe_main_base + 0x3c, &nt, 4) ||
	    !diagnostic_image_read(pe_main_base + nt + 20, &optional, 2) ||
	    optional < 0x88 ||
	    !diagnostic_image_read(pe_main_base + nt + 24 + 0x6c, &count, 4) ||
	    count < 3 ||
	    !diagnostic_image_read(pe_main_base + nt + 24 + 0x80, dd, 8) ||
	    !dd[0] || dd[1] < 16 || dd[0] >= pe_main_end - pe_main_base ||
	    dd[1] > pe_main_end - pe_main_base - dd[0]) {
		SetLastError(1812);
		return false;
	}
	*r = (struct pe_resource_root){ pe_main_base + dd[0], dd[1] };
	return true;
}
static bool pe_resource_read(const struct pe_resource_root *r, uint32_t off,
			     void *out, size_t n)
{
	return off <= r->size && n <= r->size - off &&
	       diagnostic_image_read(r->base + off, out, n);
}
static unsigned pe_resource_count(const struct pe_resource_root *r,
				  uint32_t off)
{
	uint16_t count[2];
	if (!pe_resource_read(r, off + 12, count, 4))
		return 0;
	unsigned n = (unsigned)count[0] + count[1];
	if (off > r->size || 16ULL + 8ULL * n > r->size - off)
		return 0;
	return n;
}
static bool pe_resource_name(const struct pe_resource_root *r, DWORD encoded,
			     const uint16_t *wanted)
{
	if ((uintptr_t)wanted < 65536)
		return !(encoded & 0x80000000u) && encoded == (uintptr_t)wanted;
	if (wanted[0] == '#') {
		unsigned n = 0, i = 1;
		while (wanted[i] >= '0' && wanted[i] <= '9' && n < 65536)
			n = n * 10 + wanted[i++] - '0';
		if (i > 1 && !wanted[i] && n < 65536)
			return !(encoded & 0x80000000u) && encoded == n;
	}
	if (!(encoded & 0x80000000u))
		return false;
	uint32_t off = encoded & 0x7fffffffu;
	uint16_t len;
	if (!pe_resource_read(r, off, &len, 2) || lstrlenW(wanted) != len)
		return false;
	for (unsigned i = 0; i < len; i++) {
		uint16_t c;
		if (!pe_resource_read(r, off + 2 + i * 2, &c, 2) ||
		    pe_reg_fold(c) != pe_reg_fold(wanted[i]))
			return false;
	}
	return true;
}
static bool pe_resource_find(const struct pe_resource_root *r, uint32_t dir,
			     const uint16_t *wanted, uint32_t *offset)
{
	unsigned n = pe_resource_count(r, dir);
	for (unsigned i = 0; i < n; i++) {
		struct pe_resource_entry e;
		if (!pe_resource_read(r, dir + 16 + i * 8, &e, 8))
			return false;
		if (pe_resource_name(r, e.name, wanted)) {
			*offset = e.offset;
			return true;
		}
	}
	return false;
}
__attribute__((ms_abi)) HANDLE FindResourceExW(HANDLE module,
					       const uint16_t *type,
					       const uint16_t *name,
					       uint16_t lang)
{
	struct pe_resource_root r;
	if (!pe_resource_root(module, &r))
		return NULL;
	uint32_t off;
	if (!pe_resource_find(&r, 0, type, &off) || !(off & 0x80000000u)) {
		SetLastError(1813);
		return NULL;
	}
	if (!pe_resource_find(&r, off & 0x7fffffffu, name, &off) ||
	    !(off & 0x80000000u)) {
		SetLastError(1814);
		return NULL;
	}
	uint32_t dir = off & 0x7fffffffu;
	bool found = pe_resource_find(&r, dir, (void *)(uintptr_t)lang, &off);
	if (!found && lang)
		found = pe_resource_find(
			&r, dir, (void *)(uintptr_t)(lang & 0x3ff), &off);
	if (!found)
		found = pe_resource_find(&r, dir, NULL, &off);
	if (!found && !lang)
		found = pe_resource_find(&r, dir, (void *)0x409, &off);
	if (!found && !lang && pe_resource_count(&r, dir)) {
		struct pe_resource_entry e;
		if (pe_resource_read(&r, dir + 16, &e, 8)) {
			off = e.offset;
			found = true;
		}
	}
	struct pe_resource_data data;
	if (!found || (off & 0x80000000u) ||
	    !pe_resource_read(&r, off, &data, 16) ||
	    data.rva >= pe_main_end - pe_main_base ||
	    data.size > pe_main_end - pe_main_base - data.rva) {
		SetLastError(1815);
		return NULL;
	}
	return (HANDLE)(r.base + off);
}
__attribute__((ms_abi)) HANDLE FindResourceW(HANDLE module,
					     const uint16_t *name,
					     const uint16_t *type)
{
	return FindResourceExW(module, type, name, 0);
}
__attribute__((ms_abi)) HANDLE FindResourceA(HANDLE module, const char *name,
					     const char *type)
{
	uint16_t wn[512], wt[512];
	const uint16_t *n = (void *)name, *t = (void *)type;
	if ((uintptr_t)name >= 65536) {
		if (!MultiByteToWideChar(65001, 8, name, -1, wn, 512))
			return NULL;
		n = wn;
	}
	if ((uintptr_t)type >= 65536) {
		if (!MultiByteToWideChar(65001, 8, type, -1, wt, 512))
			return NULL;
		t = wt;
	}
	return FindResourceW(module, n, t);
}
static bool pe_resource_data(HANDLE module, HANDLE resource,
			     struct pe_resource_data *data)
{
	struct pe_resource_root r;
	if (!pe_resource_root(module, &r))
		return false;
	if ((uintptr_t)resource < r.base ||
	    (uintptr_t)resource > r.base + r.size ||
	    16 > r.base + r.size - (uintptr_t)resource ||
	    !diagnostic_image_read((uintptr_t)resource, data, 16) ||
	    data->rva >= pe_main_end - pe_main_base ||
	    data->size > pe_main_end - pe_main_base - data->rva) {
		SetLastError(6);
		return false;
	}
	return true;
}
__attribute__((ms_abi)) HANDLE LoadResource(HANDLE module, HANDLE resource)
{
	struct pe_resource_data d;
	return pe_resource_data(module, resource, &d) ?
		       (HANDLE)(pe_main_base + d.rva) :
		       NULL;
}
__attribute__((ms_abi)) DWORD SizeofResource(HANDLE module, HANDLE resource)
{
	struct pe_resource_data d;
	return pe_resource_data(module, resource, &d) ? d.size : 0;
}
__attribute__((ms_abi)) void *LockResource(HANDLE data)
{
	if ((uintptr_t)data < pe_main_base || (uintptr_t)data >= pe_main_end) {
		SetLastError(6);
		return NULL;
	}
	return data;
}
static bool pe_string_resource(HANDLE module, unsigned id,
			       const uint16_t **text, unsigned *len)
{
	HANDLE resource = FindResourceW(
		module, (void *)(uintptr_t)((id >> 4) + 1), (void *)6);
	if (!resource)
		return false;
	struct pe_resource_data d;
	if (!pe_resource_data(module, resource, &d))
		return false;
	uintptr_t p = pe_main_base + d.rva, end = p + d.size;
	for (unsigned i = 0; i <= (id & 15); i++) {
		uint16_t n;
		if (p > end || end - p < 2 ||
		    !diagnostic_image_read(p, &n, 2) ||
		    (size_t)n * 2 > end - p - 2) {
			SetLastError(1812);
			return false;
		}
		p += 2;
		if (i == (id & 15)) {
			*text = (const uint16_t *)p;
			*len = n;
			return true;
		}
		p += n * 2;
	}
	return false;
}
__attribute__((ms_abi)) int LoadStringW(HANDLE module, unsigned id,
					uint16_t *out, int cap)
{
	if (!out || cap < 0) {
		SetLastError(87);
		return 0;
	}
	const uint16_t *text;
	unsigned len;
	if (!pe_string_resource(module, id, &text, &len))
		return 0;
	if (!cap) {
		*(const uint16_t **)out = text;
		return len;
	}
	unsigned n = len < (unsigned)cap ? len : cap - 1;
	if (n && !diagnostic_image_read((uintptr_t)text, out, n * 2))
		return 0;
	out[n] = 0;
	return n;
}
__attribute__((ms_abi)) int LoadStringA(HANDLE module, unsigned id, char *out,
					int cap)
{
	if (!out || cap <= 0) {
		SetLastError(87);
		return 0;
	}
	const uint16_t *text;
	unsigned len;
	if (!pe_string_resource(module, id, &text, &len))
		return 0;
	if (!len) {
		out[0] = 0;
		return 0;
	}
	uint16_t *copy = pe_raw_map(len * 2);
	if (!copy)
		return 0;
	if (!diagnostic_image_read((uintptr_t)text, copy, len * 2)) {
		raw_syscall3(SYS_munmap, (long)copy, len * 2, 0);
		return 0;
	}
	int bytes =
		WideCharToMultiByte(65001, 0, copy, len, NULL, 0, NULL, NULL);
	char *utf = pe_raw_map(bytes ? bytes : 1);
	if (!utf) {
		raw_syscall3(SYS_munmap, (long)copy, len * 2, 0);
		return 0;
	}
	WideCharToMultiByte(65001, 0, copy, len, utf, bytes, NULL, NULL);
	unsigned n = bytes < (int)cap ? bytes : cap - 1;
	while (n < (unsigned)bytes && n &&
	       ((unsigned char)utf[n] & 0xc0) == 0x80)
		n--;
	memcpy(out, utf, n);
	out[n] = 0;
	raw_syscall3(SYS_munmap, (long)utf, bytes ? bytes : 1, 0);
	raw_syscall3(SYS_munmap, (long)copy, len * 2, 0);
	return n;
}
typedef BOOL(__attribute__((ms_abi)) *
	     pe_enum_resources)(HANDLE, const uint16_t *, uint16_t *, intptr_t);
__attribute__((ms_abi)) BOOL EnumResourceNamesW(HANDLE module,
						const uint16_t *type,
						pe_enum_resources callback,
						intptr_t parameter)
{
	if (!callback) {
		SetLastError(87);
		return 0;
	}
	struct pe_resource_root r;
	if (!pe_resource_root(module, &r))
		return 0;
	uint32_t off;
	if (!pe_resource_find(&r, 0, type, &off) || !(off & 0x80000000u)) {
		SetLastError(1813);
		return 0;
	}
	unsigned count = pe_resource_count(&r, off & 0x7fffffffu);
	for (unsigned i = 0; i < count; i++) {
		struct pe_resource_entry e;
		if (!pe_resource_read(&r, (off & 0x7fffffffu) + 16 + i * 8, &e,
				      8))
			return 0;
		uint16_t *name = (void *)(uintptr_t)e.name;
		size_t bytes = 0;
		if (e.name & 0x80000000u) {
			uint32_t pos = e.name & 0x7fffffffu;
			uint16_t len;
			if (!pe_resource_read(&r, pos, &len, 2))
				return 0;
			bytes = (len + 1) * 2;
			name = pe_raw_map(bytes);
			if (!name)
				return 0;
			if (!pe_resource_read(&r, pos + 2, name, len * 2)) {
				raw_syscall3(SYS_munmap, (long)name, bytes, 0);
				return 0;
			}
		}
		BOOL keep = callback(module, type, name, parameter);
		if (bytes)
			raw_syscall3(SYS_munmap, (long)name, bytes, 0);
		if (!keep) {
			SetLastError(15106);
			return 0;
		}
	}
	return 1;
}
/* Image handles retain real resource data. Bitmap raster operations and PNG icon
 * rendering remain separate work; OEM NULL-module metadata compatibility stays. */
__attribute__((ms_abi)) HANDLE LoadImageW(HANDLE module, const uint16_t *name,
					  unsigned type, int width, int height,
					  unsigned flags)
{
	if (!module)
		return pe_headless_LoadImageW(module, name, type, width, height,
					      flags);
	if (type > 2 || width < 0 || height < 0 || (flags & 0x10)) {
		SetLastError(50);
		return NULL;
	}
	HANDLE r = FindResourceW(module, name,
				 (void *)(uintptr_t)(type == 0 ? 2 :
						     type == 1 ? 14 :
								 12));
	if (!r)
		return NULL;
	struct pe_resource_data data;
	if (!pe_resource_data(module, r, &data))
		return NULL;
	int actual_w = width, actual_h = height;
	unsigned bpp = 32, planes = 1;
	uintptr_t pixels = 0;
	if (type) {
		unsigned char header[6], entry[14];
		if (data.size < 20 ||
		    !diagnostic_image_read(pe_main_base + data.rva, header,
					   6) ||
		    !header[4]) {
			SetLastError(1812);
			return NULL;
		}
		if (!diagnostic_image_read(pe_main_base + data.rva + 6, entry,
					   14))
			return NULL;
		uint16_t id;
		memcpy(&id, entry + 12, 2);
		if (type == 1) {
			actual_w = width ? width : entry[0] ? entry[0] : 256;
			actual_h = height ? height : entry[1] ? entry[1] : 256;
		} else {
			uint16_t wh[2];
			memcpy(wh, entry, 4);
			actual_w = width ? width : wh[0];
			actual_h = height ? height : wh[1];
		}
		r = FindResourceW(module, (void *)(uintptr_t)id,
				  (void *)(uintptr_t)(type == 1 ? 3 : 1));
		if (!r || !pe_resource_data(module, r, &data))
			return NULL;
	} else {
		unsigned char dib[40];
		if (data.size < 40 ||
		    !diagnostic_image_read(pe_main_base + data.rva, dib, 40)) {
			SetLastError(1812);
			return NULL;
		}
		uint32_t header, used;
		int32_t w, h;
		uint16_t pb[2];
		memcpy(&header, dib, 4);
		memcpy(&w, dib + 4, 4);
		memcpy(&h, dib + 8, 4);
		memcpy(pb, dib + 12, 4);
		memcpy(&used, dib + 32, 4);
		if (header < 40 || header > data.size || w <= 0 ||
		    h == INT32_MIN || h == 0 || pb[0] != 1 || !pb[1] ||
		    pb[1] > 32) {
			SetLastError(1812);
			return NULL;
		}
		if (!actual_w)
			actual_w = w;
		if (!actual_h)
			actual_h = h < 0 ? -h : h;
		bpp = pb[1];
		planes = pb[0];
		uint64_t palette = used ? used : bpp <= 8 ? 1u << bpp : 0;
		if (header + palette * 4 <= data.size)
			pixels = pe_main_base + data.rva + header + palette * 4;
	}
	HANDLE h = pe_headless_LoadImageW(module, name, type, actual_w,
					  actual_h, flags);
	if (!h)
		return NULL;
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_lookup(h, 0);
	if (!o)
		return NULL;
	o->image.data = pe_main_base + data.rva;
	o->image.bytes = data.size;
	if (!type) {
		o->u.bitmap.planes = planes;
		o->u.bitmap.bpp = bpp;
		o->u.bitmap.width_bytes =
			(((uint64_t)(unsigned)actual_w * bpp + 31) / 32) * 4;
		o->u.bitmap.bits = (void *)pixels;
	}
	return h;
}
__attribute__((ms_abi)) HANDLE LoadBitmapW(HANDLE module, const uint16_t *name)
{
	return LoadImageW(module, name, 0, 0, 0, 0);
}
