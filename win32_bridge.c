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

static void print_utf16_preview(uintptr_t ptr)
{
	raw_puts("\"");
	if (ptr >= 0x10000 && ptr <= 0x7fffffffffffULL) {
		long test = raw_syscall3(SYS_write, -1, ptr, 1);
		if (test == -9 /* -EBADF */) {
			const uint16_t *u16 = (const uint16_t *)ptr;
			int i;
			for (i = 0; i < 40; i++) {
				uint16_t c;
				char ch;
				if (((uintptr_t)&u16[i] & 0xfff) == 0) {
					if (raw_syscall3(SYS_write, -1, (long)&u16[i], 1) != -9)
						break;
				}
				c = u16[i];
				if (c == 0)
					break;
				if (c >= 0x20 && c <= 0x7e)
					ch = (char)c;
				else
					ch = '?';
				raw_write(&ch, 1);
			}
		}
	}
	raw_puts("\"");
}

static __attribute__((noreturn)) void unimplemented_dispatch(unsigned int index,
							     uintptr_t ret_addr,
							     uintptr_t rcx,
							     uintptr_t rdx,
							     uintptr_t r8,
							     uintptr_t r9)
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
	raw_puts("\n");

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

static void *lookup_shim(const char *dll, const char *function, uint16_t ordinal,
		        bool by_ordinal)
{
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
	test_and_setup_teb_gs();
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
