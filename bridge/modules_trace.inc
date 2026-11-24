/* BRIDGE_TRACE_MODULES=1: log every LoadLibraryA/W/ExA/ExW, GetModuleHandle*, GetProcAddress
 * (name, result, caller PC in notepad++.exe). */
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
		if (idx < sizeof(builtin_module_names)/sizeof(builtin_module_names[0]))
			return builtin_module_names[idx];
	}
	return "unknown";
}
/* Output the return address (in notepad++.exe) from which this function was called.
 * Uses the handle to get the module name for builtin modules. */
static void pe_trace_caller_rva(const char *func, const char *mod, void *handle)
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
	raw_puts(" rva=");
	raw_hex((uintptr_t)__builtin_return_address(0));
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
	raw_puts(" caller=");
	raw_hex((uintptr_t)__builtin_return_address(0));
	raw_puts("\n");
}