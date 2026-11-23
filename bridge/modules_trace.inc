/* BRIDGE_TRACE_MODULES=1: log every LoadLibraryA/W/ExA/ExW, GetModuleHandle*, GetProcAddress
 * (api, name, result, caller, rva). */

/* Convert UTF-16 name to ASCII, up to 64 chars. Returns pointer to static buffer. */
static char *pe_utf16_to_ascii(const uint16_t *u16)
{
	static char buf[65];
	char *p = buf;
	for (int i = 0; i < 64 && *u16; i++, u16++) {
		uint16_t c = *u16;
		if (c >= 0xDC00 && c <= 0xDFFF && i > 0) {
			continue;
		}
		if (c >= 0xD800 && c <= 0xDBFF && i > 0) {
			*p++ = '?';
			continue;
		}
		if (c >= 0xDC00 && c <= 0xDFFF) {
			continue;
		}
		if (c >= 0xD800 && c <= 0xDBFF) {
			*p++ = '?';
			continue;
		}
		if (c <= 0x7F) {
			*p++ = (char)c;
		} else {
			*p++ = '?';
		}
	}
	*p = '\0';
	return buf;
}

/* Check if address is inside the PE image and compute RVA */
static bool pe_is_in_image(uintptr_t addr, uintptr_t *rva)
{
	extern uintptr_t pe_main_base, pe_main_end;
	if (addr >= pe_main_base && addr < pe_main_end) {
		if (rva) *rva = addr - pe_main_base;
		return true;
	}
	return false;
}

static bool pe_trace_modules(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *e;
		e = pe_host_env("BRIDGE_TRACE_MODULES");
		enabled = e && e[0] == '1' && e[1] == 0;
	}
	return enabled != 0;
}

/* Extract module name from fake module handle */
static const char *pe_trace_modname_from_handle(void *handle)
{
	extern const char *const builtin_module_names[];
	if (handle == (void *)0x140000000ULL) return "exe";
	uintptr_t h = (uintptr_t)handle;
	if (h >= FAKE_HMODULE_BASE && h < FAKE_HMODULE_BASE + 0x100000ULL) {
		unsigned idx = (h - FAKE_HMODULE_BASE) / 0x10000ULL;
		if (idx < sizeof(builtin_module_names)/sizeof(builtin_module_names[0]))
			return builtin_module_names[idx];
	}
	return "unknown";
}

/* Print caller address and RVA if in image */
static void pe_trace_caller_rva(void *caller)
{
	uintptr_t addr = (uintptr_t)caller;
	raw_puts(" caller=0x");
	raw_hex(addr);
	uintptr_t rva = 0;
	if (pe_is_in_image(addr, &rva)) {
		raw_puts(" rva=0x");
		raw_hex(rva);
	}
}