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
#include <sys/stat.h>

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

#ifndef SYS_arch_prctl
#define SYS_arch_prctl 158
#endif
#ifndef ARCH_SET_GS
#define ARCH_SET_GS 0x1001
#endif
#ifndef ARCH_GET_GS
#define ARCH_GET_GS 0x1004
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
static unsigned char unknown_code[UNKNOWN_STUBS][16];
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

__attribute__((ms_abi)) void SetLastError(DWORD err);

/* Linux limits descriptors to at most 2^20; reserve a disjoint handle range. */
static HANDLE pe_fd_to_handle(int fd)
{
    if (fd < 0 || fd >= 1048576) return (HANDLE)(intptr_t)-1;
    return (HANDLE)(uintptr_t)(0x40ULL + (uint64_t)fd * 4);
}
static int pe_handle_to_fd(HANDLE handle)
{
    uintptr_t h = (uintptr_t)handle;
    if (h < 0x40 || h >= 0x400040 || (h & 3)) return -1;
    int fd = (h - 0x40) / 4;
    return raw_syscall3(SYS_fcntl, fd, 1 /* F_GETFD */, 0) < 0 ? -1 : fd;
}

__attribute__((ms_abi)) HANDLE GetStdHandle(DWORD which)
{
	switch ((int32_t)which) {
	case STD_INPUT_HANDLE:
		return pe_fd_to_handle(0);
	case STD_OUTPUT_HANDLE:
		return pe_fd_to_handle(1);
	case STD_ERROR_HANDLE:
		return pe_fd_to_handle(2);
	default:
		SetLastError(6);
		return (HANDLE)(intptr_t)-1;
	}
}

__attribute__((ms_abi)) BOOL WriteFile(HANDLE handle, const void *buffer,
					DWORD length, DWORD *written, void *overlapped)
{
	long result;

	int fd = pe_handle_to_fd(handle);
    if (written) *written = 0;
    if (fd < 0) { SetLastError(6); return 0; }
    if (overlapped) { SetLastError(87); return 0; }
    result = raw_syscall3(SYS_write, fd, (long)buffer, length);
    if (result < 0) SetLastError(result == -9 ? 6 : result == -14 ? 998 : 5);
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

struct pe_timespec { int64_t tv_sec; int64_t tv_nsec; };

__attribute__((ms_abi))
void GetSystemTimeAsFileTime(uint64_t *out)
{
    struct pe_timespec ts;
    raw_syscall3(228, 0, (long)&ts, 0);          /* clock_gettime(CLOCK_REALTIME) */
    *out = ((uint64_t)ts.tv_sec + 11644473600ULL) * 10000000ULL
           + (uint64_t)ts.tv_nsec / 100ULL;       /* 100ns ticks since 1601-01-01 */
}

__attribute__((ms_abi))
int QueryPerformanceCounter(int64_t *out)
{
    struct pe_timespec ts;
    raw_syscall3(228, 1, (long)&ts, 0);          /* CLOCK_MONOTONIC */
    *out = ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}

__attribute__((ms_abi))
int QueryPerformanceFrequency(int64_t *out)
{
    *out = 1000000000LL;
    return 1;
}

__attribute__((ms_abi))
uint32_t GetCurrentProcessId(void)
{
    return (uint32_t)raw_syscall3(39, 0, 0, 0);   /* getpid */
}

__attribute__((ms_abi))
uint32_t GetCurrentThreadId(void)
{
    return (uint32_t)raw_syscall3(186, 0, 0, 0);  /* gettid */
}

__attribute__((ms_abi))
void SetLastError(DWORD err)
{
	__asm__ volatile ("movl %0, %%gs:0x68" : : "r"(err) : "memory");
}

__attribute__((ms_abi))
DWORD GetLastError(void)
{
	DWORD err;
	__asm__ volatile ("movl %%gs:0x68, %0" : "=r"(err));
	return err;
}

static const char *const builtin_module_names[] = {
	"kernel32",
	"kernel32.dll",
	"kernelbase",
	"kernelbase.dll",
	"ntdll",
	"ntdll.dll",
};

#define FAKE_HMODULE_BASE 0x6ff00000ULL

static bool utf16_ascii_equal_ci(const uint16_t *u16, const char *ascii)
{
	while (*u16 && *ascii) {
		uint16_t c1 = *u16++;
		char c2 = *ascii++;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';
		if (c1 != (uint16_t)(unsigned char)c2)
			return false;
	}
	return (*u16 == 0 && *ascii == '\0');
}

static bool utf16_starts_with_ci(const uint16_t *u16, const char *prefix)
{
	while (*prefix) {
		uint16_t c1 = *u16++;
		char c2 = *prefix++;
		if (!c1)
			return false;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';
		if (c1 != (uint16_t)(unsigned char)c2)
			return false;
	}
	return true;
}

static const uint16_t *strip_path_u16(const uint16_t *name)
{
	const uint16_t *p = name;
	const uint16_t *last = name;
	while (*p) {
		if (*p == '\\' || *p == '/')
			last = p + 1;
		p++;
	}
	return last;
}

static void *get_builtin_module_handle(const uint16_t *name)
{
	size_t i;
	const uint16_t *base_name;
	if (!name)
		return NULL;
	base_name = strip_path_u16(name);
	for (i = 0; i < sizeof(builtin_module_names)/sizeof(builtin_module_names[0]); i++) {
		if (utf16_ascii_equal_ci(base_name, builtin_module_names[i]))
			return (void *)(uintptr_t)(FAKE_HMODULE_BASE + i * 0x10000ULL);
	}
	if (utf16_starts_with_ci(base_name, "api-ms-win-"))
		return (void *)(uintptr_t)(FAKE_HMODULE_BASE + (sizeof(builtin_module_names)/sizeof(builtin_module_names[0])) * 0x10000ULL);
	return NULL;
}

static bool is_fake_module_handle(void *handle)
{
	uintptr_t h = (uintptr_t)handle;
	return (h >= FAKE_HMODULE_BASE && h <= FAKE_HMODULE_BASE + 0x100000ULL);
}

__attribute__((ms_abi))
void *LoadLibraryExW(const uint16_t *name, void *file, uint32_t flags)
{
	void *h;
	(void)file;
	(void)flags;

	if (name) {
		h = get_builtin_module_handle(name);
		if (h)
			return h;
	}
	SetLastError(126); /* ERROR_MOD_NOT_FOUND */
	return NULL;
}

__attribute__((ms_abi))
void *GetModuleHandleW(const uint16_t *name)
{
	void *h;
	if (!name)
		return (void *)0x140000000ULL; /* ImageBase */
	h = get_builtin_module_handle(name);
	if (h)
		return h;
	SetLastError(126); /* ERROR_MOD_NOT_FOUND */
	return NULL;
}

static void *lookup_shim(const char *dll, const char *function, uint16_t ordinal,
		        bool by_ordinal);

__attribute__((ms_abi))
void *GetProcAddress(void *m, const char *proc)
{
	void *fn;

	if ((uintptr_t)proc < 0x10000) {
		SetLastError(127); /* ERROR_PROC_NOT_FOUND */
		return NULL;
	}
	if (is_fake_module_handle(m) || m == (void *)0x140000000ULL || m == NULL) {
		fn = lookup_shim("kernel32.dll", proc, 0, false);
		if (fn)
			return fn;
	}
	SetLastError(127); /* ERROR_PROC_NOT_FOUND */
	return NULL;
}

static uintptr_t pe_main_base, pe_main_end, pe_text_start, pe_text_end;

/* Kernel-mediated copies keep diagnostic reads from causing a second fault. */
static bool diagnostic_read(uintptr_t addr, void *out, size_t size)
{
    if (addr < 0x10000 || addr > 0x7fffffffffffULL ||
        size > 0x7fffffffffffULL - addr) return false;
    long fd = raw_syscall3(SYS_open, (long)"/proc/self/mem", 0, 0);
    if (fd < 0) return false;
    long n = raw_syscall4(SYS_pread64, fd, (long)out, size, addr);
    raw_syscall3(SYS_close, fd, 0, 0);
    return n == (long)size;
}
static void diagnostic_image_setup(uintptr_t base)
{
    uint32_t nt, size; uint16_t sections, optional;
    if (!diagnostic_read(base+0x3c, &nt, 4) || nt > 0x1000 ||
        !diagnostic_read(base+nt+0x18+0x38, &size, 4) || !size ||
        !diagnostic_read(base+nt+6, &sections, 2) || sections > 96 ||
        !diagnostic_read(base+nt+20, &optional, 2)) return;
    pe_main_base = base; pe_main_end = base+size;
    for (unsigned i = 0; i < sections; i++) {
        struct { char name[8]; uint32_t size, rva; unsigned char rest[24]; } sec;
        if (!diagnostic_read(base+nt+24+optional+i*40, &sec, 40)) break;
        if (sec.name[0] == '.' && sec.name[1] == 't' && sec.name[2] == 'e' &&
            sec.name[3] == 'x' && sec.name[4] == 't' && !sec.name[5] &&
            sec.rva < size && sec.size <= size-sec.rva) {
            pe_text_start = base+sec.rva; pe_text_end = pe_text_start+sec.size;
        }
    }
}
__attribute__((ms_abi)) void *RtlPcToFileHeader(void *pc, void **base)
{
    void *result = (uintptr_t)pc >= pe_main_base && (uintptr_t)pc < pe_main_end ? (void *)pe_main_base : NULL;
    if (base) *base = result;
    return result;
}
static void diagnostic_backtrace(uintptr_t sp)
{
    uintptr_t low, high;
    __asm__ volatile ("mov %%gs:0x10, %0" : "=r"(low));
    __asm__ volatile ("mov %%gs:0x08, %0" : "=r"(high));
    raw_puts(" bt=");
    if (low >= high || sp < low || sp >= high || (sp & 7)) return;
    unsigned count = 0;
    for (unsigned i = 0; i < 512 && high-sp >= 8 && count < 10; i++, sp += 8) {
        uintptr_t value;
        if (!diagnostic_read(sp, &value, 8)) break;
        if (value >= pe_text_start && value < pe_text_end) {
            if (count++) raw_puts(" ");
            raw_hex(value);
        }
    }
}
static bool diagnostic_image_read(uintptr_t addr, void *out, size_t n)
{
    return addr >= pe_main_base && addr < pe_main_end && n <= pe_main_end-addr && diagnostic_read(addr, out, n);
}
/* Layout verified against Raymond Chen's 2010/2016 public decoding articles:
 * https://devblogs.microsoft.com/oldnewthing/20100730-00/?p=13273
 * x64 ThrowInfo+12 -> CTA+4 -> CatchableType+4 -> TypeDescriptor+16.
 * Links in that chain are 32-bit image-relative RVAs, not native pointers.
 */
static void diagnostic_exception(uintptr_t code, uintptr_t flags, uintptr_t nparams, uintptr_t ptr)
{
    raw_puts("[BRIDGE] RaiseException code="); raw_hex((uint32_t)code);
    raw_puts(" flags="); raw_hex((uint32_t)flags);
    raw_puts(" nparams="); raw_uint((uint32_t)nparams);
    uint64_t params[4] = {0};
    if ((uint32_t)code == 0xe06d7363 && nparams >= 3 &&
        diagnostic_read(ptr, params, (nparams >= 4 ? 4 : 3)*8)) {
        uintptr_t base = nparams >= 4 ? params[3] : pe_main_base;
        raw_puts(" object="); raw_hex(params[1]);
        raw_puts(" ThrowInfo="); raw_hex(params[2]);
        raw_puts(" image="); raw_hex(base);
        uint32_t ti[4], cta[2], ct[2];
        raw_puts(" type=\"");
        if (base == pe_main_base && diagnostic_image_read(params[2], ti, sizeof(ti)) &&
            diagnostic_image_read(base+ti[3], cta, sizeof(cta)) && (int32_t)cta[0] > 0 &&
            diagnostic_image_read(base+cta[1], ct, sizeof(ct))) {
            for (unsigned i = 0; i < 256; i++) {
                char c;
                if (!diagnostic_image_read(base+ct[1]+16+i, &c, 1) || !c) break;
                if (c < 32 || c > 126) c = '?';
                raw_write(&c, 1);
            }
        } else raw_puts("<unreadable>");
        raw_puts("\" bytes=");
        unsigned char bytes[32];
        if (diagnostic_read(params[1], bytes, sizeof(bytes))) {
            static const char hex[] = "0123456789abcdef";
            for (unsigned i = 0; i < sizeof(bytes); i++) {
                char pair[3] = {hex[bytes[i] >> 4], hex[bytes[i] & 15], ' '};
                raw_write(pair, sizeof(pair));
            }
        } else raw_puts("<unreadable>");
    }
    raw_puts("\n");
}

static void print_utf16_preview(uintptr_t ptr)
{
    raw_puts("\"");
    for (unsigned i = 0; i < 40; i++) {
        uint16_t c; char ch;
        if (!diagnostic_read(ptr+i*2, &c, 2) || !c) break;
        ch = c >= 32 && c <= 126 ? c : '?'; raw_write(&ch, 1);
    }
    raw_puts("\"");
}

static __attribute__((noreturn)) void unimplemented_dispatch(unsigned int index,
							     uintptr_t ret_addr,
							     uintptr_t rcx,
							     uintptr_t rdx,
							     uintptr_t r8,
							     uintptr_t r9, uintptr_t entry_sp)
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
	raw_puts(" ret="); raw_hex(ret_addr);
	raw_puts(" rcx="); raw_hex(rcx);
	raw_puts(" rdx="); raw_hex(rdx);
	raw_puts(" r8="); raw_hex(r8);
	raw_puts(" r9="); raw_hex(r9);
	raw_puts(" w1="); print_utf16_preview(rcx);
	raw_puts(" w2="); print_utf16_preview(rdx);
    diagnostic_backtrace(entry_sp);
    raw_puts("\n");
    if (!item->by_ordinal && ascii_equal_ci(item->function, "RaiseException"))
        diagnostic_exception(rcx, rdx, r8, r9);

	raw_syscall3(SYS_exit_group, 127, 0, 0);
	for (;;)
		__asm__ volatile ("hlt");
}

