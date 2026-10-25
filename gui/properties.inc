/* Window properties are independent of presentation and may contain NULL data. */
struct pe_property {
	struct pe_property *next;
	uintptr_t atom;
	uint16_t name[256];
	HANDLE value;
};
static bool pe_property_matches(struct pe_property *p, const uint16_t *name)
{
	return (uintptr_t)name < 65536 ?
		       p->atom == (uintptr_t)name :
		       !p->atom && pe_name_equal(p->name, name);
}
__attribute__((ms_abi)) BOOL SetPropW(HANDLE hwnd, const uint16_t *name,
				      HANDLE value)
{
	if (!name) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	struct pe_property *p = w->properties;
	while (p && !pe_property_matches(p, name))
		p = p->next;
	if (!p) {
		p = pe_raw_map(sizeof(*p));
		if (!p) {
			SetLastError(8);
			return 0;
		}
		if ((uintptr_t)name < 65536)
			p->atom = (uintptr_t)name;
		else if (!pe_copy_name(p->name, 256, name)) {
			raw_syscall3(SYS_munmap, (long)p, sizeof(*p), 0);
			return 0;
		}
		p->next = w->properties;
		w->properties = p;
	}
	p->value = value;
	return 1;
}
__attribute__((ms_abi)) HANDLE GetPropW(HANDLE hwnd, const uint16_t *name)
{
	if (!name) {
		SetLastError(87);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return NULL;
	for (struct pe_property *p = w->properties; p; p = p->next)
		if (pe_property_matches(p, name))
			return p->value;
	return NULL;
}
__attribute__((ms_abi)) HANDLE RemovePropW(HANDLE hwnd, const uint16_t *name)
{
	if (!name) {
		SetLastError(87);
		return NULL;
	}
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return NULL;
	struct pe_property **link = &w->properties;
	while (*link && !pe_property_matches(*link, name))
		link = &(*link)->next;
	if (!*link)
		return NULL;
	struct pe_property *p = *link;
	HANDLE old = p->value;
	*link = p->next;
	raw_syscall3(SYS_munmap, (long)p, sizeof(*p), 0);
	return old;
}
__attribute__((ms_abi)) BOOL SetPropA(HANDLE hwnd, const char *name,
				      HANDLE value)
{
	if ((uintptr_t)name < 65536)
		return SetPropW(hwnd, (const uint16_t *)name, value);
	uint16_t wide[256];
	if (!MultiByteToWideChar(65001, 8, name, -1, wide, 256))
		return 0;
	return SetPropW(hwnd, wide, value);
}
__attribute__((ms_abi)) HANDLE GetPropA(HANDLE hwnd, const char *name)
{
	if ((uintptr_t)name < 65536)
		return GetPropW(hwnd, (const uint16_t *)name);
	uint16_t wide[256];
	if (!MultiByteToWideChar(65001, 8, name, -1, wide, 256))
		return NULL;
	return GetPropW(hwnd, wide);
}
__attribute__((ms_abi)) HANDLE RemovePropA(HANDLE hwnd, const char *name)
{
	if ((uintptr_t)name < 65536)
		return RemovePropW(hwnd, (const uint16_t *)name);
	uint16_t wide[256];
	if (!MultiByteToWideChar(65001, 8, name, -1, wide, 256))
		return NULL;
	return RemovePropW(hwnd, wide);
}
static void pe_properties_clear(struct pe_window *w)
{
	while (w->properties) {
		struct pe_property *p = w->properties;
		w->properties = p->next;
		raw_syscall3(SYS_munmap, (long)p, sizeof(*p), 0);
	}
}
