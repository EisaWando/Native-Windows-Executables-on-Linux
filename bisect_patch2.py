import re

with open("win32_bridge.c", "r") as f:
    text = f.read()

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
		"jmp *%r12\\n\\t"
	);
}"""

# since python `re` can be messy with newlines, let's just slice it.
start_idx = text.find("__attribute__((naked, noreturn)) void _start(void)")
if start_idx != -1:
    text = text[:start_idx] + new_start + "\n"

with open("win32_bridge.c", "w") as f:
    f.write(text)
