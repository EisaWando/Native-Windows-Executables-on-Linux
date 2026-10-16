import re

with open("win32_bridge.c", "r") as f:
    text = f.read()

# 1. Update _start to include the raw syscall
new_start = """__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile (
		"push %rdi\\n\\t"
		"push %rsi\\n\\t"
		"push %rdx\\n\\t"
		"push %rcx\\n\\t"
		"push %r8\\n\\t"
		"push %r9\\n\\t"
		"push %r10\\n\\t"
		"push %r11\\n\\t"
		"mov $1, %rax\\n\\t"
		"mov $2, %rdi\\n\\t"
		"lea 1f(%rip), %rsi\\n\\t"
		"mov $17, %rdx\\n\\t"
		"syscall\\n\\t"
		"jmp 2f\\n\\t"
		"1: .ascii \\"[BRIDGE] entered\\\\n\\"\\n\\t"
		"2:\\n\\t"
		"pop %r11\\n\\t"
		"pop %r10\\n\\t"
		"pop %r9\\n\\t"
		"pop %r8\\n\\t"
		"pop %rcx\\n\\t"
		"pop %rdx\\n\\t"
		"pop %rsi\\n\\t"
		"pop %rdi\\n\\t"

		"mov %rdi, %r12\\n\\t"
		"mov %rdx, %rdi\\n\\t"
		"mov %rcx, %rsi\\n\\t"
		"mov %r8, %rdx\\n\\t"
		"mov %rsp, %rcx\\n\\t"
		"call bridge_bootstrap\\n\\t"
		"jmp *%r12\\n\\t");
}"""

# Replace the existing _start
text = re.sub(r'__attribute__\(\(naked, noreturn\)\) void _start\(void\).*?}', new_start, text, flags=re.DOTALL)

# 2. Add breadcrumbs to bridge_bootstrap
old_bootstrap = """	bool verbose = false;
	long argc = sp[0];
	uintptr_t *envp = sp + 1 + argc + 1;
	while (*envp) {
		if (string_equals((const char *)*envp, "BRIDGE_VERBOSE=1"))
			verbose = true;
		envp++;
	}

	install_crash_handler();
	resolve_imports(image_base, import_rva, import_size, verbose);"""

new_bootstrap = """	bool verbose = false;
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

text = text.replace(old_bootstrap, new_bootstrap)

with open("win32_bridge.c", "w") as f:
    f.write(text)