__attribute__((naked)) static void common_unimplemented_trampoline(void)
{
	__asm__ volatile (
		"push %rbp\n\t"
		"mov %rsp, %rbp\n\t"
		"mov %rax, %rdi\n\t"     /* arg 1: index */
		"mov 8(%rbp), %rsi\n\t"  /* arg 2: ret_addr */
		"mov %rdx, %r10\n\t"     /* save Win rdx */
		"mov %rcx, %rdx\n\t"     /* arg 3: Win rcx */
		"mov %r10, %rcx\n\t"     /* arg 4: Win rdx */
		/* arg 5 (%r8) and arg 6 (%r9) already in %r8 and %r9 */
		"and $-16, %rsp\n\t"     /* align stack to 16 bytes */
		"sub $16, %rsp\n\t"
        "lea 8(%rbp), %rax\n\t" /* exact stub-entry RSP, seventh SysV arg */
        "mov %rax, (%rsp)\n\t"
        "call unimplemented_dispatch\n\t"
		"ud2\n\t"
	);
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
	stub[0] = 0xb8; /* mov $index, %eax */
	*(uint32_t *)&stub[1] = index;
	stub[5] = 0xe9; /* jmp common_unimplemented_trampoline */
	displacement = (uint8_t *)common_unimplemented_trampoline - (stub + 10);
	*(int32_t *)&stub[6] = displacement;
	return stub;
}

/* TODO: real locks/TLS once CreateThread exists */

struct pe_crit_sec {           /* RTL_CRITICAL_SECTION x64, 40 bytes */
    void    *DebugInfo;        /* 0x00 */
    int32_t  LockCount;        /* 0x08 */
    int32_t  RecursionCount;   /* 0x0c */
    void    *OwningThread;     /* 0x10 */
    void    *LockSemaphore;    /* 0x18 */
    uint64_t SpinCount;        /* 0x20 */
};

static void pe_cs_init(struct pe_crit_sec *cs, uint64_t spin)
{ cs->DebugInfo = 0; cs->LockCount = -1; cs->RecursionCount = 0;
  cs->OwningThread = 0; cs->LockSemaphore = 0; cs->SpinCount = spin; }

__attribute__((ms_abi)) void InitializeCriticalSection(struct pe_crit_sec *cs)          { pe_cs_init(cs, 0); }
__attribute__((ms_abi)) int  InitializeCriticalSectionAndSpinCount(struct pe_crit_sec *cs, uint32_t spin) { pe_cs_init(cs, spin); return 1; }
__attribute__((ms_abi)) int  InitializeCriticalSectionEx(struct pe_crit_sec *cs, uint32_t spin, uint32_t flags) { (void)flags; pe_cs_init(cs, spin); return 1; }
__attribute__((ms_abi)) void EnterCriticalSection(struct pe_crit_sec *cs)    { cs->LockCount++; cs->RecursionCount++; }
__attribute__((ms_abi)) int  TryEnterCriticalSection(struct pe_crit_sec *cs) { cs->LockCount++; cs->RecursionCount++; return 1; }
__attribute__((ms_abi)) void LeaveCriticalSection(struct pe_crit_sec *cs)    { cs->RecursionCount--; cs->LockCount--; }
__attribute__((ms_abi)) void DeleteCriticalSection(struct pe_crit_sec *cs)   { cs->LockCount = -1; cs->RecursionCount = 0; }

#define PE_TLS_MAX 1088
static uint64_t pe_tls_slots[PE_TLS_MAX];
static uint8_t  pe_tls_used[PE_TLS_MAX];
static uint32_t pe_slot_alloc(void)
{
    uint32_t i;
    for (i = 0; i < PE_TLS_MAX; i++) {
        if (!pe_tls_used[i]) {
            pe_tls_used[i] = 1;
            pe_tls_slots[i] = 0;
            return i;
        }
    }
    return 0xFFFFFFFFu;
}

__attribute__((ms_abi)) uint32_t TlsAlloc(void)                 { uint32_t i = pe_slot_alloc(); if (i == 0xFFFFFFFFu) SetLastError(8); return i; }
__attribute__((ms_abi)) int      TlsFree(uint32_t i)            { if (i >= PE_TLS_MAX || !pe_tls_used[i]) { SetLastError(87); return 0; } pe_tls_used[i] = 0; return 1; }
__attribute__((ms_abi)) void    *TlsGetValue(uint32_t i)        { if (i >= PE_TLS_MAX || !pe_tls_used[i]) { SetLastError(87); return 0; } SetLastError(0); return (void *)pe_tls_slots[i]; }
__attribute__((ms_abi)) int      TlsSetValue(uint32_t i, void *v){ if (i >= PE_TLS_MAX || !pe_tls_used[i]) { SetLastError(87); return 0; } pe_tls_slots[i] = (uint64_t)v; return 1; }
/* Fls* share the same slot arrays (callback ignored for now): */
__attribute__((ms_abi)) uint32_t FlsAlloc(void *callback)       { (void)callback; return TlsAlloc(); }
__attribute__((ms_abi)) int      FlsFree(uint32_t i)            { return TlsFree(i); }
__attribute__((ms_abi)) void    *FlsGetValue(uint32_t i)        { return TlsGetValue(i); }
__attribute__((ms_abi)) int      FlsSetValue(uint32_t i, void *v){ return TlsSetValue(i, v); }

__attribute__((ms_abi))
int VirtualProtect(void *addr, uint64_t size, uint32_t new_prot, uint32_t *old_prot)
{
    uintptr_t start = (uintptr_t)addr & ~0xfffULL;
    uintptr_t end   = ((uintptr_t)addr + size + 0xfffULL) & ~0xfffULL;
    long prot;

    if (!old_prot) { SetLastError(998); return 0; }   /* ERROR_NOACCESS */
    if (size == 0 || end <= start) { SetLastError(87); return 0; }

    switch (new_prot & 0xff) {            /* strip GUARD/NOCACHE modifier bits */
    case 0x01: prot = 0; break;           /* PAGE_NOACCESS          -> PROT_NONE */
    case 0x02: prot = 1; break;           /* PAGE_READONLY          -> R   */
    case 0x04:
    case 0x08: prot = 3; break;           /* READWRITE / WRITECOPY  -> RW  */
    case 0x10: prot = 4; break;           /* PAGE_EXECUTE           -> X   */
    case 0x20: prot = 5; break;           /* PAGE_EXECUTE_READ      -> RX  */
    case 0x40:
    case 0x80: prot = 7; break;           /* EXECUTE_READWRITE/COPY -> RWX */
    default: SetLastError(87); return 0;
    }
    if (raw_syscall3(10, (long)start, (long)(end - start), prot) != 0) {  /* mprotect */
        SetLastError(487);                /* ERROR_INVALID_ADDRESS */
        return 0;
    }
    *old_prot = 0x04;                     /* approximation: old protection not tracked */
    return 1;
}

