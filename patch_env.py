import re

with open("win32_bridge.c", "r") as f:
    text = f.read()

old_bootstrap = """	bool verbose = false;
	install_crash_handler();
	raw_puts("[BRIDGE] stage 1 (before env walk)\\n");
	long argc = sp[0];
	uintptr_t *envp = sp + 1 + argc + 1;
	while (*envp) {
		if (string_equals((const char *)*envp, "BRIDGE_VERBOSE=1"))
			verbose = true;
		envp++;
	}
	raw_puts("[BRIDGE] stage 2 (after env walk)\\n");
	resolve_imports(image_base, import_rva, import_size, verbose);
	raw_puts("[BRIDGE] stage 3 (after import walk)\\n");"""

new_bootstrap = """	bool verbose = false;
	install_crash_handler();
	raw_puts("[BRIDGE] stage 1 (before env walk)\\n");
#ifdef BRIDGE_VERBOSE
	verbose = true;
#endif
	raw_puts("[BRIDGE] stage 2 (after env walk)\\n");
	resolve_imports(image_base, import_rva, import_size, verbose);
	raw_puts("[BRIDGE] stage 3 (after import walk)\\n");"""

text = text.replace(old_bootstrap, new_bootstrap)

with open("win32_bridge.c", "w") as f:
    f.write(text)
