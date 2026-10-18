// SPDX-License-Identifier: GPL-2.0-only
/*
 * Raw-mapped PE import bridge. This DSO has no libc dependency: binfmt_pe
 * maps it directly, so the ELF dynamic loader and its PLT resolver do not run.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

#define STD_INPUT_HANDLE  ((int32_t)-10)
#define STD_OUTPUT_HANDLE ((int32_t)-11)
#define STD_ERROR_HANDLE  ((int32_t)-12)
#define IMAGE_ORDINAL_FLAG64 0x8000000000000000ULL
#define UNKNOWN_STUBS 1024

typedef void *HANDLE;
typedef uint32_t DWORD;
typedef int32_t BOOL;

struct image_import_descriptor {
	uint32_t original_first_thunk;
	uint32_t time_date_stamp;
	uint32_t forwarder_chain;
	uint32_t name;
	uint32_t first_thunk;
} __attribute__((packed));

struct kernel_sigaction {
	void (*handler)(int, siginfo_t *, void *);
	unsigned long flags;
	void (*restorer)(void);
	unsigned long mask;
};

struct kernel_stack {
	void *sp;
	int flags;
	size_t size;
};

struct unknown_import {
	const char *dll;
	const char *function;
	uint16_t ordinal;
	bool by_ordinal;
};

static unsigned char crash_stack[64 * 1024];
static unsigned char unknown_code[UNKNOWN_STUBS][10];
static struct unknown_import unknown_imports[UNKNOWN_STUBS];
static unsigned int unknown_count;

static long raw_syscall4(long number, long arg1, long arg2, long arg3,
			 long arg4)
{
	long result;
	register long r10 __asm__("r10") = arg4;

	__asm__ volatile ("syscall"
			: "=a"(result)
			: "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
			: "rcx", "r11", "memory");
	return result;
}

static long raw_syscall3(long number, long arg1, long arg2, long arg3)
{
	long result;

	__asm__ volatile ("syscall"
			: "=a"(result)
			: "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
			: "rcx", "r11", "memory");
	return result;
}

static void raw_write(const char *buffer, size_t length)
{
	while (length) {
		long written = raw_syscall3(SYS_write, 2, (long)buffer, length);

		if (written <= 0)
			return;
		buffer += written;
		length -= written;
	}
}

static size_t cstring_length(const char *string)
{
	size_t length = 0;

	while (string[length])
		length++;
	return length;
}

static void raw_puts(const char *string)
{
	raw_write(string, cstring_length(string));
}


static void raw_uint(unsigned int value)
{
	char buf[16];
	int pos = 15;
	buf[15] = '\0';
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

static void raw_hex(uint64_t value)
{
	char text[18] = "0x0000000000000000";
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 17; i >= 2; i--) {
		text[i] = hex[value & 0xf];
		value >>= 4;
	}
	raw_write(text, sizeof(text));
}

static bool ascii_equal_ci(const char *left, const char *right)
{
	while (*left && *right) {
		char a = *left++;
		char b = *right++;

		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z')
			b += 'a' - 'A';
		if (a != b)
			return false;
	}
	return *left == *right;
}

static void dump_maps(void)
{
	char buffer[1024];
	long fd, count;

	fd = raw_syscall4(SYS_openat, -100, (long)"/proc/self/maps", 0, 0);
	if (fd < 0)
		return;
	raw_puts("BRIDGE: /proc/self/maps\n");
	while ((count = raw_syscall3(SYS_read, fd, (long)buffer,
				     sizeof(buffer))) > 0)
		raw_write(buffer, count);
	raw_syscall3(SYS_close, fd, 0, 0);
}

__attribute__((naked)) static void sigreturn_restorer(void)
{
	__asm__ volatile ("mov $15, %rax\n\tsyscall");
}

static void crash_handler(int signal, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;
	greg_t *g = uc->uc_mcontext.gregs;
	uintptr_t stack;
	int i;

	raw_puts("BRIDGE: SIGSEGV signal="); raw_hex(signal);
	raw_puts(" addr="); raw_hex((uintptr_t)info->si_addr);
	raw_puts("\nBRIDGE: RIP="); raw_hex(g[REG_RIP]);
	raw_puts(" RSP="); raw_hex(g[REG_RSP]);
	raw_puts(" RBP="); raw_hex(g[REG_RBP]);
	raw_puts(" RAX="); raw_hex(g[REG_RAX]);
	raw_puts(" RBX="); raw_hex(g[REG_RBX]);
	raw_puts(" RCX="); raw_hex(g[REG_RCX]);
	raw_puts(" RDX="); raw_hex(g[REG_RDX]);
	raw_puts(" RSI="); raw_hex(g[REG_RSI]);
	raw_puts(" RDI="); raw_hex(g[REG_RDI]);
	raw_puts(" R8="); raw_hex(g[REG_R8]);
	raw_puts(" R9="); raw_hex(g[REG_R9]);
	raw_puts(" R10="); raw_hex(g[REG_R10]);
	raw_puts(" R11="); raw_hex(g[REG_R11]);
	raw_puts(" R12="); raw_hex(g[REG_R12]);
	raw_puts(" R13="); raw_hex(g[REG_R13]);
	raw_puts(" R14="); raw_hex(g[REG_R14]);
	raw_puts(" R15="); raw_hex(g[REG_R15]);
	raw_puts(" EFL="); raw_hex(g[REG_EFL]);
	raw_puts("\nBRIDGE: stack qwords\n");

	stack = g[REG_RSP];
	for (i = 0; i < 16; i++) {
		raw_puts("  "); raw_hex(stack + i * sizeof(uint64_t));
		raw_puts(": "); raw_hex(((volatile uint64_t *)stack)[i]);
		raw_puts("\n");
	}
	dump_maps();
	raw_syscall3(SYS_exit_group, 128 + SIGSEGV, 0, 0);
}

static void install_crash_handler(void)
{
	struct kernel_stack stack = {
		.sp = crash_stack,
		.flags = 0,
		.size = sizeof(crash_stack),
	};
	struct kernel_sigaction action = {
		.handler = crash_handler,
		.flags = SA_SIGINFO | SA_ONSTACK | SA_RESTORER,
		.restorer = sigreturn_restorer,
		.mask = 0,
	};

	raw_syscall3(SYS_sigaltstack, (long)&stack, 0, 0);
	raw_syscall4(SYS_rt_sigaction, SIGSEGV, (long)&action, 0,
		     sizeof(action.mask));
}

__attribute__((ms_abi)) HANDLE GetStdHandle(DWORD which)
{
	switch ((int32_t)which) {
	case STD_INPUT_HANDLE:
		return (HANDLE)0;
	case STD_OUTPUT_HANDLE:
		return (HANDLE)1;
	case STD_ERROR_HANDLE:
		return (HANDLE)2;
	default:
		return (HANDLE)(intptr_t)-1;
	}
}

__attribute__((ms_abi)) BOOL WriteFile(HANDLE handle, const void *buffer,
					DWORD length, DWORD *written, void *overlapped)
{
	long result;

	(void)overlapped;
	result = raw_syscall3(SYS_write, (intptr_t)handle, (long)buffer, length);
	if (written)
		*written = result > 0 ? (DWORD)result : 0;
	return result == (long)length;
}

__attribute__((ms_abi, noreturn)) void ExitProcess(DWORD status)
{
	raw_syscall3(SYS_exit_group, status, 0, 0);
	for (;;)
		__asm__ volatile ("hlt");
}

static __attribute__((noreturn)) void unimplemented_dispatch(unsigned int index)
{
	struct unknown_import *item = &unknown_imports[index];

	raw_puts("[BRIDGE] CALLED UNIMPLEMENTED: ");
	raw_puts(item->dll);
	raw_puts("!");
	if (item->by_ordinal) {
		raw_puts("ordinal "); raw_hex(item->ordinal);
	} else {
		raw_puts(item->function);
	}
	raw_puts("\n");
	raw_syscall3(SYS_exit_group, 127, 0, 0);
	for (;;)
		__asm__ volatile ("hlt");
}

static void *make_unknown(const char *dll, const char *function,
			  uint16_t ordinal, bool by_ordinal)
{
	unsigned int index;
	unsigned char *stub;
	int32_t displacement;

	if (unknown_count >= UNKNOWN_STUBS)
		return unknown_code[0];
	index = unknown_count++;
	unknown_imports[index].dll = dll;
	unknown_imports[index].function = function;
	unknown_imports[index].ordinal = ordinal;
	unknown_imports[index].by_ordinal = by_ordinal;
	stub = unknown_code[index];
	stub[0] = 0xbf; /* mov $index, %edi */
	*(uint32_t *)&stub[1] = index;
	stub[5] = 0xe9; /* jmp unimplemented_dispatch */
	displacement = (uint8_t *)unimplemented_dispatch - (stub + 10);
	*(int32_t *)&stub[6] = displacement;
	return stub;
}

