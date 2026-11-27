/* BRIDGE_TRACE_MODULES=1: log every LoadLibraryA/W/ExA/ExW, GetModuleHandle*, GetProcAddress
 * (name, result, caller PC in notepad++.exe). 
 * rva= shown only if address inside PE image; otherwise caller omitted. */
static bool pe_trace_modules(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *e = pe_host_env("BRIDGE_TRACE_MODULES");
		enabled = e && e[0] == '1' && e[1] == 0;
	}
	return enabled != 0;
}
static const char *pe_trace_modname_from_handle(void *handle)
{
	/* Extract base module name from fake module handle */
	if (handle == (void *)0x140000000ULL) return "exe";
	uintptr_t h = (uintptr_t)handle;
	if (h >= FAKE_HMODULE_BASE && h < FAKE_HMODULE_BASE + 0x100000ULL) {
		unsigned idx = (h - FAKE_HMODULE_BASE) / 0x10000ULL;
		unsigned n = sizeof(builtin_module_names)/sizeof(builtin_module_names[0]);
		if (idx < n)
			return builtin_module_names[idx];
		if (idx == n)
			return "api-ms-win-core-*";
	}
	return "unknown";
}
/* Output the return address (in notepad++.exe) from which this function was called.
 * Uses the handle to get the module name for builtin modules.
 * rva= shown only if address inside PE image (between pe_main_base and pe_main_end). */
static void pe_trace_caller_rva_w(const char *func, const uint16_t *mod, void *handle)
{
	if (!pe_trace_modules())
		return;
	raw_puts("[TRACE] ");
	raw_puts(func);
	raw_puts(" ");
	if (handle) {
		/* Use handle to get module name (works for builtin modules) */
		const char *modname = pe_trace_modname_from_handle(handle);
		if (modname) {
			raw_puts(modname);
		} else {
			/* Fallback: convert UTF-16 to UTF-8 for display */
			char buf[260];
			if (mod && WideCharToMultiByte(65001, 0, mod, -1, buf, sizeof(buf), NULL, NULL))
				raw_puts(buf);
			else
				raw_puts("(unreadable)");
		}
	} else {
		/* Convert UTF-16 to UTF-8 for display */
		char buf[260];
		if (mod && WideCharToMultiByte(65001, 0, mod, -1, buf, sizeof(buf), NULL, NULL))
			raw_puts(buf);
		else
			raw_puts(mod ? "(unreadable)" : "(null)");
	}
	raw_puts(" -> ");
	raw_hex((uintptr_t)handle);
	/* Only show rva= if caller is inside the PE image */
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (caller >= pe_main_base && caller < pe_main_end) {
		raw_puts(" rva=");
		raw_hex(caller - pe_main_base);
	}
	raw_puts("\n");
}

static void pe_trace_caller_rva_a(const char *func, const char *mod, void *handle)
{
	if (!pe_trace_modules())
		return;
	raw_puts("[TRACE] ");
	raw_puts(func);
	raw_puts(" ");
	if (handle) {
		const char *modname = pe_trace_modname_from_handle(handle);
		if (modname) {
			raw_puts(modname);
		} else {
			raw_puts(mod);
		}
	} else {
		if (mod) {
			raw_puts(mod);
		} else {
			raw_puts("(null)");
		}
	}
	raw_puts(" -> ");
	raw_hex((uintptr_t)handle);
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (caller >= pe_main_base && caller < pe_main_end) {
		raw_puts(" rva=");
		raw_hex(caller - pe_main_base);
	}
	raw_puts("\n");
}

/* Detailed trace for GetProcAddress with function name */
static void pe_trace_getprocaddr(const char *mod, const char *func, void *result)
{
	if (!pe_trace_modules())
		return;
	raw_puts("[TRACE] GetProcAddress ");
	raw_puts(mod);
	raw_puts("!");
	if (func) {
		raw_puts(func);
	} else {
		raw_puts("(null)");
	}
	raw_puts(" -> ");
	raw_hex((uintptr_t)result);
	/* Only show rva= if caller is inside the PE image */
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);
	if (caller >= pe_main_base && caller < pe_main_end) {
		raw_puts(" rva=");
		raw_hex(caller - pe_main_base);
	}
	raw_puts("\n");
}