static inline long raw_syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile ("syscall" : "=a"(r)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return r;
}

__attribute__((used))
void *memset(void *d, int c, size_t n)
{ void *r = d; __asm__ volatile ("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }

__attribute__((used))
void *memcpy(void *d, const void *s, size_t n)
{ void *r = d; __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }

__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memmove(void *d, const void *s, size_t n)
{
    char *dest = (char *)d;
    const char *src = (const char *)s;
    if (dest < src) {
        for (size_t i = 0; i < n; i++)
            dest[i] = src[i];
    } else if (dest > src) {
        for (size_t i = n; i > 0; i--)
            dest[i - 1] = src[i - 1];
    }
    return d;
}

__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

#define PE_HEAP_HANDLE ((void *)0x6ffe0000ULL)
#define PE_HEAP_ZERO   0x00000008u
#define PE_HEAP_ARENA  (1024 * 1024)
#define PE_HEAP_LARGE  (128 * 1024)
struct pe_blk { uint64_t usable; uint64_t req; };   /* 16-byte header */
static uint8_t *pe_arena_cur, *pe_arena_end;
static struct pe_blk *pe_free_list[32];

static long pe_mmap_rw(uint64_t len)
{
    long p = raw_syscall6(9, 0, (long)len, 3, 0x22, -1, 0);  /* RW, PRIVATE|ANONYMOUS */
    return ((unsigned long)p >= (unsigned long)-4095) ? 0 : p;
}
static int pe_class(uint64_t n) { int c = 4; while ((1ULL << c) < n) c++; return c; }

static void *pe_heap_alloc(uint64_t req, uint32_t flags)
{
    struct pe_blk *b; uint64_t usable; int c;
    if (req > PE_HEAP_LARGE) {
        uint64_t total = (req + 16 + 0xfff) & ~0xfffULL;
        long p = pe_mmap_rw(total);
        if (!p) return 0;
        b = (struct pe_blk *)p; b->usable = total - 16; b->req = req;
        return b + 1;
    }
    c = pe_class(req ? req : 1);
    usable = 1ULL << c;
    if (pe_free_list[c]) {
        b = pe_free_list[c];
        pe_free_list[c] = *(struct pe_blk **)(b + 1);
        b->req = req;
        if (flags & PE_HEAP_ZERO) memset(b + 1, 0, req);
        return b + 1;
    }
    if (pe_arena_cur + usable + 16 > pe_arena_end) {
        long p = pe_mmap_rw(PE_HEAP_ARENA);
        if (!p) return 0;
        pe_arena_cur = (uint8_t *)p; pe_arena_end = pe_arena_cur + PE_HEAP_ARENA;
    }
    b = (struct pe_blk *)pe_arena_cur;
    pe_arena_cur += usable + 16;
    b->usable = usable; b->req = req;
    return b + 1;
}
static void pe_heap_free(void *p)
{
    struct pe_blk *b; int c;
    if (!p) return;
    b = (struct pe_blk *)p - 1;
    if (b->usable > PE_HEAP_LARGE) { raw_syscall3(11, (long)b, (long)(b->usable + 16), 0); return; } /* munmap */
    c = pe_class(b->usable);
    *(struct pe_blk **)p = pe_free_list[c];
    pe_free_list[c] = b;
}

__attribute__((ms_abi)) void *GetProcessHeap(void) { return PE_HEAP_HANDLE; }
__attribute__((ms_abi)) void *HeapCreate(uint32_t opt, uint64_t init, uint64_t max) { (void)opt; (void)init; (void)max; return PE_HEAP_HANDLE; }
__attribute__((ms_abi)) int   HeapDestroy(void *h) { (void)h; return 1; }
__attribute__((ms_abi)) void *HeapAlloc(void *h, uint32_t flags, uint64_t size)
{ void *p; (void)h; p = pe_heap_alloc(size, flags); if (!p) SetLastError(8); return p; }
__attribute__((ms_abi)) int   HeapFree(void *h, uint32_t flags, void *p) { (void)h; (void)flags; pe_heap_free(p); return 1; }
__attribute__((ms_abi)) uint64_t HeapSize(void *h, uint32_t flags, void *p)
{ (void)h; (void)flags; if (!p) return (uint64_t)-1; return ((struct pe_blk *)p - 1)->req; }
__attribute__((ms_abi)) void *HeapReAlloc(void *h, uint32_t flags, void *p, uint64_t size)
{
    struct pe_blk *b; void *n; uint64_t old; (void)h;
    if (!p) return 0;
    b = (struct pe_blk *)p - 1; old = b->req;
    if (size <= b->usable) {
        if ((flags & PE_HEAP_ZERO) && size > old) memset((uint8_t *)p + old, 0, size - old);
        b->req = size; return p;
    }
    n = pe_heap_alloc(size, 0);
    if (!n) { SetLastError(8); return 0; }
    memcpy(n, p, old < size ? old : size);
    if ((flags & PE_HEAP_ZERO) && size > old) memset((uint8_t *)n + old, 0, size - old);
    pe_heap_free(p);
    return n;
}
__attribute__((ms_abi)) int HeapSetInformation(void *h, int cls, void *info, uint64_t len)
{ (void)h; (void)cls; (void)info; (void)len; return 1; }

__attribute__((ms_abi)) void GetStartupInfoW(void *si) { memset(si, 0, 104); *(uint32_t *)si = 104; }
__attribute__((ms_abi)) void GetStartupInfoA(void *si) { memset(si, 0, 104); *(uint32_t *)si = 104; }
__attribute__((ms_abi)) int IsProcessorFeaturePresent(uint32_t f)
{ switch (f) { case 2: case 3: case 6: case 10: case 12: case 13: case 14: return 1; default: return 0; } }
__attribute__((ms_abi)) int IsDebuggerPresent(void) { return 0; }
static void *pe_unhandled_filter;
__attribute__((ms_abi)) void *SetUnhandledExceptionFilter(void *f) { void *old = pe_unhandled_filter; pe_unhandled_filter = f; return old; }
__attribute__((ms_abi)) int32_t UnhandledExceptionFilter(void *info) { (void)info; return 0; }
__attribute__((ms_abi)) void *EncodePointer(void *p) { return p; }
__attribute__((ms_abi)) void *DecodePointer(void *p) { return p; }
__attribute__((ms_abi)) void InitializeSListHead(void *h) { memset(h, 0, 16); }
__attribute__((ms_abi)) void *GetCurrentProcess(void) { return (void *)(intptr_t)-1; }
__attribute__((ms_abi)) void *GetCurrentThread(void) { return (void *)(intptr_t)-2; }
static const uint16_t pe_cmdline_w[] = { 'n','o','t','e','p','a','d','+','+','.','e','x','e', 0 };
static const char pe_cmdline_a[] = "notepad++.exe";      /* TODO: build from /proc/self/cmdline */
static const uint16_t pe_env_w[] = { 0, 0 };              /* TODO: real environment block */
__attribute__((ms_abi)) uint16_t *GetCommandLineW(void) { return (uint16_t *)pe_cmdline_w; }
__attribute__((ms_abi)) char *GetCommandLineA(void) { return (char *)pe_cmdline_a; }
__attribute__((ms_abi)) uint16_t *GetEnvironmentStringsW(void) { return (uint16_t *)pe_env_w; }
__attribute__((ms_abi)) int FreeEnvironmentStringsW(uint16_t *p) { (void)p; return 1; }

/* Batch B: descriptor type, UTF-8 code page metadata and monotonic time. */
__attribute__((ms_abi)) DWORD GetFileType(HANDLE h)
{
    struct stat st;
    int fd = pe_handle_to_fd(h);
    if (fd < 0) { SetLastError(6); return 0; }
    long r = raw_syscall3(SYS_fstat, fd, (long)&st, 0);
    if (r < 0) { SetLastError(r == -9 ? 6 : 5); return 0; }
    switch (st.st_mode & S_IFMT) {
    case S_IFCHR: return 2;
    case S_IFREG: case S_IFDIR: return 1;
    case S_IFIFO: case S_IFSOCK: return 3;
    default: SetLastError(0); return 0;
    }
}
__attribute__((ms_abi)) DWORD GetACP(void) { return 65001; }
__attribute__((ms_abi)) DWORD GetOEMCP(void) { return 65001; }
struct pe_cpinfo { uint32_t max_char_size; uint8_t default_char[2], lead_byte[12]; };
__attribute__((ms_abi)) BOOL GetCPInfo(DWORD cp, struct pe_cpinfo *out)
{
    if (!out || (cp != 0 && cp != 1 && cp != 65001)) { SetLastError(87); return 0; }
    memset(out, 0, sizeof(*out)); out->max_char_size = 4; out->default_char[0] = '?'; return 1;
}
__attribute__((ms_abi)) int lstrlenA(const char *s) { return s ? cstring_length(s) : 0; }
__attribute__((ms_abi)) int lstrlenW(const uint16_t *s)
{ int n = 0; if (s) while (s[n]) n++; return n; }
__attribute__((ms_abi)) void Sleep(DWORD ms)
{
    struct pe_timespec req = {ms / 1000, (ms % 1000) * 1000000LL}, rem;
    while (raw_syscall3(SYS_nanosleep, (long)&req, (long)&rem, 0) == -4) req = rem;
}
__attribute__((ms_abi)) uint64_t GetTickCount64(void)
{
    struct pe_timespec ts;
    if (raw_syscall3(SYS_clock_gettime, 1, (long)&ts, 0)) return 0;
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
__attribute__((ms_abi)) DWORD GetTickCount(void) { return GetTickCount64(); }

__attribute__((ms_abi)) BOOL IsValidCodePage(DWORD cp)
{ return cp == 0 || cp == 1 || cp == 65001; }

/* Decode one scalar; consume only the malformed prefix on replacement. */
static uint32_t pe_utf8_read(const unsigned char *s, int n, int *used, int *bad)
{
    uint32_t c = s[0], min; int k;
    *used = 1; *bad = 0;
    if (c < 128) return c;
    if (c >= 0xc2 && c <= 0xdf) { k = 2; min = 0x80; c &= 31; }
    else if (c >= 0xe0 && c <= 0xef) { k = 3; min = 0x800; c &= 15; }
    else if (c >= 0xf0 && c <= 0xf4) { k = 4; min = 0x10000; c &= 7; }
    else { *bad = 1; return 0xfffd; }
    for (int i = 1; i < k; i++) {
        if (i >= n || (s[i] & 0xc0) != 0x80 ||
            (i == 1 && ((s[0] == 0xe0 && s[i] < 0xa0) ||
                        (s[0] == 0xed && s[i] >= 0xa0) ||
                        (s[0] == 0xf0 && s[i] < 0x90) ||
                        (s[0] == 0xf4 && s[i] >= 0x90)))) {
            *bad = 1; return 0xfffd;
        }
        c = (c << 6) | (s[i] & 63); *used = i + 1;
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
        *bad = 1; return 0xfffd;
    }
    return c;
}
__attribute__((ms_abi)) int MultiByteToWideChar(DWORD cp, DWORD flags,
    const char *src, int len, uint16_t *out, int cap)
{
    if (!IsValidCodePage(cp) || !src || len == 0 || len < -1 || cap < 0 ||
        (cap && (!out || (void *)out == (void *)src))) { SetLastError(87); return 0; }
    if (flags & ~8u) { SetLastError(1004); return 0; }
    if (len == -1) len = cstring_length(src) + 1;
    int count = 0;
    for (int pass = 0; pass < (cap ? 2 : 1); pass++) {
        count = 0;
        for (int i = 0; i < len;) {
            int used, bad; uint32_t c = pe_utf8_read((const unsigned char *)src+i, len-i, &used, &bad);
            if (bad && (flags & 8)) { SetLastError(1113); return 0; }
            i += used;
            if (pass) {
                if (c > 0xffff) { out[count++] = 0xd800 + ((c-0x10000)>>10); out[count++] = 0xdc00 + ((c-0x10000)&1023); }
                else out[count++] = c;
            } else count += c > 0xffff ? 2 : 1;
        }
        if (!pass && cap && count > cap) { SetLastError(122); return 0; }
    }
    return count;
}
__attribute__((ms_abi)) int WideCharToMultiByte(DWORD cp, DWORD flags,
    const uint16_t *src, int len, char *out, int cap, const char *def, BOOL *used_def)
{
    if (!IsValidCodePage(cp) || !src || len == 0 || len < -1 || cap < 0 ||
        (cap && (!out || (void *)out == (void *)src)) || def || used_def) { SetLastError(87); return 0; }
    if (flags & ~0x80u) { SetLastError(1004); return 0; }
    if (len == -1) len = lstrlenW(src) + 1;
    int count = 0;
    for (int pass = 0; pass < (cap ? 2 : 1); pass++) {
        count = 0;
        for (int i = 0; i < len; i++) {
            uint32_t c = src[i]; int bad = 0;
            if (c >= 0xd800 && c <= 0xdbff && i+1 < len && src[i+1] >= 0xdc00 && src[i+1] <= 0xdfff)
                { c = 0x10000 + ((c-0xd800)<<10) + src[++i]-0xdc00; }
            else if (c >= 0xd800 && c <= 0xdfff) { c = 0xfffd; bad = 1; }
            if (bad && (flags & 0x80)) { SetLastError(1113); return 0; }
            int n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
            if (pass) {
                if (n == 1) out[count] = c;
                else {
                    out[count] = (n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | (c >> (6*(n-1)));
                    for (int j = 1; j < n; j++) out[count+j] = 0x80 | ((c >> (6*(n-1-j))) & 63);
                }
            }
            count += n;
        }
        if (!pass && cap && count > cap) { SetLastError(122); return 0; }
    }
    return count;
}
__attribute__((ms_abi)) DWORD SetHandleCount(DWORD count) { return count; }
__attribute__((ms_abi)) BOOL GetCPInfoExW(DWORD cp, DWORD flags, void *out)
{
    (void)flags;
    if (!out) { SetLastError(87); return 0; }
    memset(out, 0, 544);
    if (!GetCPInfo(cp, out)) return 0;
    *(uint16_t *)((char *)out + 18) = 0xfffd;
    *(DWORD *)((char *)out + 20) = 65001;
    const char *name = "Unicode (UTF-8)";
    uint16_t *dst = (uint16_t *)((char *)out + 24);
    while (*name) *dst++ = *name++;
    return 1;
}
__attribute__((ms_abi)) BOOL AreFileApisANSI(void) { return 1; }
__attribute__((ms_abi)) DWORD GetConsoleCP(void) { return 65001; }
__attribute__((ms_abi)) DWORD GetConsoleOutputCP(void) { return 65001; }

/* Z:\ maps Linux /, case preserved. Other drive letters are unsupported. */
static int pe_windows_path(char *out, int cap, int cwd)
{
    char path[4096];
    long n = cwd ? raw_syscall3(SYS_getcwd, (long)path, sizeof(path), 0) :
        raw_syscall3(SYS_readlink, (long)"/proc/self/exe", (long)path, sizeof(path)-1);
    if (n < 0 || n >= (long)sizeof(path)) { SetLastError(3); return 0; }
    if (cwd) n--; /* getcwd includes NUL */
    if (n + 3 > cap) { SetLastError(122); return 0; }
    out[0] = 'Z'; out[1] = ':';
    for (int i = 0; i < n; i++) out[i+2] = path[i] == '/' ? '\\' : path[i];
    out[n+2] = 0;
    return n+2;
}
__attribute__((ms_abi)) DWORD GetModuleFileNameW(void *module, uint16_t *out, DWORD cap)
{
    char path[4100]; uint16_t wide[4100];
    if (module && module != (void *)0x140000000ULL) { SetLastError(126); return 0; }
    if (!out || !cap) { SetLastError(122); return 0; }
    if (!pe_windows_path(path, sizeof(path), 0)) return 0;
    int n = MultiByteToWideChar(65001, 8, path, -1, wide, 4100);
    if (!n) return 0;
    DWORD copy = (DWORD)n <= cap ? n : cap;
    memcpy(out, wide, copy * 2);
    if ((DWORD)n > cap) { out[cap-1] = 0; SetLastError(122); return cap; }
    return n-1;
}
__attribute__((ms_abi)) DWORD GetModuleFileNameA(void *module, char *out, DWORD cap)
{
    char path[4100];
    if (module && module != (void *)0x140000000ULL) { SetLastError(126); return 0; }
    if (!out || !cap) { SetLastError(122); return 0; }
    int n = pe_windows_path(path, sizeof(path), 0);
    if (!n) return 0;
    DWORD copy = (DWORD)n+1 <= cap ? n+1 : cap;
    memcpy(out, path, copy);
    if ((DWORD)n >= cap) { out[cap-1] = 0; SetLastError(122); return cap; }
    return n;
}
__attribute__((ms_abi)) DWORD GetCurrentDirectoryW(DWORD cap, uint16_t *out)
{
    char path[4100]; uint16_t wide[4100];
    if (!pe_windows_path(path, sizeof(path), 1)) return 0;
    int n = MultiByteToWideChar(65001, 8, path, -1, wide, 4100);
    if (!n) return 0;
    if ((DWORD)n > cap) return n;
    if (!out) { SetLastError(87); return 0; }
    memcpy(out, wide, n*2); return n-1;
}
__attribute__((ms_abi)) DWORD GetCurrentDirectoryA(DWORD cap, char *out)
{
    char path[4100]; int n = pe_windows_path(path, sizeof(path), 1);
    if (!n) return 0;
    if ((DWORD)n >= cap) return n+1;
    if (!out) { SetLastError(87); return 0; }
    memcpy(out, path, n+1); return n;
}
/* Consistent with the intentionally empty environment block in Batch A. */
__attribute__((ms_abi)) DWORD GetEnvironmentVariableW(const uint16_t *name, uint16_t *out, DWORD cap)
{
    (void)out; (void)cap;
    SetLastError(!name || !*name ? 87 : 203); return 0;
}
__attribute__((ms_abi)) DWORD ExpandEnvironmentStringsW(const uint16_t *src, uint16_t *out, DWORD cap)
{
    if (!src || (cap && !out)) { SetLastError(87); return 0; }
    DWORD n = lstrlenW(src)+1;
    if (cap >= n) memmove(out, src, n*2);
    return n; /* unknown variables remain unchanged */
}
__attribute__((ms_abi)) void OutputDebugStringA(const char *s)
{ if (s) { raw_puts("[BRIDGE] debug: "); raw_puts(s); raw_puts("\n"); } }
__attribute__((ms_abi)) void OutputDebugStringW(const uint16_t *s)
{
    char buf[1024];
    if (!s) return;
    int n = WideCharToMultiByte(65001, 0, s, -1, 0, 0, 0, 0);
    if (n > 0 && n <= (int)sizeof(buf) && WideCharToMultiByte(65001, 0, s, -1, buf, sizeof(buf), 0, 0)) OutputDebugStringA(buf);
}

/* Headless GUI objects: no display connection, rasterization or window dispatch.
 * Handles encode slot + generation; exhausted generations are never reused. */
#define PE_GUI_BASE 0x50000000ULL
#define PE_GUI_SLOTS 1024
#define PE_GUI_LIMIT 0x60000000ULL
#define PE_GDI_PEN 1
#define PE_GDI_BRUSH 2
#define PE_GDI_DC 3
#define PE_GDI_PALETTE 5
#define PE_GDI_FONT 6
#define PE_GDI_BITMAP 7
#define PE_GDI_REGION 8
#define PE_GDI_MEMDC 10
#define PE_GUI_ICON 20
#define PE_GUI_CURSOR 21
#define PE_GUI_MENU 22
#define PE_GUI_WINDOW 23
struct pe_logfont {
    int32_t height, width, escapement, orientation, weight;
    uint8_t italic, underline, strikeout, charset, outprec, clipprec, quality, pitch;
    uint16_t face[32];
};
struct pe_logbrush { uint32_t style, color; uint64_t hatch; };
struct pe_logpen { uint32_t style; int32_t x, y; uint32_t color; };
struct pe_bitmap { int32_t type, width, height, width_bytes; uint16_t planes, bpp; void *bits; };
struct pe_gui_object {
    uint32_t type, generation, selected, shared;
    HANDLE owner;
    struct { uintptr_t module, resource; uint32_t flags; int32_t width, height; uint16_t name[64]; } image;
    union {
        struct pe_logbrush brush;
        struct pe_logpen pen;
        struct pe_logfont font;
        struct pe_bitmap bitmap;
        struct { HANDLE pen, brush, font, bitmap, palette; HANDLE owner; } dc;
        int32_t rect[4];
    } u;
};
static struct pe_gui_object pe_gui_objects[PE_GUI_SLOTS];
static HANDLE pe_stocks[20];
_Static_assert(sizeof(struct pe_logfont) == 92, "LOGFONTW layout");
_Static_assert(sizeof(struct pe_logbrush) == 16, "LOGBRUSH layout");
_Static_assert(sizeof(struct pe_bitmap) == 32, "BITMAP layout");
static HANDLE pe_gui_handle(struct pe_gui_object *o)
{
    return (HANDLE)(uintptr_t)(PE_GUI_BASE+4*((uint64_t)o->generation*PE_GUI_SLOTS+(o-pe_gui_objects)));
}
static struct pe_gui_object *pe_gui_lookup(HANDLE h, unsigned type)
{
    uintptr_t v = (uintptr_t)h;
    if (v >= PE_GUI_BASE && v < PE_GUI_LIMIT && !(v & 3)) {
        v = (v-PE_GUI_BASE)/4;
        struct pe_gui_object *o = &pe_gui_objects[v%PE_GUI_SLOTS];
        if (o->type && o->generation == v/PE_GUI_SLOTS && (!type || o->type == type)) return o;
    }
    SetLastError(6); return NULL;
}
static struct pe_gui_object *pe_gui_new(unsigned type)
{
    for (unsigned i = 0; i < PE_GUI_SLOTS; i++) {
        struct pe_gui_object *o = &pe_gui_objects[i];
        if (!o->type && o->generation < 65535) {
            unsigned generation = o->generation+1;
            memset(o, 0, sizeof(*o)); o->generation = generation; o->type = type;
            return o;
        }
    }
    SetLastError(8); return NULL;
}
static void pe_gui_drop(struct pe_gui_object *o) { o->type = 0; }
static void pe_default_font(struct pe_logfont *font)
{
    memset(font, 0, sizeof(*font)); font->height = -12; font->weight = 400; font->charset = 1;
    const char *name = "Segoe UI";
    for (int i = 0; name[i]; i++) font->face[i] = name[i];
}
__attribute__((ms_abi)) HANDLE CreateSolidBrush(DWORD color)
{
    struct pe_gui_object *o = pe_gui_new(PE_GDI_BRUSH); if (!o) return NULL;
    o->u.brush.color = color & 0xffffff; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreatePen(int style, int width, DWORD color)
{
    if (style < 0 || style > 6 || width == INT32_MIN) { SetLastError(87); return NULL; }
    struct pe_gui_object *o = pe_gui_new(PE_GDI_PEN); if (!o) return NULL;
    o->u.pen = (struct pe_logpen){style, width < 0 ? -width : width, 0, color & 0xffffff}; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreateFontIndirectW(const struct pe_logfont *font)
{
    if (!font) { SetLastError(87); return NULL; }
    struct pe_gui_object *o = pe_gui_new(PE_GDI_FONT); if (!o) return NULL;
    o->u.font = *font; o->u.font.face[31] = 0; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreateFontW(int height, int width, int escape, int orient, int weight,
    DWORD italic, DWORD underline, DWORD strike, DWORD charset, DWORD outprec,
    DWORD clipprec, DWORD quality, DWORD pitch, const uint16_t *face)
{
    struct pe_logfont f = {height, width, escape, orient, weight,
        italic, underline, strike, charset, outprec, clipprec, quality, pitch, {0}};
    if (face) for (unsigned i = 0; i < 31 && face[i]; i++) f.face[i] = face[i];
    return CreateFontIndirectW(&f);
}
__attribute__((ms_abi)) HANDLE GetStockObject(int id)
{
    if (id < 0 || id >= 20 || id == 9) { SetLastError(87); return NULL; }
    if (pe_stocks[id]) return pe_stocks[id];
    HANDLE h = NULL;
    if (id <= 5 || id == 18) {
        static const DWORD colors[6] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0, 0};
        h = CreateSolidBrush(id <= 5 ? colors[id] : 0xffffff);
        if (h && id == 5) pe_gui_lookup(h, PE_GDI_BRUSH)->u.brush.style = 1;
    } else if (id <= 8 || id == 19) h = CreatePen(id == 8 ? 5 : 0, 1, id == 6 ? 0xffffff : 0);
    else if (id == 15) {
        struct pe_gui_object *o = pe_gui_new(PE_GDI_PALETTE); if (o) h = pe_gui_handle(o);
    } else { struct pe_logfont f; pe_default_font(&f); h = CreateFontIndirectW(&f); }
    if (h) { pe_gui_lookup(h, 0)->shared = 1; pe_stocks[id] = h; }
    return h;
}
static struct pe_gui_object *pe_dc_lookup(HANDLE h)
{
    struct pe_gui_object *o = pe_gui_lookup(h, 0);
    if (o && (o->type == PE_GDI_DC || o->type == PE_GDI_MEMDC)) return o;
    SetLastError(6); return NULL;
}
static HANDLE pe_new_dc(unsigned type, HANDLE owner)
{
    struct pe_gui_object *o = pe_gui_new(type); if (!o) return NULL;
    HANDLE h = pe_gui_handle(o);
    o->u.dc.pen = GetStockObject(7); o->u.dc.brush = GetStockObject(0);
    o->u.dc.font = GetStockObject(13); o->u.dc.palette = GetStockObject(15);
    struct pe_gui_object *b = pe_gui_new(PE_GDI_BITMAP);
    if (!o->u.dc.pen || !o->u.dc.brush || !o->u.dc.font || !o->u.dc.palette || !b) { pe_gui_drop(o); return NULL; }
    b->shared = 1; b->owner = h; b->u.bitmap = (struct pe_bitmap){0, 1, 1, 2, 1, 1, NULL};
    o->u.dc.bitmap = pe_gui_handle(b); o->u.dc.owner = owner;
    HANDLE initial[] = {o->u.dc.pen, o->u.dc.brush, o->u.dc.font, o->u.dc.bitmap, o->u.dc.palette};
    for (unsigned i = 0; i < 5; i++) pe_gui_lookup(initial[i], 0)->selected++;
    return h;
}
__attribute__((ms_abi)) HANDLE CreateCompatibleDC(HANDLE source)
{ if (source && !pe_dc_lookup(source)) return NULL; return pe_new_dc(PE_GDI_MEMDC, NULL); }
__attribute__((ms_abi)) BOOL DeleteObject(HANDLE h)
{
    struct pe_gui_object *o = pe_gui_lookup(h, 0); if (!o) return 0;
    if (o->type != PE_GDI_PEN && o->type != PE_GDI_BRUSH && o->type != PE_GDI_FONT &&
        o->type != PE_GDI_BITMAP && o->type != PE_GDI_PALETTE && o->type != PE_GDI_REGION) { SetLastError(6); return 0; }
    if (o->shared) return 1;
    if (o->selected) { SetLastError(170); return 0; }
    pe_gui_drop(o); return 1;
}
static void pe_drop_dc(struct pe_gui_object *o)
{
    HANDLE handles[] = {o->u.dc.pen, o->u.dc.brush, o->u.dc.font, o->u.dc.bitmap, o->u.dc.palette};
    for (unsigned i = 0; i < 5; i++) {
        struct pe_gui_object *s = pe_gui_lookup(handles[i], 0); if (s) s->selected--;
    }
    /* The private monochrome default bitmap is owned by this DC. */
    for (unsigned i = 0; i < PE_GUI_SLOTS; i++) {
        struct pe_gui_object *b = &pe_gui_objects[i];
        if (b->type == PE_GDI_BITMAP && b->shared && b->owner == pe_gui_handle(o)) pe_gui_drop(b);
    }
    pe_gui_drop(o);
}
__attribute__((ms_abi)) BOOL DeleteDC(HANDLE h)
{ struct pe_gui_object *o = pe_gui_lookup(h, PE_GDI_MEMDC); if (!o) return 0; pe_drop_dc(o); return 1; }
__attribute__((ms_abi)) HANDLE SelectObject(HANDLE dc, HANDLE h)
{
    struct pe_gui_object *d = pe_dc_lookup(dc), *o = pe_gui_lookup(h, 0);
    if (!d || !o) return NULL;
    HANDLE *slot;
    switch (o->type) {
    case PE_GDI_PEN: slot = &d->u.dc.pen; break;
    case PE_GDI_BRUSH: slot = &d->u.dc.brush; break;
    case PE_GDI_FONT: slot = &d->u.dc.font; break;
    case PE_GDI_BITMAP:
        if (d->type != PE_GDI_MEMDC || (o->owner && o->owner != dc) || (o->selected && d->u.dc.bitmap != h)) { SetLastError(87); return NULL; }
        slot = &d->u.dc.bitmap; break;
    default: SetLastError(87); return NULL;
    }
    HANDLE old = *slot;
    pe_gui_lookup(old, 0)->selected--; *slot = h; o->selected++; return old;
}
__attribute__((ms_abi)) DWORD GetObjectType(HANDLE h)
{ struct pe_gui_object *o = pe_gui_lookup(h, 0);
    if (!o || o->type > 14) { SetLastError(6); return 0; }
    return o->type; }
__attribute__((ms_abi)) HANDLE GetCurrentObject(HANDLE dc, unsigned type)
{
    struct pe_gui_object *d = pe_dc_lookup(dc); if (!d) return NULL;
    switch (type) {
    case PE_GDI_PEN: return d->u.dc.pen; case PE_GDI_BRUSH: return d->u.dc.brush;
    case PE_GDI_FONT: return d->u.dc.font; case PE_GDI_BITMAP: return d->u.dc.bitmap;
    case PE_GDI_PALETTE: return d->u.dc.palette; default: SetLastError(87); return NULL;
    }
}
__attribute__((ms_abi)) int GetObjectW(HANDLE h, int bytes, void *out)
{
    struct pe_gui_object *o = pe_gui_lookup(h, 0); if (!o) return 0;
    const void *src; unsigned size;
    static const uint16_t palette_entries = 20;
    switch (o->type) {
    case PE_GDI_BRUSH: src = &o->u.brush; size = sizeof(o->u.brush); break;
    case PE_GDI_PEN: src = &o->u.pen; size = sizeof(o->u.pen); break;
    case PE_GDI_FONT: src = &o->u.font; size = sizeof(o->u.font); break;
    case PE_GDI_BITMAP: src = &o->u.bitmap; size = sizeof(o->u.bitmap); break;
    case PE_GDI_PALETTE: src = &palette_entries; size = sizeof(palette_entries); break;
    default: SetLastError(87); return 0;
    }
    if (!out) return size;
    if (bytes < 0) { SetLastError(87); return 0; }
    if ((unsigned)bytes < size) size = bytes;
    memcpy(out, src, size); return size;
}
__attribute__((ms_abi)) int GetDeviceCaps(HANDLE dc, int index)
{
    if (!pe_dc_lookup(dc)) return 0;
    switch (index) {
    case 0: return 0x100; case 2: return 1; /* version, raster display */
    case 4: return 508; case 6: return 286; /* physical mm at 96 DPI */
    case 8: case 118: return 1920; case 10: case 117: return 1080;
    case 12: return 32; case 14: return 1; case 24: return -1;
    case 88: case 90: return 96;
    default: return 0; /* unsupported capabilities */
    }
}

/* Read-only headless desktop profile. No HWND is manufactured here. */
__attribute__((ms_abi)) HANDLE GetDC(HANDLE window)
{
    if (window && !pe_gui_lookup(window, PE_GUI_WINDOW)) return NULL;
    return pe_new_dc(PE_GDI_DC, window);
}
__attribute__((ms_abi)) int ReleaseDC(HANDLE window, HANDLE dc)
{
    struct pe_gui_object *o = pe_gui_lookup(dc, PE_GDI_DC);
    if (!o) return 0;
    if (o->u.dc.owner != window) { SetLastError(6); return 0; }
    pe_drop_dc(o); return 1;
}
__attribute__((ms_abi)) int GetSystemMetrics(int index)
{
    switch (index) {
    case 0: case 16: case 78: return 1920;
    case 1: case 79: return 1080;
    case 17: return 1017;
    case 2: case 3: case 9: case 10: case 20: case 21: case 30: case 31: return 17;
    case 4: return 23; case 15: case 51: case 54: case 55: return 19;
    case 5: case 6: case 83: case 84: return 1;
    case 7: case 8: case 32: case 33: case 36: case 37: case 68: case 69: case 92: return 4;
    case 11: case 12: case 13: case 14: return 32;
    case 19: case 44: case 63: case 75: case 80: case 81: return 1;
    case 28: case 34: return 112; case 29: case 35: return 27;
    case 38: case 39: return 75; case 43: return 3;
    case 45: case 46: return 2;
    case 49: case 50: case 52: case 53: return 16;
    case 59: case 61: return 1928; case 60: case 62: return 1088;
    case 71: case 72: return 13;
    default: return 0; /* absent features/unsupported metric */
    }
}
__attribute__((ms_abi)) DWORD GetSysColor(int index)
{
    /* COLORREF (BGR), deterministic light desktop colors, not dark-mode policy. */
    static const DWORD colors[31] = {
        0xc8c8c8, 0, 0xd77800, 0xbfcddb, 0xf0f0f0, 0xffffff, 0x646464,
        0, 0, 0xffffff, 0xb4b4b4, 0xfcf7f4, 0xababab, 0xd77800, 0xffffff,
        0xf0f0f0, 0xa0a0a0, 0x6d6d6d, 0, 0, 0xffffff, 0x696969,
        0xe3e3e3, 0, 0xffffe1, 0, 0xcc6600, 0xead1b9, 0xf2e4d7,
        0xd77800, 0xf0f0f0
    };
    if (index < 0 || index >= 31) { SetLastError(87); return 0; }
    return colors[index];
}
struct pe_nonclient {
    uint32_t size; int32_t border, scroll_width, scroll_height, caption_width, caption_height;
    struct pe_logfont caption_font;
    int32_t small_caption_width, small_caption_height; struct pe_logfont small_caption_font;
    int32_t menu_width, menu_height; struct pe_logfont menu_font, status_font, message_font;
    int32_t padded_border;
};
_Static_assert(sizeof(struct pe_nonclient) == 504, "NONCLIENTMETRICSW layout");
__attribute__((ms_abi)) BOOL SystemParametersInfoW(unsigned action, unsigned param, void *out, unsigned flags)
{
    (void)flags;
    if (!out) { SetLastError(87); return 0; }
    switch (action) {
    case 0x29: {
        uint32_t size = *(uint32_t *)out;
        if ((size != 500 && size != sizeof(struct pe_nonclient)) || param != size) { SetLastError(87); return 0; }
        struct pe_nonclient m = {0}; m.size = size;
        m.border = 1; m.scroll_width = m.scroll_height = 17;
        m.caption_width = 18; m.caption_height = 23;
        m.small_caption_width = m.small_caption_height = 19; m.menu_width = m.menu_height = 19;
        m.padded_border = 4;
        pe_default_font(&m.caption_font); m.caption_font.weight = 700;
        pe_default_font(&m.small_caption_font); pe_default_font(&m.menu_font);
        pe_default_font(&m.status_font); pe_default_font(&m.message_font);
        memcpy(out, &m, size); return 1;
    }
    case 0x1f:
        if (param != sizeof(struct pe_logfont)) { SetLastError(87); return 0; }
        pe_default_font(out); return 1;
    case 0x2d: { /* ICONMETRICSW */
        struct { uint32_t size; int32_t x, y, wrap; struct pe_logfont font; } m = {108, 75, 75, 1, {0}};
        if (*(DWORD *)out != sizeof(m) || param != sizeof(m)) { SetLastError(87); return 0; }
        pe_default_font(&m.font); memcpy(out, &m, sizeof(m)); return 1;
    }
    case 0x30: { int32_t rect[4] = {0, 0, 1920, 1040}; memcpy(out, rect, sizeof(rect)); return 1; }
    case 0x42: { /* HIGHCONTRASTW */
        struct { uint32_t size, flags; const uint16_t *scheme; } m = {16, 0, NULL};
        if (*(DWORD *)out != sizeof(m)) { SetLastError(87); return 0; }
        memcpy(out, &m, sizeof(m)); return 1;
    }
    case 0x48: /* ANIMATIONINFO */
        if (*(DWORD *)out != 8) { SetLastError(87); return 0; }
        ((DWORD *)out)[1] = 0; return 1;
    case 0x68: case 0x6c: *(DWORD *)out = 3; return 1;
    case 0x200a: *(DWORD *)out = 2; return 1;
    case 0x200c: *(DWORD *)out = 1400; return 1;
    case 0x2012: case 0x4a: case 0x1022: case 0x100a: *(DWORD *)out = 1; return 1;
    case 0x1002: case 0x103e: *(DWORD *)out = 0; return 1;
    default: SetLastError(50); return 0; /* no silent success for unhandled SPI */
    }
}
struct pe_wndclassex {
    uint32_t size, style; void *wndproc; int32_t class_extra, window_extra;
    HANDLE instance, icon, cursor, background;
    const uint16_t *menu_name, *class_name; HANDLE small_icon;
};
struct pe_gui_class {
    uint16_t atom; uint16_t name[256], menu[256];
    struct pe_wndclassex data; void *extra;
};
static struct pe_gui_class pe_gui_classes[64];
_Static_assert(sizeof(struct pe_wndclassex) == 80, "WNDCLASSEXW layout");
static bool pe_copy_name(uint16_t *out, unsigned cap, const uint16_t *src)
{
    for (unsigned i = 0; i < cap; i++) { out[i] = src[i]; if (!src[i]) return true; }
    SetLastError(87); return false;
}
static bool pe_name_equal(const uint16_t *a, const uint16_t *b)
{
    while (*a || *b) {
        unsigned x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}
__attribute__((ms_abi)) uint16_t RegisterClassExW(const struct pe_wndclassex *c)
{
    if (!c || c->size != sizeof(*c) || (uintptr_t)c->class_name < 65536 || !*c->class_name || !c->wndproc ||
        c->class_extra < 0 || c->window_extra < 0 || c->class_extra > 4096 || c->window_extra > 4096) { SetLastError(87); return 0; }
    struct pe_gui_class *free = NULL;
    for (unsigned i = 0; i < 64; i++) {
        struct pe_gui_class *v = &pe_gui_classes[i];
        if (!v->atom) { if (!free) free = v; }
        else if (v->data.instance == c->instance && pe_name_equal(v->name, c->class_name)) { SetLastError(1410); return 0; }
    }
    if (c->icon && !pe_gui_lookup(c->icon, PE_GUI_ICON)) return 0;
    if (c->small_icon && !pe_gui_lookup(c->small_icon, PE_GUI_ICON)) return 0;
    if (c->cursor && !pe_gui_lookup(c->cursor, PE_GUI_CURSOR)) return 0;
    if ((uintptr_t)c->background > 31 && !pe_gui_lookup(c->background, PE_GDI_BRUSH)) return 0;
    if (!free) { SetLastError(8); return 0; }
    if (!pe_copy_name(free->name, 256, c->class_name)) return 0;
    if ((uintptr_t)c->menu_name >= 65536 && !pe_copy_name(free->menu, 256, c->menu_name)) return 0;
    free->extra = c->class_extra ? pe_heap_alloc(c->class_extra, PE_HEAP_ZERO) : NULL;
    if (c->class_extra && !free->extra) { SetLastError(8); return 0; }
    free->data = *c; free->data.class_name = free->name;
    if ((uintptr_t)c->menu_name >= 65536) free->data.menu_name = free->menu;
    free->atom = 0xc000+(free-pe_gui_classes); return free->atom;
}
static struct pe_gui_class *pe_class_lookup(HANDLE instance, const uint16_t *name)
{
    for (unsigned i = 0; i < 64; i++) {
        struct pe_gui_class *v = &pe_gui_classes[i];
        if (v->atom && v->data.instance == instance &&
            ((uintptr_t)name < 65536 ? (uintptr_t)name == v->atom : pe_name_equal(v->name, name))) return v;
    }
    SetLastError(1411); return NULL;
}
__attribute__((ms_abi)) BOOL GetClassInfoExW(HANDLE instance, const uint16_t *name, struct pe_wndclassex *out)
{
    if (!name || !out || out->size != sizeof(*out)) { SetLastError(87); return 0; }
    struct pe_gui_class *v = pe_class_lookup(instance, name); if (!v) return 0;
    *out = v->data; return 1;
}
__attribute__((ms_abi)) BOOL UnregisterClassW(const uint16_t *name, HANDLE instance)
{
    if (!name) { SetLastError(87); return 0; }
    struct pe_gui_class *v = pe_class_lookup(instance, name); if (!v) return 0;
    pe_heap_free(v->extra); memset(v, 0, sizeof(*v)); return 1;
}
__attribute__((ms_abi)) HANDLE LoadImageW(HANDLE instance, const uint16_t *name, unsigned type, int width, int height, unsigned flags)
{
    if (!name || type > 2 || width < 0 || width > INT32_MAX/4 || height < 0) { SetLastError(87); return NULL; }
    if (flags & 0x10) { SetLastError(50); return NULL; } /* LR_LOADFROMFILE needs actual file/resource decoding */
    unsigned tag = type == 0 ? PE_GDI_BITMAP : type == 1 ? PE_GUI_ICON : PE_GUI_CURSOR;
    uint16_t copy[64] = {0};
    if ((uintptr_t)name >= 65536 && !pe_copy_name(copy, 64, name)) return NULL;
    if (flags & 0x8000) for (unsigned i = 0; i < PE_GUI_SLOTS; i++) {
        struct pe_gui_object *s = &pe_gui_objects[i];
        if (s->type == tag && s->shared && s->image.module == (uintptr_t)instance &&
            s->image.width == (width ? width : 32) && s->image.height == (height ? height : 32) &&
            s->image.flags == flags && ((uintptr_t)name < 65536 ? s->image.resource == (uintptr_t)name :
                !s->image.resource && pe_name_equal(s->image.name, copy))) return pe_gui_handle(s);
    }
    struct pe_gui_object *o = pe_gui_new(tag); if (!o) return NULL;
    o->shared = !!(flags & 0x8000);
    if (tag == PE_GDI_BITMAP) o->u.bitmap = (struct pe_bitmap){0, width ? width : 32, height ? height : 32, (width ? width : 32)*4, 1, 32, NULL};
    {
        o->image.module = (uintptr_t)instance; o->image.resource = (uintptr_t)name < 65536 ? (uintptr_t)name : 0;
        o->image.flags = flags; o->image.width = width ? width : 32; o->image.height = height ? height : 32;
        memcpy(o->image.name, copy, sizeof(copy));
    }
    return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE LoadCursorW(HANDLE instance, const uint16_t *name)
{ return LoadImageW(instance, name, 2, 0, 0, 0x8040); }
__attribute__((ms_abi)) HANDLE LoadIconW(HANDLE instance, const uint16_t *name)
{ return LoadImageW(instance, name, 1, 0, 0, 0x8040); }
__attribute__((ms_abi)) BOOL DestroyIcon(HANDLE h)
{
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_ICON); if (!o) return 0;
    if (!o->shared) pe_gui_drop(o);
    return 1;
}
__attribute__((ms_abi)) BOOL DestroyCursor(HANDLE h)
{
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_CURSOR); if (!o) return 0;
    if (!o->shared) pe_gui_drop(o);
    return 1;
}

static void *lookup_shim(const char *dll, const char *function, uint16_t ordinal,
		        bool by_ordinal)
{
    if (!by_ordinal && ascii_equal_ci(function, "GetDC")) return GetDC;
    if (!by_ordinal && ascii_equal_ci(function, "ReleaseDC")) return ReleaseDC;
    if (!by_ordinal && ascii_equal_ci(function, "GetSystemMetrics")) return GetSystemMetrics;
    if (!by_ordinal && ascii_equal_ci(function, "GetSysColor")) return GetSysColor;
    if (!by_ordinal && ascii_equal_ci(function, "SystemParametersInfoW")) return SystemParametersInfoW;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterClassExW")) return RegisterClassExW;
    if (!by_ordinal && ascii_equal_ci(function, "GetClassInfoExW")) return GetClassInfoExW;
    if (!by_ordinal && ascii_equal_ci(function, "UnregisterClassW")) return UnregisterClassW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadImageW")) return LoadImageW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadCursorW")) return LoadCursorW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadIconW")) return LoadIconW;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyIcon")) return DestroyIcon;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyCursor")) return DestroyCursor;
    if (!by_ordinal && ascii_equal_ci(function, "CreateSolidBrush")) return CreateSolidBrush;
    if (!by_ordinal && ascii_equal_ci(function, "CreatePen")) return CreatePen;
    if (!by_ordinal && ascii_equal_ci(function, "CreateFontIndirectW")) return CreateFontIndirectW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateFontW")) return CreateFontW;
    if (!by_ordinal && ascii_equal_ci(function, "GetStockObject")) return GetStockObject;
    if (!by_ordinal && ascii_equal_ci(function, "CreateCompatibleDC")) return CreateCompatibleDC;
    if (!by_ordinal && ascii_equal_ci(function, "DeleteObject")) return DeleteObject;
    if (!by_ordinal && ascii_equal_ci(function, "DeleteDC")) return DeleteDC;
    if (!by_ordinal && ascii_equal_ci(function, "SelectObject")) return SelectObject;
    if (!by_ordinal && ascii_equal_ci(function, "GetObjectType")) return GetObjectType;
    if (!by_ordinal && ascii_equal_ci(function, "GetCurrentObject")) return GetCurrentObject;
    if (!by_ordinal && ascii_equal_ci(function, "GetObjectW")) return GetObjectW;
    if (!by_ordinal && ascii_equal_ci(function, "GetDeviceCaps")) return GetDeviceCaps;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileType")) return GetFileType;
    if (!by_ordinal && ascii_equal_ci(function, "GetACP")) return GetACP;
    if (!by_ordinal && ascii_equal_ci(function, "GetOEMCP")) return GetOEMCP;
    if (!by_ordinal && ascii_equal_ci(function, "GetCPInfo")) return GetCPInfo;
    if (!by_ordinal && ascii_equal_ci(function, "lstrlenA")) return lstrlenA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrlenW")) return lstrlenW;
    if (!by_ordinal && ascii_equal_ci(function, "Sleep")) return Sleep;
    if (!by_ordinal && ascii_equal_ci(function, "GetTickCount64")) return GetTickCount64;
    if (!by_ordinal && ascii_equal_ci(function, "GetTickCount")) return GetTickCount;
    if (!by_ordinal && ascii_equal_ci(function, "IsValidCodePage")) return IsValidCodePage;
    if (!by_ordinal && ascii_equal_ci(function, "MultiByteToWideChar")) return MultiByteToWideChar;
    if (!by_ordinal && ascii_equal_ci(function, "WideCharToMultiByte")) return WideCharToMultiByte;
    if (!by_ordinal && ascii_equal_ci(function, "SetHandleCount")) return SetHandleCount;
    if (!by_ordinal && ascii_equal_ci(function, "GetCPInfoExW")) return GetCPInfoExW;
    if (!by_ordinal && ascii_equal_ci(function, "AreFileApisANSI")) return AreFileApisANSI;
    if (!by_ordinal && ascii_equal_ci(function, "GetConsoleCP")) return GetConsoleCP;
    if (!by_ordinal && ascii_equal_ci(function, "GetConsoleOutputCP")) return GetConsoleOutputCP;
    if (!by_ordinal && ascii_equal_ci(function, "GetModuleFileNameW")) return GetModuleFileNameW;
    if (!by_ordinal && ascii_equal_ci(function, "GetModuleFileNameA")) return GetModuleFileNameA;
    if (!by_ordinal && ascii_equal_ci(function, "GetCurrentDirectoryW")) return GetCurrentDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "GetCurrentDirectoryA")) return GetCurrentDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "GetEnvironmentVariableW")) return GetEnvironmentVariableW;
    if (!by_ordinal && ascii_equal_ci(function, "ExpandEnvironmentStringsW")) return ExpandEnvironmentStringsW;
    if (!by_ordinal && ascii_equal_ci(function, "OutputDebugStringA")) return OutputDebugStringA;
    if (!by_ordinal && ascii_equal_ci(function, "OutputDebugStringW")) return OutputDebugStringW;
    if (!by_ordinal && ascii_equal_ci(function, "RtlPcToFileHeader")) return RtlPcToFileHeader;
	(void)dll;
	if (!by_ordinal && ascii_equal_ci(function, "GetStdHandle"))
		return GetStdHandle;
	if (!by_ordinal && ascii_equal_ci(function, "WriteFile"))
		return WriteFile;
	if (!by_ordinal && ascii_equal_ci(function, "ExitProcess"))
		return ExitProcess;
	if (!by_ordinal && ascii_equal_ci(function, "GetSystemTimeAsFileTime"))
		return GetSystemTimeAsFileTime;
	if (!by_ordinal && ascii_equal_ci(function, "QueryPerformanceCounter"))
		return QueryPerformanceCounter;
	if (!by_ordinal && ascii_equal_ci(function, "QueryPerformanceFrequency"))
		return QueryPerformanceFrequency;
	if (!by_ordinal && ascii_equal_ci(function, "GetCurrentProcessId"))
		return GetCurrentProcessId;
	if (!by_ordinal && ascii_equal_ci(function, "GetCurrentThreadId"))
		return GetCurrentThreadId;
	if (!by_ordinal && ascii_equal_ci(function, "SetLastError"))
		return SetLastError;
	if (!by_ordinal && ascii_equal_ci(function, "GetLastError"))
		return GetLastError;
	if (!by_ordinal && ascii_equal_ci(function, "LoadLibraryExW"))
		return LoadLibraryExW;
	if (!by_ordinal && ascii_equal_ci(function, "GetModuleHandleW"))
		return GetModuleHandleW;
	if (!by_ordinal && ascii_equal_ci(function, "GetProcAddress"))
		return GetProcAddress;
	if (!by_ordinal && ascii_equal_ci(function, "InitializeCriticalSection"))
		return InitializeCriticalSection;
	if (!by_ordinal && ascii_equal_ci(function, "InitializeCriticalSectionAndSpinCount"))
		return InitializeCriticalSectionAndSpinCount;
	if (!by_ordinal && ascii_equal_ci(function, "InitializeCriticalSectionEx"))
		return InitializeCriticalSectionEx;
	if (!by_ordinal && ascii_equal_ci(function, "EnterCriticalSection"))
		return EnterCriticalSection;
	if (!by_ordinal && ascii_equal_ci(function, "TryEnterCriticalSection"))
		return TryEnterCriticalSection;
	if (!by_ordinal && ascii_equal_ci(function, "LeaveCriticalSection"))
		return LeaveCriticalSection;
	if (!by_ordinal && ascii_equal_ci(function, "DeleteCriticalSection"))
		return DeleteCriticalSection;
	if (!by_ordinal && ascii_equal_ci(function, "TlsAlloc"))
		return TlsAlloc;
	if (!by_ordinal && ascii_equal_ci(function, "TlsFree"))
		return TlsFree;
	if (!by_ordinal && ascii_equal_ci(function, "TlsGetValue"))
		return TlsGetValue;
	if (!by_ordinal && ascii_equal_ci(function, "TlsSetValue"))
		return TlsSetValue;
	if (!by_ordinal && ascii_equal_ci(function, "FlsAlloc"))
		return FlsAlloc;
	if (!by_ordinal && ascii_equal_ci(function, "FlsFree"))
		return FlsFree;
	if (!by_ordinal && ascii_equal_ci(function, "FlsGetValue"))
		return FlsGetValue;
	if (!by_ordinal && ascii_equal_ci(function, "FlsSetValue"))
		return FlsSetValue;
	if (!by_ordinal && ascii_equal_ci(function, "VirtualProtect"))
		return VirtualProtect;
	if (!by_ordinal && ascii_equal_ci(function, "GetProcessHeap"))
		return GetProcessHeap;
	if (!by_ordinal && ascii_equal_ci(function, "HeapCreate"))
		return HeapCreate;
	if (!by_ordinal && ascii_equal_ci(function, "HeapDestroy"))
		return HeapDestroy;
	if (!by_ordinal && ascii_equal_ci(function, "HeapAlloc"))
		return HeapAlloc;
	if (!by_ordinal && ascii_equal_ci(function, "HeapFree"))
		return HeapFree;
	if (!by_ordinal && ascii_equal_ci(function, "HeapSize"))
		return HeapSize;
	if (!by_ordinal && ascii_equal_ci(function, "HeapReAlloc"))
		return HeapReAlloc;
	if (!by_ordinal && ascii_equal_ci(function, "HeapSetInformation"))
		return HeapSetInformation;
	if (!by_ordinal && ascii_equal_ci(function, "GetStartupInfoW"))
		return GetStartupInfoW;
	if (!by_ordinal && ascii_equal_ci(function, "GetStartupInfoA"))
		return GetStartupInfoA;
	if (!by_ordinal && ascii_equal_ci(function, "IsProcessorFeaturePresent"))
		return IsProcessorFeaturePresent;
	if (!by_ordinal && ascii_equal_ci(function, "IsDebuggerPresent"))
		return IsDebuggerPresent;
	if (!by_ordinal && ascii_equal_ci(function, "SetUnhandledExceptionFilter"))
		return SetUnhandledExceptionFilter;
	if (!by_ordinal && ascii_equal_ci(function, "UnhandledExceptionFilter"))
		return UnhandledExceptionFilter;
	if (!by_ordinal && ascii_equal_ci(function, "EncodePointer"))
		return EncodePointer;
	if (!by_ordinal && ascii_equal_ci(function, "DecodePointer"))
		return DecodePointer;
	if (!by_ordinal && ascii_equal_ci(function, "InitializeSListHead"))
		return InitializeSListHead;
	if (!by_ordinal && ascii_equal_ci(function, "GetCurrentProcess"))
		return GetCurrentProcess;
	if (!by_ordinal && ascii_equal_ci(function, "GetCurrentThread"))
		return GetCurrentThread;
	if (!by_ordinal && ascii_equal_ci(function, "GetCommandLineW"))
		return GetCommandLineW;
	if (!by_ordinal && ascii_equal_ci(function, "GetCommandLineA"))
		return GetCommandLineA;
	if (!by_ordinal && ascii_equal_ci(function, "GetEnvironmentStringsW"))
		return GetEnvironmentStringsW;
	if (!by_ordinal && ascii_equal_ci(function, "FreeEnvironmentStringsW"))
		return FreeEnvironmentStringsW;
	return NULL;
}