/* The shim table is code-addressed: raw mapping needs no ELF relocations. */
static void *find_shim(const char *dll, const char *function, uint16_t ordinal,
		       bool by_ordinal)
{
	(void)dll;
	if (!by_ordinal && ascii_equal_ci(function, "GetStdHandle"))
		return GetStdHandle;
	if (!by_ordinal && ascii_equal_ci(function, "WriteFile"))
		return WriteFile;
	if (!by_ordinal && ascii_equal_ci(function, "ExitProcess"))
		return ExitProcess;
	return make_unknown(dll, function, ordinal, by_ordinal);
}

static void resolve_imports(uintptr_t image_base, uint32_t import_rva,
			    uint32_t import_size, bool verbose)
{
	struct image_import_descriptor *descriptor;
	uint32_t offset;
	unsigned int resolved = 0, unimplemented = 0;

	if (!import_rva || import_size < sizeof(*descriptor)) {
		raw_puts("BRIDGE: no import directory\n");
		return;
	}

	if (verbose) {
		raw_puts("BRIDGE: resolving imports at "); raw_hex(image_base + import_rva);
		raw_puts(" size="); raw_hex(import_size); raw_puts("\n");
	}
	for (offset = 0; offset + sizeof(*descriptor) <= import_size;
	     offset += sizeof(*descriptor)) {
		uint64_t *lookup, *iat;
		const char *dll;
		uint32_t thunk_rva;

		descriptor = (void *)(image_base + import_rva + offset);
		if (!descriptor->name && !descriptor->first_thunk)
			break;
		dll = (const char *)(image_base + descriptor->name);
		thunk_rva = descriptor->original_first_thunk ?
			descriptor->original_first_thunk : descriptor->first_thunk;
		lookup = (uint64_t *)(image_base + thunk_rva);
		iat = (uint64_t *)(image_base + descriptor->first_thunk);

		for (; *lookup; lookup++, iat++) {
			uint64_t thunk = *lookup;
			const char *function = NULL;
			uint16_t ordinal = 0;
			bool by_ordinal = thunk & IMAGE_ORDINAL_FLAG64;
			void *shim;
			unsigned int old_unknown = unknown_count;

			if (by_ordinal)
				ordinal = thunk & 0xffff;
			else
				function = (const char *)(image_base + (uint32_t)thunk + 2);
			shim = find_shim(dll, function, ordinal, by_ordinal);
			*iat = (uintptr_t)shim;

			if (unknown_count > old_unknown)
				unimplemented++;
			else
				resolved++;

			if (verbose) {
				raw_puts("BRIDGE: IAT "); raw_puts(dll); raw_puts("!");
				if (by_ordinal) {
					raw_puts("ordinal "); raw_hex(ordinal);
				} else {
					raw_puts(function);
				}
				raw_puts(" -> "); raw_hex((uintptr_t)shim); raw_puts("\n");
			}
		}
	}

	if (!verbose) {
		raw_puts("[BRIDGE] imports: resolved ");
		raw_uint(resolved);
		raw_puts(", unimplemented ");
		raw_uint(unimplemented);
		raw_puts("\n");
	}
}

