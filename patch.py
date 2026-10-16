import re

with open("win32_bridge.c", "r") as f:
    text = f.read()

# 1. raw_uint and string_equals
to_insert = """
static void raw_uint(unsigned int value)
{
	char buf[16];
	int pos = 15;
	buf[15] = '\\0';
	if (value == 0) buf[--pos] = '0';
	while (value) {
		buf[--pos] = '0' + (value % 10);
		value /= 10;
	}
	raw_write(&buf[pos], 15 - pos);
}

static bool string_equals(const char *left, const char *right)
{
	while (*left && *right && *left == *right) {
		left++; right++;
	}
	return *left == *right;
}
"""

text = text.replace("static void raw_hex", to_insert + "\nstatic void raw_hex")

# 2. bridge_bootstrap signature
text = text.replace(
    "void bridge_bootstrap(uintptr_t image_base, uint32_t import_rva,\n\t\t      uint32_t import_size)",
    "void bridge_bootstrap(uintptr_t image_base, uint32_t import_rva,\n\t\t      uint32_t import_size, uintptr_t *sp)"
)

# 3. bridge_bootstrap body
old_body = """{
	install_crash_handler();
	resolve_imports(image_base, import_rva, import_size);
}"""
new_body = """{
	bool verbose = false;
	long argc = sp[0];
	uintptr_t *envp = sp + 1 + argc + 1;
	while (*envp) {
		if (string_equals((const char *)*envp, "BRIDGE_VERBOSE=1"))
			verbose = true;
		envp++;
	}

	install_crash_handler();
	resolve_imports(image_base, import_rva, import_size, verbose);
}"""
text = text.replace(old_body, new_body)

# 4. _start
text = text.replace('"mov %r8, %rdx\\n\\t"', '"mov %r8, %rdx\\n\\t"\n\t\t"mov %rsp, %rcx\\n\\t"')

# 5. resolve_imports
old_res = "static void resolve_imports(uintptr_t image_base, uint32_t import_rva,\n\t\t\t    uint32_t import_size)"
new_res = "static void resolve_imports(uintptr_t image_base, uint32_t import_rva,\n\t\t\t    uint32_t import_size, bool verbose)"
text = text.replace(old_res, new_res)

# We need to replace the body of resolve_imports for verbose check.
# We'll use regex or just replace.
with open("win32_bridge.c", "w") as f:
    f.write(text)