/* The shim table is code-addressed: raw mapping needs no ELF relocations. */
static void *find_shim(const char *dll, const char *function, uint16_t ordinal,
		       bool by_ordinal)
{
	void *shim = lookup_shim(dll, function, ordinal, by_ordinal);
	if (shim)
		return shim;
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

static void test_and_setup_teb_gs(void)
{
	uintptr_t gs_base = 0;
	uintptr_t val_30 = 0, val_60 = 0, val_10 = 0;
	uint64_t *tib;
	long ret;

	ret = raw_syscall3(SYS_arch_prctl, ARCH_GET_GS, (long)&gs_base, 0);
	if (ret != 0 || gs_base == 0) {
		raw_syscall3(SYS_arch_prctl, ARCH_SET_GS, 0x7ff00000, 0);
		gs_base = 0x7ff00000;
	}

	tib = (uint64_t *)gs_base;
	if (!tib[1])
		tib[1] = 0x7ffff7000000ULL; /* StackBase (+0x08) */
	if (!tib[2])
		tib[2] = 0x7ffff6800000ULL; /* StackLimit (+0x10) */
	if (!tib[6])
		tib[6] = 0x7ff00000ULL;     /* Self (+0x30) */
	if (!tib[12])
		tib[12] = 0x7ff00800ULL;   /* PEB (+0x60) */

	__asm__ volatile ("mov %%gs:0x30, %0" : "=r"(val_30));
	__asm__ volatile ("mov %%gs:0x60, %0" : "=r"(val_60));
	__asm__ volatile ("mov %%gs:0x10, %0" : "=r"(val_10));

	if (val_30 == 0) {
		raw_syscall3(SYS_arch_prctl, ARCH_SET_GS, 0x7ff00000, 0);
		__asm__ volatile ("mov %%gs:0x30, %0" : "=r"(val_30));
		__asm__ volatile ("mov %%gs:0x60, %0" : "=r"(val_60));
		__asm__ volatile ("mov %%gs:0x10, %0" : "=r"(val_10));
	}

	uint64_t *peb = (uint64_t *)val_60;
	if (peb) {
		if (!peb[2])
			peb[2] = 0x140000000ULL;           /* ImageBaseAddress, offset 0x10 */
		if (!peb[6])
			peb[6] = (uint64_t)PE_HEAP_HANDLE; /* ProcessHeap, offset 0x30 */
	}

	raw_puts("[BRIDGE] gs:30=");
	raw_hex(val_30);
	raw_puts(" gs:60=");
	raw_hex(val_60);
	raw_puts(" gs:10=");
	raw_hex(val_10);
	raw_puts("\n");
}

struct pe_tls_directory64 {
    uint64_t start, end, index, callbacks;
    uint32_t zero_fill, characteristics;
};
_Static_assert(sizeof(struct pe_tls_directory64) == 40, "IMAGE_TLS_DIRECTORY64 layout");
static __attribute__((noreturn)) void pe_loader_error(const char *message)
{
    raw_puts("[BRIDGE] loader error: "); raw_puts(message); raw_puts("\n");
    ExitProcess(127);
}
/* Compiler TLS is separate from the Tls/Fls API slot arrays above.
 * TODO: per-thread blocks for CreateThread. */
static void pe_setup_image_tls(uintptr_t base)
{
    uint32_t nt, count, dd[2]; uint16_t optional_size;
    if (!diagnostic_image_read(base+0x3c, &nt, 4) ||
        !diagnostic_image_read(base+nt+20, &optional_size, 2) || optional_size < 0x70 ||
        !diagnostic_image_read(base+nt+24+0x6c, &count, 4)) pe_loader_error("invalid TLS headers");
    if (count <= 9) return;
    if (optional_size < 0x70+10*8 ||
        !diagnostic_image_read(base+nt+24+0x70+9*8, dd, sizeof(dd))) pe_loader_error("invalid TLS directory entry");
    if (!dd[0] && !dd[1]) return;
    struct pe_tls_directory64 tls;
    if (dd[1] < sizeof(tls) || !diagnostic_image_read(base+dd[0], &tls, sizeof(tls)) ||
        tls.end < tls.start) pe_loader_error("invalid TLS directory");
    uint64_t raw_size = tls.end-tls.start;
    if ((raw_size && (tls.start < base || tls.end > pe_main_end)) ||
        tls.index < base || tls.index > pe_main_end-4) pe_loader_error("TLS address outside image");
    unsigned align_code = (tls.characteristics >> 20) & 15;
    if (align_code == 15) pe_loader_error("reserved TLS alignment");
    uint64_t align = align_code ? 1ULL << (align_code-1) : 16;
    if (align < 16) align = 16;
    if (raw_size > UINT64_MAX-tls.zero_fill-15) pe_loader_error("TLS size overflow");
    uint64_t size = (raw_size+tls.zero_fill+15) & ~15ULL;
    if (!size) size = 16;
    if (size > INT64_MAX-align) pe_loader_error("TLS allocation too large");
    uint8_t *allocation = (void *)pe_mmap_rw(size+align-1);
    void **array = (void *)pe_mmap_rw(64*sizeof(void *));
    if (!allocation || !array) pe_loader_error("TLS allocation failed");
    uint8_t *block = (void *)(((uintptr_t)allocation+align-1) & ~(align-1));
    if (raw_size && !diagnostic_image_read(tls.start, block, raw_size)) pe_loader_error("unreadable TLS template");
    memset(block+raw_size, 0, size-raw_size);
    uintptr_t teb;
    __asm__ volatile ("mov %%gs:0x30, %0" : "=r"(teb));
    array[0] = block;
    *(void ***)(teb+0x58) = array;
    *(uint32_t *)(uintptr_t)tls.index = 0;
    raw_puts("[BRIDGE] TLS: template="); raw_hex(raw_size);
    raw_puts(" zero-fill="); raw_hex(tls.zero_fill); raw_puts(" alignment="); raw_hex(align); raw_puts("\n");
    if (tls.callbacks) {
        /* Bound the callback walk by the image, including its terminator. */
        uintptr_t pos = tls.callbacks;
        for (;;) {
            uint64_t callback;
            if (!diagnostic_image_read(pos, &callback, 8)) pe_loader_error("unterminated TLS callbacks");
            if (!callback) break;
            if (callback < pe_text_start || callback >= pe_text_end) pe_loader_error("TLS callback outside .text");
            typedef void (__attribute__((ms_abi)) *tls_callback)(void *, uint32_t, void *);
            ((tls_callback)(uintptr_t)callback)((void *)base, 1, NULL);
            pos += 8;
        }
    }
}

__attribute__((used, visibility("hidden")))
void bridge_bootstrap(uintptr_t image_base, uint32_t import_rva,
		      uint32_t import_size, uintptr_t *sp)
{
	bool verbose = false;
	(void)sp;
	install_crash_handler();
    diagnostic_image_setup(image_base);
#ifdef BRIDGE_VERBOSE
	verbose = true;
#endif
	resolve_imports(image_base, import_rva, import_size, verbose);
	test_and_setup_teb_gs();
    pe_setup_image_tls(image_base);
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