__attribute__((used, visibility("hidden")))
void bridge_bootstrap(uintptr_t image_base, uint32_t import_rva,
		      uint32_t import_size, uintptr_t *sp)
{
	bool verbose = false;
	(void)sp;
	install_crash_handler();
#ifdef BRIDGE_VERBOSE
	verbose = true;
#endif
	resolve_imports(image_base, import_rva, import_size, verbose);
}

/* RDI=PE entry, RSI=PEB, RDX=image base, RCX=import RVA, R8=import size. */
__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile (
		/* Step 0: raw write "[BRIDGE] entered\n" before touching anything */
		"push %rdi\n\t"
		"push %rsi\n\t"
		"push %rdx\n\t"
		"push %rcx\n\t"
		"push %r8\n\t"
		"push %r9\n\t"
		"push %r10\n\t"
		"push %r11\n\t"
		"mov $1, %rax\n\t"
		"mov $2, %rdi\n\t"
		"lea 1f(%rip), %rsi\n\t"
		"mov $17, %rdx\n\t"
		"syscall\n\t"
		"jmp 2f\n\t"
		"1: .ascii \"[BRIDGE] entered\\n\"\n\t"
		"2:\n\t"
		"pop %r11\n\t"
		"pop %r10\n\t"
		"pop %r9\n\t"
		"pop %r8\n\t"
		"pop %rcx\n\t"
		"pop %rdx\n\t"
		"pop %rsi\n\t"
		"pop %rdi\n\t"

		/* Save PE entry VA (RDI) in R12 for the call below */
		"mov %rdi, %r12\n\t"

		/* Call bridge_bootstrap(image_base, import_rva, import_size, sp):
		 * System-V args: rdi=image_base rdx=import_rva, rsi=import_size...
		 * Kernel hands us: RDI=PE entry, RDX=image_base, RCX=import_rva, R8=import_size */
		"mov %rdx, %rdi\n\t"   /* arg1: image_base */
		"mov %rcx, %rsi\n\t"   /* arg2: import_rva */
		"mov %r8,  %rdx\n\t"   /* arg3: import_size */
		"mov %rsp, %rcx\n\t"   /* arg4: stack pointer */
		"call bridge_bootstrap\n\t"

		/* Now call PE entry as Windows x64 (ms_abi):
		 *   - Windows x64 ABI requires RSP to be 16-byte aligned immediately
		 *     BEFORE executing a 'call' instruction.
		 *   - Caller must allocate 32 bytes of shadow space (home space).
		 *
		 * Align RSP to 16 bytes, then allocate 32 bytes of shadow space (32 is a
		 * multiple of 16, preserving 16-byte alignment before the call).
		 * The 'call' instruction pushes the 8-byte return address, leaving RSP
		 * at (16k + 8) upon entry to the target function, exactly as ms_abi expects.
		 */
		"and $-16, %rsp\n\t"   /* align to 16 */
		"sub $32, %rsp\n\t"    /* 32 bytes shadow space (preserves 16-byte alignment) */
		"call *%r12\n\t"       /* call PE entry (ms_abi, e.g. mainCRTStartup) */

		/* If entry returns, exit cleanly */
		"mov %rax, %rdi\n\t"
		"mov $231, %rax\n\t"   /* SYS_exit_group */
		"syscall\n\t"
		"ud2\n\t"
	);
}
