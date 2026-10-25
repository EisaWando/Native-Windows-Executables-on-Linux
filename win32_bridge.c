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
#include <sys/vfs.h>

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
    unsigned char *stub;
};

static unsigned char crash_stack[64 * 1024];
static struct unknown_import *unknown_imports;
static unsigned int unknown_count, unknown_capacity;
static inline long raw_syscall6(long, long, long, long, long, long, long);
static __attribute__((noreturn)) void pe_loader_error(const char *message);
void *memcpy(void *dest, const void *src, size_t n);

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

/* One short-held recursive bridge lock; never held across PE callbacks or waits.
 * Recursion permits helpers (GUI -> stock object -> heap) to share the guard. */
static uint32_t pe_state_owner, pe_state_depth;
static bool pe_state_enter(void)
{
    uint32_t tid = raw_syscall3(SYS_gettid, 0, 0, 0);
    if (__atomic_load_n(&pe_state_owner, __ATOMIC_RELAXED) == tid) { pe_state_depth++; return true; }
    for (;;) {
        for (unsigned spin = 0; spin < 128; spin++) {
            uint32_t expected = 0;
            if (__atomic_compare_exchange_n(&pe_state_owner, &expected, tid, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                pe_state_depth = 1; return true;
            }
            __asm__ volatile ("pause");
        }
        uint32_t owner = __atomic_load_n(&pe_state_owner, __ATOMIC_RELAXED);
        if (owner && raw_syscall6(SYS_futex, (long)&pe_state_owner, 128, owner, 0, 0, 0) < 0)
            raw_syscall3(SYS_sched_yield, 0, 0, 0); /* spin/yield fallback if futex denied */
    }
}
static void pe_state_leave(bool *held)
{
    if (!*held) return;
    *held = false;
    if (--pe_state_depth) return;
    __atomic_store_n(&pe_state_owner, 0, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)&pe_state_owner, 129, 1, 0, 0, 0);
}
#define PE_STATE_GUARD bool pe_guard __attribute__((cleanup(pe_state_leave))) = pe_state_enter()
static uintptr_t pe_current_teb(void)
{ uintptr_t p; __asm__ volatile ("mov %%gs:0x30,%0" : "=r"(p)); return p; }
static void *pe_raw_map(size_t bytes)
{
    long p = raw_syscall6(SYS_mmap, 0, bytes, 3, 0x22, -1, 0);
    return (unsigned long)p >= (unsigned long)-4095 ? NULL : (void *)p;
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

static bool diagnostic_read(uintptr_t addr, void *out, size_t size);

static void crash_handler(int signal, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;
	greg_t *g = uc->uc_mcontext.gregs;
	uintptr_t stack;
	int i;

	raw_puts("BRIDGE: crash tid="); raw_uint(raw_syscall3(SYS_gettid, 0, 0, 0)); raw_puts("\n");
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
		raw_puts(": "); uint64_t value;
        if (diagnostic_read(stack+i*8, &value, 8)) raw_hex(value); else raw_puts("<unreadable>");
		raw_puts("\n");
	}
	dump_maps();
	raw_syscall3(SYS_exit_group, 128 + signal, 0, 0);
}

static void install_thread_crash_handler(void *memory, size_t bytes)
{
	struct kernel_stack stack = {
		.sp = memory,
		.flags = 0,
		.size = bytes,
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
    raw_syscall4(SYS_rt_sigaction, SIGILL, (long)&action, 0, sizeof(action.mask));
    raw_syscall4(SYS_rt_sigaction, SIGBUS, (long)&action, 0, sizeof(action.mask));
}
static void install_crash_handler(void) { install_thread_crash_handler(crash_stack, sizeof(crash_stack)); }

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

/* Pin a file description while allowing other threads to close its HANDLE. */
static int pe_fd_pin(HANDLE handle)
{
    PE_STATE_GUARD; int fd = pe_handle_to_fd(handle);
    if (fd < 0) { SetLastError(6); return -1; }
    long copy = raw_syscall3(SYS_fcntl, fd, 1030 /* F_DUPFD_CLOEXEC */, 0);
    if (copy < 0) { SetLastError(copy == -9 ? 6 : 4); return -1; }
    return copy;
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

	int fd = pe_fd_pin(handle);
    if (written) *written = 0;
    if (fd < 0) { SetLastError(6); return 0; }
    if (overlapped) { raw_syscall3(SYS_close, fd, 0, 0); SetLastError(87); return 0; }
    do { result = raw_syscall3(SYS_write, fd, (long)buffer, length); } while (result == -4);
    raw_syscall3(SYS_close, fd, 0, 0);
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
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD;
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
    if (unknown_count == unknown_capacity) {
        unsigned cap = unknown_capacity ? unknown_capacity*2 : 128;
        if (cap > 65536) pe_loader_error("too many unknown imports");
        long records = raw_syscall6(SYS_mmap, 0, cap*sizeof(*unknown_imports), 3, 0x22, -1, 0);
        long code = raw_syscall6(SYS_mmap, 0, (cap-unknown_capacity)*32, 7, 0x22, -1, 0);
        if ((unsigned long)records >= (unsigned long)-4095 ||
            (unsigned long)code >= (unsigned long)-4095) pe_loader_error("unknown import allocation failed");
        struct unknown_import *items = (void *)records;
        if (unknown_capacity) {
            memcpy(items, unknown_imports, unknown_capacity*sizeof(*items));
            raw_syscall3(SYS_munmap, (long)unknown_imports, unknown_capacity*sizeof(*items), 0);
        }
        for (unsigned i = unknown_capacity; i < cap; i++) items[i].stub = (void *)(code+(i-unknown_capacity)*32);
        unknown_imports = items; unknown_capacity = cap;
    }
	index = unknown_count++;
	unknown_imports[index].dll = dll;
	unknown_imports[index].function = function;
	unknown_imports[index].ordinal = ordinal;
	unknown_imports[index].by_ordinal = by_ordinal;
	stub = unknown_imports[index].stub;
	stub[0] = 0xb8; /* mov $index, %eax */
	*(uint32_t *)&stub[1] = index;
    /* mmap may lie beyond rel32 reach of the fixed bridge. r11 is volatile. */
    stub[5] = 0x49; stub[6] = 0xbb; /* movabs $trampoline, %r11 */
    *(uint64_t *)&stub[7] = (uintptr_t)common_unimplemented_trampoline;
    stub[15] = 0x41; stub[16] = 0xff; stub[17] = 0xe3; /* jmp *%r11 */
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
__attribute__((ms_abi)) void EnterCriticalSection(struct pe_crit_sec *cs)
{
    uintptr_t tid = raw_syscall3(SYS_gettid, 0, 0, 0);
    __atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_RELAXED);
    if (__atomic_load_n(&cs->OwningThread, __ATOMIC_ACQUIRE) == (void *)tid) { cs->RecursionCount++; return; }
    for (;;) {
        void *expected = NULL;
        if (__atomic_compare_exchange_n(&cs->OwningThread, &expected, (void *)tid, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) break;
        raw_syscall6(SYS_futex, (long)&cs->OwningThread, 128, (uint32_t)(uintptr_t)expected, 0, 0, 0);
    }
    cs->RecursionCount = 1;
}
__attribute__((ms_abi)) BOOL TryEnterCriticalSection(struct pe_crit_sec *cs)
{
    uintptr_t tid = raw_syscall3(SYS_gettid, 0, 0, 0);
    void *expected = NULL;
    if (__atomic_load_n(&cs->OwningThread, __ATOMIC_ACQUIRE) != (void *)tid &&
        !__atomic_compare_exchange_n(&cs->OwningThread, &expected, (void *)tid, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
    __atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_RELAXED); cs->RecursionCount++; return 1;
}
__attribute__((ms_abi)) void LeaveCriticalSection(struct pe_crit_sec *cs)
{
    if (__atomic_load_n(&cs->OwningThread, __ATOMIC_RELAXED) != (void *)(uintptr_t)raw_syscall3(SYS_gettid, 0, 0, 0)) { SetLastError(288); return; }
    __atomic_sub_fetch(&cs->LockCount, 1, __ATOMIC_RELAXED);
    if (--cs->RecursionCount) return;
    __atomic_store_n(&cs->OwningThread, NULL, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)&cs->OwningThread, 129, 1, 0, 0, 0);
}
__attribute__((ms_abi)) void DeleteCriticalSection(struct pe_crit_sec *cs)
{ if (cs->OwningThread) { SetLastError(170); return; } pe_cs_init(cs, 0); }

#define PE_TLS_MAX 64
#define PE_FLS_MAX 128
static uint8_t pe_tls_used[PE_TLS_MAX], pe_fls_used[PE_FLS_MAX];
static uint32_t pe_tls_generation[PE_TLS_MAX], pe_fls_generation[PE_FLS_MAX];
typedef void (__attribute__((ms_abi)) *pe_fls_callback)(void *);
static pe_fls_callback pe_fls_callbacks[PE_FLS_MAX];
struct pe_fls_data {
    void *values[PE_FLS_MAX]; uint32_t generation[PE_FLS_MAX];
    struct pe_fls_data *next;
};
static struct pe_fls_data *pe_all_fls;
static struct pe_fls_data *pe_current_fls(void)
{ return *(struct pe_fls_data **)(pe_current_teb()+0x17c8); }
static bool pe_init_fls(uintptr_t teb)
{
    struct pe_fls_data *f = pe_raw_map(sizeof(*f)); if (!f) return false;
    PE_STATE_GUARD;
    f->next = pe_all_fls; pe_all_fls = f;
    *(struct pe_fls_data **)(teb+0x17c8) = f; return true;
}
__attribute__((ms_abi)) uint32_t TlsAlloc(void)
{
    PE_STATE_GUARD;
    for (unsigned i = 0; i < PE_TLS_MAX; i++) if (!pe_tls_used[i]) {
        pe_tls_used[i] = 1; pe_tls_generation[i]++;
        *(void **)(pe_current_teb()+0x1480+i*8) = NULL;
        ((uint32_t *)(pe_current_teb()+0x1800))[i] = pe_tls_generation[i]; return i;
    }
    SetLastError(8); return UINT32_MAX;
}
__attribute__((ms_abi)) BOOL TlsFree(DWORD i)
{
    PE_STATE_GUARD;
    if (i >= PE_TLS_MAX || !pe_tls_used[i]) { SetLastError(87); return 0; }
    pe_tls_used[i] = 0; *(void **)(pe_current_teb()+0x1480+i*8) = NULL; return 1;
}
static bool pe_tls_validate(DWORD i)
{
    if (i >= PE_TLS_MAX || !pe_tls_used[i]) { SetLastError(87); return false; }
    uint32_t *g = (uint32_t *)(pe_current_teb()+0x1800);
    if (g[i] != pe_tls_generation[i]) { *(void **)(pe_current_teb()+0x1480+i*8) = NULL; g[i] = pe_tls_generation[i]; }
    return true;
}
__attribute__((ms_abi)) void *TlsGetValue(DWORD i)
{
    PE_STATE_GUARD; if (!pe_tls_validate(i)) return NULL;
    SetLastError(0); return *(void **)(pe_current_teb()+0x1480+i*8);
}
__attribute__((ms_abi)) BOOL TlsSetValue(DWORD i, void *v)
{ PE_STATE_GUARD; if (!pe_tls_validate(i)) return 0; *(void **)(pe_current_teb()+0x1480+i*8) = v; return 1; }
__attribute__((ms_abi)) DWORD FlsAlloc(pe_fls_callback callback)
{
    PE_STATE_GUARD;
    for (unsigned i = 0; i < PE_FLS_MAX; i++) if (!pe_fls_used[i]) {
        pe_fls_used[i] = 1; pe_fls_generation[i]++; pe_fls_callbacks[i] = callback; return i;
    }
    SetLastError(8); return UINT32_MAX;
}
static bool pe_fls_validate(DWORD i, struct pe_fls_data *f)
{
    if (!f || i >= PE_FLS_MAX || !pe_fls_used[i]) { SetLastError(87); return false; }
    if (f->generation[i] != pe_fls_generation[i]) { f->values[i] = NULL; f->generation[i] = pe_fls_generation[i]; }
    return true;
}
__attribute__((ms_abi)) void *FlsGetValue(DWORD i)
{
    PE_STATE_GUARD; struct pe_fls_data *f = pe_current_fls();
    if (!pe_fls_validate(i, f)) return NULL;
    SetLastError(0); return f->values[i];
}
__attribute__((ms_abi)) BOOL FlsSetValue(DWORD i, void *v)
{
    PE_STATE_GUARD; struct pe_fls_data *f = pe_current_fls();
    if (!pe_fls_validate(i, f)) return 0;
    f->values[i] = v; return 1;
}
__attribute__((ms_abi)) BOOL FlsFree(DWORD i)
{
    PE_STATE_GUARD;
    if (i >= PE_FLS_MAX || !pe_fls_used[i]) { SetLastError(87); return 0; }
    unsigned n = 0;
    for (struct pe_fls_data *f = pe_all_fls; f; f = f->next) n++;
    void **values = pe_raw_map((n ? n : 1)*sizeof(void *));
    if (!values) { SetLastError(8); return 0; }
    pe_fls_callback cb = pe_fls_callbacks[i]; unsigned k = 0;
    for (struct pe_fls_data *f = pe_all_fls; f; f = f->next) {
        if (f->generation[i] == pe_fls_generation[i]) { values[k++] = f->values[i]; f->values[i] = NULL; }
    }
    pe_fls_used[i] = 0; pe_fls_callbacks[i] = NULL;
    pe_state_leave(&pe_guard);
    if (cb) for (unsigned j = 0; j < k; j++) if (values[j]) cb(values[j]);
    raw_syscall3(SYS_munmap, (long)values, (n ? n : 1)*sizeof(void *), 0); return 1;
}
static void pe_fls_destruct(void)
{
    struct pe_fls_data *f = pe_current_fls(); if (!f) return;
    /* Destructors may repopulate slots; bound reentrant destructor passes. */
    for (unsigned pass = 0; pass < 4; pass++) {
        bool called = false;
        for (unsigned i = 0; i < PE_FLS_MAX; i++) {
            pe_fls_callback cb = NULL; void *value = NULL;
            { PE_STATE_GUARD;
              if (pe_fls_used[i] && f->generation[i] == pe_fls_generation[i] && f->values[i]) {
                  cb = pe_fls_callbacks[i]; value = f->values[i]; f->values[i] = NULL;
              }
            }
            if (cb) { called = true; cb(value); }
        }
        if (!called) break;
    }
    PE_STATE_GUARD;
    struct pe_fls_data **p = &pe_all_fls;
    while (*p && *p != f) p = &(*p)->next;
    if (*p) *p = f->next;
}

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
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD; (void)h; (void)flags; if (!p) return (uint64_t)-1; return ((struct pe_blk *)p - 1)->req; }
__attribute__((ms_abi)) void *HeapReAlloc(void *h, uint32_t flags, void *p, uint64_t size)
{ PE_STATE_GUARD;
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
__attribute__((ms_abi)) void *SetUnhandledExceptionFilter(void *f) { PE_STATE_GUARD; void *old = pe_unhandled_filter; pe_unhandled_filter = f; return old; }
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
    int fd = pe_fd_pin(h);
    if (fd < 0) return 0;
    long r = raw_syscall3(SYS_fstat, fd, (long)&st, 0);
    raw_syscall3(SYS_close, fd, 0, 0);
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
    if (!ms) { raw_syscall3(SYS_sched_yield, 0, 0, 0); return; }
    if (ms == UINT32_MAX) {
        for (;;) { struct pe_timespec day = {86400, 0}; raw_syscall3(SYS_nanosleep, (long)&day, 0, 0); }
    }
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

/* Shared GDI/USER metadata; the original window backend is in gui/bridge.inc.
 * Handles encode slot + generation; exhausted generations are never reused. */
#define PE_GUI_BASE 0x50000000ULL
#define PE_GUI_MAX_SLOTS 65536
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
struct pe_window;
struct pe_menu;struct pe_accel_table;
struct pe_gui_object {
    struct pe_window *window;
    struct pe_menu *menu;struct pe_accel_table *accelerator;
    uint32_t type, generation, selected, shared, slot;
    HANDLE owner;
    struct { uintptr_t module, resource,data;uint32_t bytes; uint32_t flags; int32_t width, height; uint16_t name[64]; } image;
    union {
        struct pe_logbrush brush;
        struct pe_logpen pen;
        struct pe_logfont font;
        struct pe_bitmap bitmap;
        struct { HANDLE pen, brush, font, bitmap, palette; HANDLE owner; DWORD text_color, background; int background_mode, x,y; } dc;
        int32_t rect[4];
    } u;
};
/* Only this vector moves on doubling; entry chunks and live pointers stay put. */
static struct pe_gui_object **pe_gui_objects;
static unsigned pe_gui_capacity, pe_gui_next;
static HANDLE pe_stocks[20];
_Static_assert(sizeof(struct pe_logfont) == 92, "LOGFONTW layout");
_Static_assert(sizeof(struct pe_logbrush) == 16, "LOGBRUSH layout");
_Static_assert(sizeof(struct pe_bitmap) == 32, "BITMAP layout");
static HANDLE pe_gui_handle(struct pe_gui_object *o)
{
    return (HANDLE)(uintptr_t)(PE_GUI_BASE+4*((uint64_t)o->generation*PE_GUI_MAX_SLOTS+o->slot));
}
static struct pe_gui_object *pe_gui_lookup(HANDLE h, unsigned type)
{
    uintptr_t v = (uintptr_t)h;
    if (v >= PE_GUI_BASE && v < PE_GUI_LIMIT && !(v & 3)) {
        v = (v-PE_GUI_BASE)/4;
        unsigned slot = v%PE_GUI_MAX_SLOTS;
        if (slot < pe_gui_capacity) {
            struct pe_gui_object *o = pe_gui_objects[slot];
            if (o->type && o->generation == v/PE_GUI_MAX_SLOTS && (!type || o->type == type)) return o;
        }
    }
    SetLastError(6); return NULL;
}
static bool pe_gui_grow(void)
{
    unsigned old = pe_gui_capacity, cap = old ? old*2 : 1024;
    if (cap > PE_GUI_MAX_SLOTS) { SetLastError(8); return false; }
    struct pe_gui_object **vector = (void *)pe_mmap_rw(cap*sizeof(*vector));
    struct pe_gui_object *chunk = (void *)pe_mmap_rw((cap-old)*sizeof(*chunk));
    if (!vector || !chunk) {
        if (vector) raw_syscall3(SYS_munmap, (long)vector, cap*sizeof(*vector), 0);
        if (chunk) raw_syscall3(SYS_munmap, (long)chunk, (cap-old)*sizeof(*chunk), 0);
        SetLastError(8); return false;
    }
    if (old) memcpy(vector, pe_gui_objects, old*sizeof(*vector));
    for (unsigned i = old; i < cap; i++) { vector[i] = &chunk[i-old]; vector[i]->slot = i; }
    if (old) raw_syscall3(SYS_munmap, (long)pe_gui_objects, old*sizeof(*vector), 0);
    pe_gui_objects = vector; pe_gui_capacity = cap; pe_gui_next = old;
    return true;
}
static struct pe_gui_object *pe_gui_new(unsigned type)
{
    for (;;) {
        for (unsigned n = 0; n < pe_gui_capacity; n++) {
            unsigned i = (pe_gui_next+n)%pe_gui_capacity;
            struct pe_gui_object *o = pe_gui_objects[i];
            if (!o->type && o->generation < 1023) {
                unsigned generation = o->generation+1;
                memset(o, 0, sizeof(*o)); o->generation = generation; o->slot = i; o->type = type;
                pe_gui_next = (i+1)%pe_gui_capacity;
                return o;
            }
        }
        if (!pe_gui_grow()) return NULL;
    }
}
static void pe_gui_drop(struct pe_gui_object *o) { o->type = 0; pe_gui_next = o->slot; }
static void pe_default_font(struct pe_logfont *font)
{
    memset(font, 0, sizeof(*font)); font->height = -12; font->weight = 400; font->charset = 1;
    const char *name = "Segoe UI";
    for (int i = 0; name[i]; i++) font->face[i] = name[i];
}
__attribute__((ms_abi)) HANDLE CreateSolidBrush(DWORD color)
{ PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_new(PE_GDI_BRUSH); if (!o) return NULL;
    o->u.brush.color = color & 0xffffff; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreatePen(int style, int width, DWORD color)
{ PE_STATE_GUARD;
    if (style < 0 || style > 6 || width == INT32_MIN) { SetLastError(87); return NULL; }
    struct pe_gui_object *o = pe_gui_new(PE_GDI_PEN); if (!o) return NULL;
    o->u.pen = (struct pe_logpen){style, width < 0 ? -width : width, 0, color & 0xffffff}; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreateFontIndirectW(const struct pe_logfont *font)
{ PE_STATE_GUARD;
    if (!font) { SetLastError(87); return NULL; }
    struct pe_gui_object *o = pe_gui_new(PE_GDI_FONT); if (!o) return NULL;
    o->u.font = *font; o->u.font.face[31] = 0; return pe_gui_handle(o);
}
__attribute__((ms_abi)) HANDLE CreateFontW(int height, int width, int escape, int orient, int weight,
    DWORD italic, DWORD underline, DWORD strike, DWORD charset, DWORD outprec,
    DWORD clipprec, DWORD quality, DWORD pitch, const uint16_t *face)
{ PE_STATE_GUARD;
    struct pe_logfont f = {height, width, escape, orient, weight,
        italic, underline, strike, charset, outprec, clipprec, quality, pitch, {0}};
    if (face) for (unsigned i = 0; i < 31 && face[i]; i++) f.face[i] = face[i];
    return CreateFontIndirectW(&f);
}
__attribute__((ms_abi)) HANDLE GetStockObject(int id)
{ PE_STATE_GUARD;
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
    o->u.dc.background=0xffffff; o->u.dc.background_mode=2;
    HANDLE initial[] = {o->u.dc.pen, o->u.dc.brush, o->u.dc.font, o->u.dc.bitmap, o->u.dc.palette};
    for (unsigned i = 0; i < 5; i++) pe_gui_lookup(initial[i], 0)->selected++;
    return h;
}
__attribute__((ms_abi)) HANDLE CreateCompatibleDC(HANDLE source)
{ PE_STATE_GUARD; if (source && !pe_dc_lookup(source)) return NULL; return pe_new_dc(PE_GDI_MEMDC, NULL); }
__attribute__((ms_abi)) BOOL DeleteObject(HANDLE h)
{ PE_STATE_GUARD;
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
    for (unsigned i = 0; i < pe_gui_capacity; i++) {
        struct pe_gui_object *b = pe_gui_objects[i];
        if (b->type == PE_GDI_BITMAP && b->shared && b->owner == pe_gui_handle(o)) pe_gui_drop(b);
    }
    pe_gui_drop(o);
}
__attribute__((ms_abi)) BOOL DeleteDC(HANDLE h)
{ PE_STATE_GUARD; struct pe_gui_object *o = pe_gui_lookup(h, PE_GDI_MEMDC); if (!o) return 0; pe_drop_dc(o); return 1; }
__attribute__((ms_abi)) HANDLE SelectObject(HANDLE dc, HANDLE h)
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD; struct pe_gui_object *o = pe_gui_lookup(h, 0);
    if (!o || o->type > 14) { SetLastError(6); return 0; }
    return o->type; }
__attribute__((ms_abi)) HANDLE GetCurrentObject(HANDLE dc, unsigned type)
{ PE_STATE_GUARD;
    struct pe_gui_object *d = pe_dc_lookup(dc); if (!d) return NULL;
    switch (type) {
    case PE_GDI_PEN: return d->u.dc.pen; case PE_GDI_BRUSH: return d->u.dc.brush;
    case PE_GDI_FONT: return d->u.dc.font; case PE_GDI_BITMAP: return d->u.dc.bitmap;
    case PE_GDI_PALETTE: return d->u.dc.palette; default: SetLastError(87); return NULL;
    }
}
__attribute__((ms_abi)) int GetObjectW(HANDLE h, int bytes, void *out)
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD;
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
{ PE_STATE_GUARD;
    if (window && !pe_gui_lookup(window, PE_GUI_WINDOW)) return NULL;
    return pe_new_dc(PE_GDI_DC, window);
}
__attribute__((ms_abi)) int ReleaseDC(HANDLE window, HANDLE dc);
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
    uint16_t atom; unsigned windows,builtin; uint16_t name[256], menu[256];
    struct pe_wndclassex data; void *extra;
};
static struct pe_gui_class *pe_gui_classes;
static struct pe_gui_class *pe_builtin_class(HANDLE,const uint16_t *);
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
{ PE_STATE_GUARD;
    if (!c || c->size != sizeof(*c) || (uintptr_t)c->class_name < 65536 || !*c->class_name || !c->wndproc ||
        c->class_extra < 0 || c->window_extra < 0 || c->class_extra > 4096 || c->window_extra > 4096) { SetLastError(87); return 0; }
    if (!pe_gui_classes) pe_gui_classes = (void *)pe_mmap_rw(64*sizeof(*pe_gui_classes));
    if (!pe_gui_classes) { SetLastError(8); return 0; }
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

    if(pe_gui_classes) for (unsigned i = 0; i < 64; i++) {
        struct pe_gui_class *v = &pe_gui_classes[i];
        if (v->atom && v->data.instance == instance &&
            ((uintptr_t)name < 65536 ? (uintptr_t)name == v->atom : pe_name_equal(v->name, name))) return v;
    }
    struct pe_gui_class *builtin=pe_builtin_class(instance,name);if(builtin)return builtin;
    SetLastError(1411); return NULL;
}
__attribute__((ms_abi)) BOOL GetClassInfoExW(HANDLE instance, const uint16_t *name, struct pe_wndclassex *out)
{ PE_STATE_GUARD;
    if (!name || !out || out->size != sizeof(*out)) { SetLastError(87); return 0; }
    struct pe_gui_class *v = pe_class_lookup(instance, name); if (!v) return 0;
    *out = v->data; return 1;
}
__attribute__((ms_abi)) BOOL UnregisterClassW(const uint16_t *name, HANDLE instance)
{ PE_STATE_GUARD;
    if (!name) { SetLastError(87); return 0; }
    struct pe_gui_class *v = pe_class_lookup(instance, name); if (!v) return 0;
    if (v->windows) { SetLastError(1412); return 0; }
    pe_heap_free(v->extra); memset(v, 0, sizeof(*v)); return 1;
}
__attribute__((ms_abi)) HANDLE LoadImageW(HANDLE,const uint16_t *,unsigned,int,int,unsigned);
__attribute__((ms_abi)) HANDLE pe_headless_LoadImageW(HANDLE instance, const uint16_t *name, unsigned type, int width, int height, unsigned flags)
{ PE_STATE_GUARD;
    if (!name || type > 2 || width < 0 || width > INT32_MAX/4 || height < 0) { SetLastError(87); return NULL; }
    if (flags & 0x10) { SetLastError(50); return NULL; } /* LR_LOADFROMFILE needs actual file/resource decoding */
    unsigned tag = type == 0 ? PE_GDI_BITMAP : type == 1 ? PE_GUI_ICON : PE_GUI_CURSOR;
    uint16_t copy[64] = {0};
    if ((uintptr_t)name >= 65536 && !pe_copy_name(copy, 64, name)) return NULL;
    if (flags & 0x8000) for (unsigned i = 0; i < pe_gui_capacity; i++) {
        struct pe_gui_object *s = pe_gui_objects[i];
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
{ PE_STATE_GUARD; return LoadImageW(instance, name, 2, 0, 0, 0x8040); }
__attribute__((ms_abi)) HANDLE LoadIconW(HANDLE instance, const uint16_t *name)
{ PE_STATE_GUARD; return LoadImageW(instance, name, 1, 0, 0, 0x8040); }
__attribute__((ms_abi)) BOOL DestroyIcon(HANDLE h)
{ PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_ICON); if (!o) return 0;
    if (!o->shared) pe_gui_drop(o);
    return 1;
}
__attribute__((ms_abi)) BOOL DestroyCursor(HANDLE h)
{ PE_STATE_GUARD;
    struct pe_gui_object *o = pe_gui_lookup(h, PE_GUI_CURSOR); if (!o) return 0;
    if (!o->shared) pe_gui_drop(o);
    return 1;
}

/* Opaque SRW state: low word is -1 exclusive, >=0 shared count. Futex waits
 * avoid pretending that contended locks succeeded. No CreateThread yet. */
static BOOL pe_lock_pointer(void *p)
{ if (!p || ((uintptr_t)p & 7)) { SetLastError(87); return 0; } return 1; }
__attribute__((ms_abi)) void InitializeSRWLock(uint64_t *p)
{ if (pe_lock_pointer(p)) __atomic_store_n(p, 0, __ATOMIC_RELEASE); }
__attribute__((ms_abi)) BOOL TryAcquireSRWLockExclusive(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return 0;
    int32_t expected = 0;
    return __atomic_compare_exchange_n((int32_t *)p, &expected, -1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
__attribute__((ms_abi)) BOOL TryAcquireSRWLockShared(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return 0;
    int32_t v = __atomic_load_n((int32_t *)p, __ATOMIC_RELAXED);
    while (v >= 0 && v < INT32_MAX)
        if (__atomic_compare_exchange_n((int32_t *)p, &v, v+1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 1;
    return 0;
}
__attribute__((ms_abi)) void AcquireSRWLockExclusive(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    while (!TryAcquireSRWLockExclusive(p)) {
        int32_t v = __atomic_load_n((int32_t *)p, __ATOMIC_RELAXED);
        if (v) raw_syscall6(SYS_futex, (long)p, 128, v, 0, 0, 0);
    }
}
__attribute__((ms_abi)) void AcquireSRWLockShared(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    while (!TryAcquireSRWLockShared(p)) raw_syscall6(SYS_futex, (long)p, 128, -1, 0, 0, 0);
}
__attribute__((ms_abi)) void ReleaseSRWLockExclusive(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    __atomic_store_n((int32_t *)p, 0, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)p, 129, INT32_MAX, 0, 0, 0);
}
__attribute__((ms_abi)) void ReleaseSRWLockShared(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    if (__atomic_sub_fetch((int32_t *)p, 1, __ATOMIC_RELEASE) == 0)
        raw_syscall6(SYS_futex, (long)p, 129, INT32_MAX, 0, 0, 0);
}
__attribute__((ms_abi)) void InitializeConditionVariable(uint64_t *p)
{ if (pe_lock_pointer(p)) __atomic_store_n(p, 0, __ATOMIC_RELEASE); }
__attribute__((ms_abi)) void WakeConditionVariable(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    __atomic_add_fetch((uint32_t *)p, 1, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)p, 129, 1, 0, 0, 0);
}
__attribute__((ms_abi)) void WakeAllConditionVariable(uint64_t *p)
{
    if (!pe_lock_pointer(p)) return;
    __atomic_add_fetch((uint32_t *)p, 1, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)p, 129, INT32_MAX, 0, 0, 0);
}
__attribute__((ms_abi)) BOOL SleepConditionVariableSRW(uint64_t *cv, uint64_t *lock, DWORD timeout, DWORD flags)
{
    if (!pe_lock_pointer(cv) || !pe_lock_pointer(lock) || (flags & ~1u)) { SetLastError(87); return 0; }
    uint32_t seq = __atomic_load_n((uint32_t *)cv, __ATOMIC_ACQUIRE);
    struct pe_timespec ts = {timeout/1000, (timeout%1000)*1000000LL};
    if (flags & 1) ReleaseSRWLockShared(lock); else ReleaseSRWLockExclusive(lock);
    long r = raw_syscall6(SYS_futex, (long)cv, 128, seq, timeout == UINT32_MAX ? 0 : (long)&ts, 0, 0);
    if (flags & 1) AcquireSRWLockShared(lock); else AcquireSRWLockExclusive(lock);
    if (r == -110) { SetLastError(1460); return 0; }
    if (r < 0 && r != -11 && r != -4) { SetLastError(87); return 0; }
    return 1; /* spurious/interrupted wakes are permitted */
}

#define PE_WIN_MAJOR 10u
#define PE_WIN_MINOR 0u
#define PE_WIN_BUILD 19045u
#define PE_WIN_BUILD_STRING "19045"
/* Stable emulated Windows 10 / x64 / single-CPU process profile. */
__attribute__((ms_abi)) BOOL GetVersionExW(void *out)
{
    if (!out || (*(DWORD *)out != 276 && *(DWORD *)out != 284)) { SetLastError(87); return 0; }
    DWORD size = *(DWORD *)out;
    memset(out, 0, size); DWORD *v = out;
    v[0] = size; v[1] = PE_WIN_MAJOR; v[2] = PE_WIN_MINOR; v[3] = PE_WIN_BUILD; v[4] = 2;
    if (size == 284) ((uint8_t *)out)[282] = 1; /* VER_NT_WORKSTATION */
    return 1;
}
__attribute__((ms_abi)) BOOL GetVersionExA(void *out)
{
    if (!out || (*(DWORD *)out != 148 && *(DWORD *)out != 156)) { SetLastError(87); return 0; }
    DWORD size = *(DWORD *)out;
    memset(out, 0, size); DWORD *v = out;
    v[0] = size; v[1] = PE_WIN_MAJOR; v[2] = PE_WIN_MINOR; v[3] = PE_WIN_BUILD; v[4] = 2;
    if (size == 156) ((uint8_t *)out)[154] = 1;
    return 1;
}
__attribute__((ms_abi)) int32_t RtlGetVersion(void *out)
{ return GetVersionExW(out) ? 0 : (int32_t)0xc000000d; }
__attribute__((ms_abi)) DWORD GetVersion(void) { return (PE_WIN_BUILD << 16) | PE_WIN_MAJOR; }
struct pe_system_info {
    uint16_t arch, reserved; uint32_t page_size;
    uintptr_t min_address, max_address, active_mask;
    uint32_t processors, processor_type, granularity; uint16_t level, revision;
};
_Static_assert(sizeof(struct pe_system_info) == 48, "SYSTEM_INFO x64 layout");
__attribute__((ms_abi)) void GetSystemInfo(struct pe_system_info *out)
{
    if (!out) { SetLastError(87); return; }
    *out = (struct pe_system_info){9, 0, 4096, 0x10000, 0x7ffffffeffffULL, 1, 1, 8664, 65536, 6, 0x3a09};
}
__attribute__((ms_abi)) void GetNativeSystemInfo(struct pe_system_info *out) { GetSystemInfo(out); }
__attribute__((ms_abi)) DWORD GetCurrentProcessorNumber(void) { return 0; }
__attribute__((ms_abi)) BOOL IsWow64Process(HANDLE process, BOOL *out)
{
    if (process != (HANDLE)(intptr_t)-1) { SetLastError(6); return 0; }
    if (!out) { SetLastError(87); return 0; } *out = 0; return 1;
}
__attribute__((ms_abi)) BOOL IsWow64Process2(HANDLE process, uint16_t *machine, uint16_t *native)
{
    if (process != (HANDLE)(intptr_t)-1) { SetLastError(6); return 0; }
    if (!machine || !native) { SetLastError(87); return 0; }
    *machine = 0; *native = 0x8664; return 1;
}
__attribute__((ms_abi)) BOOL GetProcessAffinityMask(HANDLE process, uintptr_t *process_mask, uintptr_t *system_mask)
{
    if (process != (HANDLE)(intptr_t)-1) { SetLastError(6); return 0; }
    if (!process_mask || !system_mask) { SetLastError(87); return 0; }
    *process_mask = *system_mask = 1; return 1;
}

static bool pe_path_sep(uint16_t c) { return c == '\\' || c == '/'; }
static int pe_path_length(const uint16_t *p)
{
    if (!p) { SetLastError(87); return -1; }
    for (int i = 0; i < 260; i++) if (!p[i]) return i;
    SetLastError(206); return -1;
}
static unsigned pe_path_root_length(const uint16_t *p, unsigned n)
{
    if (n >= 2 && p[1] == ':') return n >= 3 && pe_path_sep(p[2]) ? 3 : 2;
    if (n && pe_path_sep(p[0])) {
        if (n > 1 && pe_path_sep(p[1])) {
            unsigned i = 2;
            while (i < n && !pe_path_sep(p[i])) i++;
            if (i < n) i++;
            while (i < n && !pe_path_sep(p[i])) i++;
            return i;
        }
        return 1;
    }
    return 0;
}
__attribute__((ms_abi)) BOOL PathRemoveFileSpecW(uint16_t *p)
{
    int n = pe_path_length(p); if (n < 0) return 0;
    unsigned root = pe_path_root_length(p, n), end = n;
    if (end <= root) return 0;
    while (end > root && !pe_path_sep(p[end-1])) end--;
    if (end > root) end--;
    p[end] = 0; return end != (unsigned)n;
}
__attribute__((ms_abi)) BOOL PathIsRelativeW(const uint16_t *p)
{ return !p || !*p || (!pe_path_sep(p[0]) && !(p[1] == ':')); }
__attribute__((ms_abi)) BOOL PathIsUNCW(const uint16_t *p)
{ return p && p[0] == '\\' && p[1] == '\\'; }
__attribute__((ms_abi)) BOOL PathIsRootW(const uint16_t *p)
{
    int n = pe_path_length(p); if (n <= 0) return 0;
    unsigned root = pe_path_root_length(p, n);
    return root && root == (unsigned)n && !(n == 2 && p[1] == ':');
}
__attribute__((ms_abi)) int PathGetDriveNumberW(const uint16_t *p)
{
    if (!p || !p[0] || p[1] != ':') return -1;
    unsigned c = p[0]; if (c >= 'a' && c <= 'z') c -= 32;
    return c >= 'A' && c <= 'Z' ? (int)(c-'A') : -1;
}
__attribute__((ms_abi)) uint16_t *PathFindFileNameW(const uint16_t *p)
{
    if (!p) { SetLastError(87); return NULL; }
    const uint16_t *last = p;
    for (; *p; p++) if (pe_path_sep(*p) || *p == ':') last = p+1;
    return (uint16_t *)last;
}
__attribute__((ms_abi)) uint16_t *PathFindExtensionW(const uint16_t *p)
{
    const uint16_t *name = PathFindFileNameW(p), *dot = NULL;
    if (!name) return NULL;
    while (*name) { if (*name == '.') dot = name; name++; }
    return (uint16_t *)(dot ? dot : name);
}
__attribute__((ms_abi)) void PathRemoveExtensionW(uint16_t *p)
{ uint16_t *dot = PathFindExtensionW(p); if (dot) *dot = 0; }
__attribute__((ms_abi)) uint16_t *PathAddBackslashW(uint16_t *p)
{
    int n = pe_path_length(p); if (n < 0) return NULL;
    if (n && !pe_path_sep(p[n-1])) {
        if (n >= 259) { SetLastError(206); return NULL; }
        p[n++] = '\\'; p[n] = 0;
    }
    return p+n;
}
__attribute__((ms_abi)) uint16_t *PathRemoveBackslashW(uint16_t *p)
{
    int n = pe_path_length(p); if (n < 0) return NULL;
    if (n && pe_path_sep(p[n-1]) && !PathIsRootW(p)) p[--n] = 0;
    return p+n;
}

/* Kernel-style synchronization objects, in a range separate from files/GDI. */
struct pe_reg_node;
enum pe_wait_kind { PE_WAIT_THREAD = 1, PE_WAIT_EVENT, PE_WAIT_MUTEX, PE_WAIT_SEMAPHORE, PE_REGKEY, PE_TOKEN };
struct pe_thread;
struct pe_wait_object {
    unsigned kind, refs; bool named; uint16_t name[256];
    struct pe_wait_object *next;
    union {
        struct pe_thread *thread;
        struct { struct pe_reg_node *node; DWORD access; } reg;
        struct { DWORD uid,access; } token;
        struct { bool manual, signaled; } event;
        struct { uint32_t owner, recursion; bool abandoned; } mutex;
        struct { int32_t count, max; } semaphore;
    } u;
};
struct pe_wait_handle { HANDLE value; struct pe_wait_object *object; struct pe_wait_handle *next; };
struct pe_thread {
    uintptr_t teb; /* offset 0 is consumed by the assembly clone child path */
    struct pe_wait_object *object;
    void *stack, *altstack, *tls_allocation, *tls_array;
    size_t stack_size, tls_size;
    DWORD (__attribute__((ms_abi)) *start)(void *);
    void *parameter; int32_t kernel_tid;
    DWORD exit_code; unsigned suspended; bool done, running_ref, main_thread;
};
_Static_assert(offsetof(struct pe_thread, teb) == 0, "clone bootstrap TEB offset");
static struct pe_wait_object *pe_wait_objects;
static struct pe_wait_handle *pe_wait_handles;
static uint32_t pe_wait_sequence, pe_next_wait_handle;
static bool pe_thread_tls_init(struct pe_thread *t);
static void pe_tls_notify(DWORD reason);
static void pe_gui_thread_cleanup(void);
__attribute__((ms_abi, noreturn)) void ExitThread(DWORD code);
static struct pe_thread *pe_current_thread(void)
{ return *(struct pe_thread **)(pe_current_teb()+0x1700); }
struct pe_waiter {
    struct pe_wait_object **objects; unsigned count; bool all, complete;
    uint32_t tid; uint64_t deadline; DWORD result; struct pe_waiter *next;
};
static struct pe_waiter *pe_pending_waiters;
static bool pe_wait_ready(struct pe_wait_object *o, uint32_t tid);
static bool pe_wait_consume(struct pe_wait_object *o, uint32_t tid);
static uint64_t pe_monotonic_ns(void);
static bool pe_wait_satisfy(struct pe_waiter *w)
{
    bool ready = w->all; unsigned first = w->count;
    for (unsigned i = 0; i < w->count; i++) {
        bool signaled = pe_wait_ready(w->objects[i], w->tid);
        if (w->all) ready &= signaled;
        else if (signaled && first == w->count) { first = i; ready = true; }
    }
    if (!ready) return false;
    w->result = w->all ? 0 : first;
    for (unsigned i = 0; i < w->count; i++) if (w->all || i == first)
        if (pe_wait_consume(w->objects[i], w->tid) && w->result < 0x80) w->result = 0x80+i;
    w->complete = true; return true;
}
static void pe_wait_changed(void)
{
    /* Satisfy registered waits atomically with signaling, before a subsequent
     * ResetEvent or a second auto-event signal can lose a granted wake. */
    uint64_t now = pe_pending_waiters ? pe_monotonic_ns() : 0;
    for (struct pe_waiter *w = pe_pending_waiters; w; w = w->next) if (!w->complete) {
        if (w->deadline != UINT64_MAX && now >= w->deadline) { w->result = 0x102; w->complete = true; }
        else pe_wait_satisfy(w);
    }
    __atomic_add_fetch(&pe_wait_sequence, 1, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)&pe_wait_sequence, 129, INT32_MAX, 0, 0, 0);
}
static struct pe_wait_object *pe_wait_lookup(HANDLE h, unsigned kind)
{
    struct pe_wait_object *o = NULL;
    if (h == (HANDLE)(intptr_t)-2) {
        struct pe_thread *t = pe_current_thread(); if (t) o = t->object;
    } else for (struct pe_wait_handle *v = pe_wait_handles; v; v = v->next)
        if (v->value == h) { o = v->object; break; }
    if (!o || (kind && o->kind != kind)) { SetLastError(6); return NULL; }
    return o;
}
static HANDLE pe_wait_handle_new(struct pe_wait_object *o)
{
    if (pe_next_wait_handle >= 0x3800000) { SetLastError(8); return NULL; }
    struct pe_wait_handle *h = pe_raw_map(sizeof(*h)); if (!h) { SetLastError(8); return NULL; }
    h->value = (HANDLE)(uintptr_t)(0x60000000ULL+4*pe_next_wait_handle++);
    h->object = o; h->next = pe_wait_handles; pe_wait_handles = h; o->refs++; return h->value;
}
static bool pe_wait_name_equal(const uint16_t *a, const uint16_t *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static struct pe_wait_object *pe_wait_new(unsigned kind, const uint16_t *name, bool *exists)
{
    *exists = false;
    if (name && !pe_copy_name((uint16_t[256]){0}, 256, name)) return NULL;
    if (name) for (struct pe_wait_object *o = pe_wait_objects; o; o = o->next) {
        if (o->named && pe_wait_name_equal(o->name, name)) {
            if (o->kind != kind) { SetLastError(6); return NULL; }
            *exists = true; SetLastError(183); return o;
        }
    }
    struct pe_wait_object *o = pe_raw_map(sizeof(*o)); if (!o) { SetLastError(8); return NULL; }
    o->kind = kind; o->named = name != NULL;
    if (name) pe_copy_name(o->name, 256, name);
    o->next = pe_wait_objects; pe_wait_objects = o; SetLastError(0); return o;
}
/* Call only under the state lock. Kernel-cleared TID is the final lifetime fence;
 * done alone cannot authorize freeing the stack on which ExitThread is running. */
static void pe_wait_reap(void)
{
    struct pe_wait_object **link = &pe_wait_objects;
    while (*link) {
        struct pe_wait_object *o = *link;
        if (o->kind == PE_WAIT_THREAD && o->u.thread && o->u.thread->running_ref &&
            o->u.thread->done && !__atomic_load_n(&o->u.thread->kernel_tid, __ATOMIC_ACQUIRE)) {
            o->u.thread->running_ref = false; o->refs--;
        }
        if (o->refs) { link = &o->next; continue; }
        *link = o->next;
        if (o->kind == PE_WAIT_THREAD && o->u.thread) {
            struct pe_thread *t = o->u.thread;
            if (!t->main_thread) {
                void *fls = *(void **)(t->teb+0x17c8);
                if (fls) raw_syscall3(SYS_munmap, (long)fls, sizeof(struct pe_fls_data), 0);
                if (t->tls_allocation) raw_syscall3(SYS_munmap, (long)t->tls_allocation, t->tls_size, 0);
                if (t->tls_array) raw_syscall3(SYS_munmap, (long)t->tls_array, 64*8, 0);
                if (t->altstack) raw_syscall3(SYS_munmap, (long)t->altstack, 65536, 0);
                if (t->stack) raw_syscall3(SYS_munmap, (long)t->stack, t->stack_size, 0);
                raw_syscall3(SYS_munmap, t->teb, 0x2000, 0);
            }
            raw_syscall3(SYS_munmap, (long)t, sizeof(*t), 0);
        }
        raw_syscall3(SYS_munmap, (long)o, sizeof(*o), 0);
    }
}
__attribute__((ms_abi)) BOOL CloseHandle(HANDLE h)
{
    PE_STATE_GUARD;
    struct pe_wait_handle **link = &pe_wait_handles;
    while (*link && (*link)->value != h) link = &(*link)->next;
    if (!*link) {
        int fd = pe_handle_to_fd(h);
        if (fd < 0) { SetLastError(6); return 0; }
        if (raw_syscall3(SYS_close, fd, 0, 0)) { SetLastError(6); return 0; }
        return 1;
    }
    struct pe_wait_handle *v = *link; struct pe_wait_object *o = v->object;
    *link = v->next; o->refs--;
    raw_syscall3(SYS_munmap, (long)v, sizeof(*v), 0);
    if (o->kind == PE_WAIT_THREAD && o->u.thread && o->u.thread->done && !o->u.thread->main_thread) {
        /* Pin during the tiny done -> kernel exit interval, without holding lock. */
        o->refs++; struct pe_thread *t = o->u.thread;
        pe_state_leave(&pe_guard);
        int32_t tid;
        while ((tid = __atomic_load_n(&t->kernel_tid, __ATOMIC_ACQUIRE)))
            raw_syscall6(SYS_futex, (long)&t->kernel_tid, 0, tid, 0, 0, 0);
        pe_guard = pe_state_enter(); o->refs--;
    }
    pe_wait_reap(); return 1;
}
static uint64_t pe_monotonic_ns(void)
{
    struct pe_timespec ts; raw_syscall3(SYS_clock_gettime, 1, (long)&ts, 0);
    return (uint64_t)ts.tv_sec*1000000000ULL+ts.tv_nsec;
}
static bool pe_wait_ready(struct pe_wait_object *o, uint32_t tid)
{
    switch (o->kind) {
    case PE_WAIT_THREAD: return o->u.thread->done;
    case PE_WAIT_EVENT: return o->u.event.signaled;
    case PE_WAIT_MUTEX: return !o->u.mutex.owner || o->u.mutex.owner == tid;
    case PE_WAIT_SEMAPHORE: return o->u.semaphore.count > 0;
    default: return false;
    }
}
static bool pe_wait_consume(struct pe_wait_object *o, uint32_t tid)
{
    bool abandoned = false;
    if (o->kind == PE_WAIT_EVENT && !o->u.event.manual) o->u.event.signaled = false;
    if (o->kind == PE_WAIT_SEMAPHORE) o->u.semaphore.count--;
    if (o->kind == PE_WAIT_MUTEX) {
        abandoned = o->u.mutex.abandoned; o->u.mutex.abandoned = false;
        o->u.mutex.owner = tid; o->u.mutex.recursion++;
    }
    return abandoned;
}
__attribute__((ms_abi)) DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL all, DWORD timeout, BOOL alertable)
{
    (void)alertable; /* APC delivery is not implemented; QueueUserAPC stays unknown. */
    if (!count || count > 64 || !handles) { SetLastError(87); return UINT32_MAX; }
    struct pe_wait_object *objects[64]; unsigned pinned = 0;
    uint64_t deadline = timeout == UINT32_MAX ? UINT64_MAX : pe_monotonic_ns()+timeout*1000000ULL;
    struct pe_waiter w = {objects, count, all != 0, false, GetCurrentThreadId(), deadline, UINT32_MAX, NULL};
    bool linked = false;
    PE_STATE_GUARD;
    for (unsigned i = 0; i < count; i++) {
        objects[i] = pe_wait_lookup(handles[i], 0); if (!objects[i]) goto out;
        if ((objects[i]->kind == PE_REGKEY || objects[i]->kind == PE_TOKEN)) { SetLastError(6); goto out; }
        for (unsigned j = 0; j < i; j++)
            if (handles[j] == handles[i] || (all && objects[j] == objects[i])) { SetLastError(87); goto out; }
        objects[i]->refs++; pinned++;
    }
    if (pe_wait_satisfy(&w)) goto out;
    w.next = pe_pending_waiters; pe_pending_waiters = &w; linked = true;
    for (;;) {
        if (w.complete) break;
        uint64_t now = pe_monotonic_ns();
        if (now >= deadline) { w.result = 0x102; break; }
        uint32_t seq = __atomic_load_n(&pe_wait_sequence, __ATOMIC_ACQUIRE);
        struct pe_timespec ts = {(deadline-now)/1000000000ULL, (deadline-now)%1000000000ULL};
        pe_state_leave(&pe_guard);
        long r = raw_syscall6(SYS_futex, (long)&pe_wait_sequence, 128, seq, deadline == UINT64_MAX ? 0 : (long)&ts, 0, 0);
        pe_guard = pe_state_enter();
        if (r < 0 && r != -11 && r != -4 && r != -110) { SetLastError(87); break; }
    }
out:
    if (linked) {
        struct pe_waiter **p = &pe_pending_waiters;
        while (*p && *p != &w) p = &(*p)->next;
        if (*p) *p = w.next;
    }
    for (unsigned i = 0; i < pinned; i++) objects[i]->refs--;
    pe_wait_reap(); return w.result;
}

__attribute__((ms_abi)) DWORD WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD timeout)
{ return WaitForMultipleObjectsEx(n, h, all, timeout, 0); }
__attribute__((ms_abi)) DWORD WaitForSingleObjectEx(HANDLE h, DWORD timeout, BOOL alertable)
{ return WaitForMultipleObjectsEx(1, &h, 0, timeout, alertable); }
__attribute__((ms_abi)) DWORD WaitForSingleObject(HANDLE h, DWORD timeout)
{ return WaitForSingleObjectEx(h, timeout, 0); }
__attribute__((ms_abi)) HANDLE CreateEventW(void *security, BOOL manual, BOOL initial, const uint16_t *name)
{
    (void)security; PE_STATE_GUARD; pe_wait_reap(); bool existing;
    struct pe_wait_object *o = pe_wait_new(PE_WAIT_EVENT, name, &existing); if (!o) return NULL;
    if (!existing) { o->u.event.manual = manual != 0; o->u.event.signaled = initial != 0; }
    HANDLE h = pe_wait_handle_new(o); if (!h) pe_wait_reap(); return h;
}
__attribute__((ms_abi)) BOOL SetEvent(HANDLE h)
{
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_EVENT); if (!o) return 0;
    o->u.event.signaled = true; pe_wait_changed(); return 1;
}
__attribute__((ms_abi)) BOOL ResetEvent(HANDLE h)
{
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_EVENT); if (!o) return 0;
    o->u.event.signaled = false; return 1;
}
__attribute__((ms_abi)) HANDLE CreateMutexW(void *security, BOOL initial_owner, const uint16_t *name)
{
    (void)security; PE_STATE_GUARD; pe_wait_reap(); bool existing;
    struct pe_wait_object *o = pe_wait_new(PE_WAIT_MUTEX, name, &existing); if (!o) return NULL;
    if (!existing && initial_owner) { o->u.mutex.owner = GetCurrentThreadId(); o->u.mutex.recursion = 1; }
    HANDLE h = pe_wait_handle_new(o); if (!h) pe_wait_reap(); return h;
}
__attribute__((ms_abi)) BOOL ReleaseMutex(HANDLE h)
{
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_MUTEX); if (!o) return 0;
    if (o->u.mutex.owner != GetCurrentThreadId()) { SetLastError(288); return 0; }
    if (!--o->u.mutex.recursion) { o->u.mutex.owner = 0; pe_wait_changed(); }
    return 1;
}
__attribute__((ms_abi)) HANDLE CreateSemaphoreW(void *security, int32_t initial, int32_t max, const uint16_t *name)
{
    (void)security;
    if (initial < 0 || max < 1 || initial > max) { SetLastError(87); return NULL; }
    PE_STATE_GUARD; pe_wait_reap(); bool existing;
    struct pe_wait_object *o = pe_wait_new(PE_WAIT_SEMAPHORE, name, &existing); if (!o) return NULL;
    if (!existing) { o->u.semaphore.count = initial; o->u.semaphore.max = max; }
    HANDLE h = pe_wait_handle_new(o); if (!h) pe_wait_reap(); return h;
}
__attribute__((ms_abi)) BOOL ReleaseSemaphore(HANDLE h, int32_t count, int32_t *previous)
{
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_SEMAPHORE); if (!o) return 0;
    if (count < 1) { SetLastError(87); return 0; }
    if (count > o->u.semaphore.max-o->u.semaphore.count) { SetLastError(298); return 0; }
    if (previous) *previous = o->u.semaphore.count;
    o->u.semaphore.count += count; pe_wait_changed(); return 1;
}
static const uint16_t *pe_wait_ansi_name(const char *name, uint16_t *out)
{ return !name ? NULL : MultiByteToWideChar(65001, 8, name, -1, out, 256) ? out : NULL; }
__attribute__((ms_abi)) HANDLE CreateEventA(void *security, BOOL manual, BOOL initial, const char *name)
{ uint16_t wide[256]; const uint16_t *p = pe_wait_ansi_name(name, wide); return name && !p ? NULL : CreateEventW(security, manual, initial, p); }
__attribute__((ms_abi)) HANDLE CreateMutexA(void *security, BOOL initial, const char *name)
{ uint16_t wide[256]; const uint16_t *p = pe_wait_ansi_name(name, wide); return name && !p ? NULL : CreateMutexW(security, initial, p); }
__attribute__((ms_abi)) HANDLE CreateSemaphoreA(void *security, int32_t initial, int32_t max, const char *name)
{ uint16_t wide[256]; const uint16_t *p = pe_wait_ansi_name(name, wide); return name && !p ? NULL : CreateSemaphoreW(security, initial, max, p); }
__attribute__((ms_abi)) DWORD SleepEx(DWORD ms, BOOL alertable)
{ (void)alertable; Sleep(ms); return 0; }
__attribute__((ms_abi)) BOOL SwitchToThread(void)
{ return raw_syscall3(SYS_sched_yield, 0, 0, 0) == 0; }
__attribute__((ms_abi)) int32_t InterlockedIncrement(int32_t *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
__attribute__((ms_abi)) int32_t InterlockedDecrement(int32_t *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
__attribute__((ms_abi)) int32_t InterlockedExchange(int32_t *p, int32_t v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
__attribute__((ms_abi)) int32_t InterlockedCompareExchange(int32_t *p, int32_t v, int32_t expected)
{ __atomic_compare_exchange_n(p, &expected, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return expected; }

/* x86-64 raw clone ABI: rdi=flags rsi=stack rdx=parent_tid r10=child_tid.
 * The child uses only assembly until its new stack and GS base are installed. */
long pe_clone(unsigned long flags, void *stack, int32_t *parent_tid, int32_t *child_tid);
static __attribute__((used, noreturn)) void pe_thread_entry(struct pe_thread *t);
__asm__(
    ".text\n.type pe_clone,@function\npe_clone:\n"
    "mov %rcx,%r10\n xor %r8d,%r8d\n mov $56,%eax\n syscall\n"
    "test %rax,%rax\n jnz 1f\n pop %r12\n"
    "mov (%r12),%rsi\n mov $0x1001,%edi\n mov $158,%eax\n syscall\n"
    "test %rax,%rax\n js 2f\n mov %r12,%rdi\n and $-16,%rsp\n call pe_thread_entry\n ud2\n"
    "1: ret\n 2: mov $127,%edi\n mov $231,%eax\n syscall\n ud2\n"
    ".size pe_clone,.-pe_clone\n"
);
static __attribute__((used, noreturn)) void pe_thread_entry(struct pe_thread *t)
{
    uintptr_t teb = t->teb;
    *(uint64_t *)(teb+0x40) = GetCurrentProcessId(); *(uint64_t *)(teb+0x48) = GetCurrentThreadId();
    install_thread_crash_handler(t->altstack, 65536);
    if (!pe_init_fls(teb)) pe_loader_error("thread FLS allocation failed");
    for (;;) {
        PE_STATE_GUARD;
        if (!t->suspended) break;
        uint32_t seq = __atomic_load_n(&pe_wait_sequence, __ATOMIC_ACQUIRE);
        pe_state_leave(&pe_guard);
        raw_syscall6(SYS_futex, (long)&pe_wait_sequence, 128, seq, 0, 0, 0);
    }
    if (!pe_thread_tls_init(t)) pe_loader_error("thread TLS allocation failed");
    pe_tls_notify(2);
    DWORD code = t->start(t->parameter);
    ExitThread(code);
}
__attribute__((ms_abi)) HANDLE CreateThread(void *security, uint64_t stack_size,
    DWORD (__attribute__((ms_abi)) *start)(void *), void *parameter, DWORD flags, DWORD *id)
{
    (void)security;
    if (!start || (flags & ~(4u|0x10000u))) { SetLastError(87); return NULL; }
    if (!stack_size) {
        uint32_t nt; uint64_t reserve;
        if (diagnostic_image_read(pe_main_base+0x3c, &nt, 4) &&
            diagnostic_image_read(pe_main_base+nt+24+0x48, &reserve, 8)) stack_size = reserve;
    }
    if (stack_size < 1024*1024) stack_size = 1024*1024;
    if (stack_size > INT64_MAX-8192) { SetLastError(8); return NULL; }
    stack_size = (stack_size+4095) & ~4095ULL;
    struct pe_thread *t = pe_raw_map(sizeof(*t)); if (!t) { SetLastError(8); return NULL; }
    t->stack_size = stack_size+4096; t->stack = pe_raw_map(t->stack_size);
    t->teb = (uintptr_t)pe_raw_map(0x2000); t->altstack = pe_raw_map(65536);
    if (!t->stack || !t->teb || !t->altstack || raw_syscall3(SYS_mprotect, (long)t->stack, 4096, 0)) goto fail;
    uintptr_t top = (uintptr_t)t->stack+t->stack_size, teb = t->teb;
    *(uintptr_t *)(teb+8) = top; *(uintptr_t *)(teb+0x10) = (uintptr_t)t->stack+4096;
    *(uintptr_t *)(teb+0x30) = teb;
    *(uintptr_t *)(teb+0x60) = *(uintptr_t *)(pe_current_teb()+0x60);
    *(struct pe_thread **)(teb+0x1700) = t;
    t->start = start; t->parameter = parameter; t->suspended = !!(flags & 4); t->kernel_tid = -1;
    *(struct pe_thread **)(top-16) = t;
    {
        PE_STATE_GUARD; pe_wait_reap(); bool exists;
        struct pe_wait_object *o = pe_wait_new(PE_WAIT_THREAD, NULL, &exists);
        if (!o) goto fail;
        t->object = o; o->u.thread = t;
        HANDLE h = pe_wait_handle_new(o);
        if (!h) { o->u.thread = NULL; pe_wait_reap(); goto fail; }
        t->running_ref = true; o->refs++;
        long tid = pe_clone(0x350f00, (void *)(top-16), &t->kernel_tid, &t->kernel_tid);
        if (tid < 0) {
            t->running_ref = false; o->refs--; o->u.thread = NULL; CloseHandle(h); goto fail;
        }
        if (id) *id = tid;
        return h;
    }
fail:
    if (t->stack) raw_syscall3(SYS_munmap, (long)t->stack, t->stack_size, 0);
    if (t->teb) raw_syscall3(SYS_munmap, t->teb, 0x2000, 0);
    if (t->altstack) raw_syscall3(SYS_munmap, (long)t->altstack, 65536, 0);
    raw_syscall3(SYS_munmap, (long)t, sizeof(*t), 0); SetLastError(8); return NULL;
}
__attribute__((ms_abi)) DWORD ResumeThread(HANDLE h)
{
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_THREAD); if (!o) return UINT32_MAX;
    unsigned previous = o->u.thread->suspended;
    if (previous) { o->u.thread->suspended--; pe_wait_changed(); } return previous;
}
__attribute__((ms_abi)) BOOL GetExitCodeThread(HANDLE h, DWORD *code)
{
    if (!code) { SetLastError(87); return 0; }
    PE_STATE_GUARD; struct pe_wait_object *o = pe_wait_lookup(h, PE_WAIT_THREAD); if (!o) return 0;
    *code = o->u.thread->done ? o->u.thread->exit_code : 259; return 1;
}
__attribute__((ms_abi, noreturn)) void ExitThread(DWORD code)
{
    struct pe_thread *t = pe_current_thread();
    pe_gui_thread_cleanup();
    pe_tls_notify(3); pe_fls_destruct();
    {
        PE_STATE_GUARD; uint32_t tid = GetCurrentThreadId();
        for (struct pe_wait_object *o = pe_wait_objects; o; o = o->next)
            if (o->kind == PE_WAIT_MUTEX && o->u.mutex.owner == tid) {
                o->u.mutex.owner = 0; o->u.mutex.recursion = 0; o->u.mutex.abandoned = true;
            }
        t->exit_code = code; t->done = true; pe_wait_changed();
    }
    raw_syscall3(SYS_exit, code, 0, 0);
    for (;;) __asm__ volatile ("ud2");
}
static void pe_main_thread_init(void)
{
    PE_STATE_GUARD; bool exists;
    struct pe_wait_object *o = pe_wait_new(PE_WAIT_THREAD, NULL, &exists);
    struct pe_thread *t = pe_raw_map(sizeof(*t)); if (!o || !t) pe_loader_error("main thread object allocation failed");
    o->u.thread = t; o->refs = 1; t->object = o; t->teb = pe_current_teb();
    t->kernel_tid = GetCurrentThreadId(); t->main_thread = true;
    *(struct pe_thread **)(t->teb+0x1700) = t;
}

static DWORD pe_file_error(long error)
{
    switch (-error) {
    case 1: case 13: case 21: return 5; case 2: return 2; case 20: return 3;
    case 9: return 6; case 12: return 8; case 14: return 998; case 17: return 80;
    case 23: case 24: return 4; case 36: return 206; case 38: return 50; default: return 31;
    }
}
static bool pe_linux_path(const uint16_t *path, char *out, unsigned cap)
{
    if (!path || !*path) { SetLastError(123); return false; }
    if (path[0] == '\\' && path[1] == '\\' && path[2] == '?' && path[3] == '\\') path += 4;
    if (path[0] == '\\' && path[1] == '\\') { SetLastError(53); return false; }
    if (path[1] == ':') {
        if (path[0] != 'Z' && path[0] != 'z') { SetLastError(15); return false; }
        if (!pe_path_sep(path[2])) { SetLastError(123); return false; }
        path += 2;
    }
    if (!WideCharToMultiByte(65001, 0x80, path, -1, out, cap, NULL, NULL)) return false;
    for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
    return true;
}
static DWORD pe_stat_attributes(const struct stat *s)
{ return (S_ISDIR(s->st_mode) ? 16u : 128u) | (!(s->st_mode & 0222) ? 1u : 0u); }
struct pe_filetime { uint32_t low, high; };
static struct pe_filetime pe_stat_time(int64_t seconds, long nanos)
{
    uint64_t value = ((uint64_t)seconds+11644473600ULL)*10000000ULL+nanos/100;
    return (struct pe_filetime){value, value >> 32};
}
struct pe_attribute_data {
    DWORD attributes; struct pe_filetime creation, access, write; DWORD size_high, size_low;
};
_Static_assert(sizeof(struct pe_attribute_data) == 36, "WIN32_FILE_ATTRIBUTE_DATA layout");
__attribute__((ms_abi)) BOOL GetFileAttributesExW(const uint16_t *path, unsigned level, struct pe_attribute_data *out)
{
    if (level || !out) { SetLastError(87); return 0; }
    char linux_path[4096]; struct stat s;
    if (!pe_linux_path(path, linux_path, sizeof(linux_path))) return 0;
    long r = raw_syscall4(SYS_newfstatat, -100, (long)linux_path, (long)&s, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; }
    out->attributes = pe_stat_attributes(&s);
    out->creation = pe_stat_time(s.st_ctim.tv_sec, s.st_ctim.tv_nsec); /* ctime approximation */
    out->access = pe_stat_time(s.st_atim.tv_sec, s.st_atim.tv_nsec);
    out->write = pe_stat_time(s.st_mtim.tv_sec, s.st_mtim.tv_nsec);
    out->size_high = (uint64_t)s.st_size >> 32; out->size_low = s.st_size; return 1;
}
__attribute__((ms_abi)) DWORD GetFileAttributesW(const uint16_t *path)
{ struct pe_attribute_data d; return GetFileAttributesExW(path, 0, &d) ? d.attributes : UINT32_MAX; }
__attribute__((ms_abi)) BOOL GetFileAttributesExA(const char *path, unsigned level, struct pe_attribute_data *out)
{
    uint16_t wide[4096];
    if (!path) { SetLastError(87); return 0; }
    if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return 0;
    return GetFileAttributesExW(wide, level, out);
}
__attribute__((ms_abi)) DWORD GetFileAttributesA(const char *path)
{ struct pe_attribute_data d; return GetFileAttributesExA(path, 0, &d) ? d.attributes : UINT32_MAX; }
__attribute__((ms_abi)) HANDLE CreateFileW(const uint16_t *path, DWORD access, DWORD share, void *security,
    DWORD disposition, DWORD attributes, HANDLE template_file)
{
    (void)share; (void)security; (void)template_file; /* sharing/inheritance not enforced yet */
    if (attributes & (0x40000000u|0x04000000u)) { SetLastError(50); return (HANDLE)(intptr_t)-1; }
    char linux_path[4096]; if (!pe_linux_path(path, linux_path, sizeof(linux_path))) return (HANDLE)(intptr_t)-1;
    int flags = (access & 0x40000000) ? ((access & 0x80000000) ? 2 : 1) : 0;
    switch (disposition) {
    case 1: flags |= 0x40|0x80; break; case 2: flags |= 0x40|0x200; break;
    case 3: break; case 4: flags |= 0x40; break;
    case 5: if (!(access & 0x40000000)) { SetLastError(87); return (HANDLE)(intptr_t)-1; } flags |= 0x200; break;
    default: SetLastError(87); return (HANDLE)(intptr_t)-1;
    }
    struct stat s; bool exists = !raw_syscall4(SYS_newfstatat, -100, (long)linux_path, (long)&s, 0);
    long fd = raw_syscall4(SYS_openat, -100, (long)linux_path, flags|0x80000, 0666);
    if (fd < 0) { SetLastError(pe_file_error(fd)); return (HANDLE)(intptr_t)-1; }
    struct stat opened;
    long status = raw_syscall3(SYS_fstat, fd, (long)&opened, 0);
    if (status < 0 || (S_ISDIR(opened.st_mode) && !(attributes & 0x02000000))) {
        raw_syscall3(SYS_close, fd, 0, 0); SetLastError(5); return (HANDLE)(intptr_t)-1;
    }
    HANDLE h = pe_fd_to_handle(fd);
    if (h == (HANDLE)(intptr_t)-1) { raw_syscall3(SYS_close, fd, 0, 0); SetLastError(4); return h; }
    SetLastError((disposition == 2 || disposition == 4) && exists ? 183 : 0); return h;
}
__attribute__((ms_abi)) HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security,
    DWORD disposition, DWORD attributes, HANDLE template_file)
{
    uint16_t wide[4096];
    if (!path) { SetLastError(87); return (HANDLE)(intptr_t)-1; }
    if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return (HANDLE)(intptr_t)-1;
    return CreateFileW(wide, access, share, security, disposition, attributes, template_file);
}
__attribute__((ms_abi)) BOOL ReadFile(HANDLE h, void *buffer, DWORD bytes, DWORD *read_bytes, void *overlapped)
{
    if (read_bytes) *read_bytes = 0;
    if (overlapped) { SetLastError(50); return 0; }
    int fd = pe_fd_pin(h); if (fd < 0) return 0;
    long r; do { r = raw_syscall3(SYS_read, fd, (long)buffer, bytes); } while (r == -4);
    raw_syscall3(SYS_close, fd, 0, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; }
    if (read_bytes) *read_bytes = r; return 1;
}
__attribute__((ms_abi)) BOOL SetFilePointerEx(HANDLE h, int64_t distance, int64_t *position, DWORD method)
{
    if (method > 2) { SetLastError(87); return 0; }
    int fd = pe_fd_pin(h); if (fd < 0) return 0;
    long r = raw_syscall3(SYS_lseek, fd, distance, method);
    raw_syscall3(SYS_close, fd, 0, 0);
    if (r < 0) { SetLastError(r == -22 ? 131 : pe_file_error(r)); return 0; }
    if (position) *position = r; return 1;
}
__attribute__((ms_abi)) BOOL GetFileSizeEx(HANDLE h, int64_t *size)
{
    if (!size) { SetLastError(87); return 0; }
    int fd = pe_fd_pin(h); if (fd < 0) return 0;
    struct stat s; long r = raw_syscall3(SYS_fstat, fd, (long)&s, 0);
    raw_syscall3(SYS_close, fd, 0, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; }
    *size = s.st_size; return 1;
}
__attribute__((ms_abi)) DWORD GetFileSize(HANDLE h, DWORD *high)
{
    int64_t size; if (!GetFileSizeEx(h, &size)) return UINT32_MAX;
    if (high) *high = (uint64_t)size >> 32; SetLastError(0); return size;
}
__attribute__((ms_abi)) BOOL DeleteFileW(const uint16_t *path)
{
    char linux_path[4096]; if (!pe_linux_path(path, linux_path, sizeof(linux_path))) return 0;
    long r = raw_syscall3(SYS_unlink, (long)linux_path, 0, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; } return 1;
}

/* Startup lstr helpers. Comparisons use ordinal ordering and ASCII case folding;
 * locale-sensitive collation is still a separate, unimplemented surface. */
#define PE_DEFINE_LSTR(suffix, type, length_fn) \
__attribute__((ms_abi)) type *lstrcpyn##suffix(type *out, const type *src, int cap) \
{ \
    if (!out || !src) { SetLastError(87); return NULL; } \
    if (cap <= 0) return out; \
    int i = 0; while (i < cap-1 && src[i]) { out[i] = src[i]; i++; } out[i] = 0; return out; \
} \
__attribute__((ms_abi)) type *lstrcpy##suffix(type *out, const type *src) \
{ \
    if (!out || !src) { SetLastError(87); return NULL; } \
    memmove(out, src, (length_fn(src)+1)*sizeof(type)); return out; \
} \
__attribute__((ms_abi)) type *lstrcat##suffix(type *out, const type *src) \
{ \
    if (!out || !src) { SetLastError(87); return NULL; } \
    memmove(out+length_fn(out), src, (length_fn(src)+1)*sizeof(type)); return out; \
} \
static int pe_lstrcmp##suffix(const type *a, const type *b, bool fold) \
{ \
    if (!a || !b) { SetLastError(87); return 0; } \
    while (*a || *b) { \
        unsigned x = sizeof(type) == 1 ? (unsigned char)*a : (uint16_t)*a; \
        unsigned y = sizeof(type) == 1 ? (unsigned char)*b : (uint16_t)*b; \
        if (fold) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; } \
        if (x != y) return x < y ? -1 : 1; a++; b++; \
    } return 0; \
} \
__attribute__((ms_abi)) int lstrcmp##suffix(const type *a, const type *b) { return pe_lstrcmp##suffix(a, b, false); } \
__attribute__((ms_abi)) int lstrcmpi##suffix(const type *a, const type *b) { return pe_lstrcmp##suffix(a, b, true); }
PE_DEFINE_LSTR(A, char, lstrlenA)
PE_DEFINE_LSTR(W, uint16_t, lstrlenW)
#undef PE_DEFINE_LSTR

struct pe_user_name { uint16_t name[256]; };
struct pe_user_names { struct pe_user_name *entries; unsigned count, capacity; };
static struct pe_user_names pe_message_names, pe_clipboard_names;
static unsigned pe_user_name_register(struct pe_user_names *table, const uint16_t *name)
{
    if (!name || !*name) { SetLastError(87); return 0; }
    uint16_t copy[256]; if (!pe_copy_name(copy, 256, name)) return 0;
    PE_STATE_GUARD;
    for (unsigned i = 0; i < table->count; i++)
        if (pe_name_equal(table->entries[i].name, copy)) return 0xc000+i;
    if (table->count == table->capacity) {
        unsigned cap = table->capacity ? table->capacity*2 : 64;
        if (cap > 0x4000) { SetLastError(8); return 0; }
        struct pe_user_name *entries = pe_raw_map(cap*sizeof(*entries));
        if (!entries) { SetLastError(8); return 0; }
        if (table->entries) {
            memcpy(entries, table->entries, table->count*sizeof(*entries));
            raw_syscall3(SYS_munmap, (long)table->entries, table->capacity*sizeof(*entries), 0);
        }
        table->entries = entries; table->capacity = cap;
    }
    pe_copy_name(table->entries[table->count].name, 256, copy); return 0xc000+table->count++;
}
__attribute__((ms_abi)) unsigned RegisterWindowMessageW(const uint16_t *name)
{ return pe_user_name_register(&pe_message_names, name); }
__attribute__((ms_abi)) unsigned RegisterWindowMessageA(const char *name)
{
    uint16_t wide[256];
    if (!name || !MultiByteToWideChar(65001, 8, name, -1, wide, 256)) return 0;
    return RegisterWindowMessageW(wide);
}
__attribute__((ms_abi)) unsigned RegisterClipboardFormatW(const uint16_t *name)
{ return pe_user_name_register(&pe_clipboard_names, name); }
__attribute__((ms_abi)) unsigned RegisterClipboardFormatA(const char *name)
{
    uint16_t wide[256];
    if (!name || !MultiByteToWideChar(65001, 8, name, -1, wide, 256)) return 0;
    return RegisterClipboardFormatW(wide);
}
__attribute__((ms_abi)) int GetClipboardFormatNameW(unsigned format, uint16_t *out, int cap)
{
    if (!out || cap <= 0) { SetLastError(87); return 0; }
    PE_STATE_GUARD;
    if (format < 0xc000 || format-0xc000 >= pe_clipboard_names.count) { SetLastError(87); return 0; }
    const uint16_t *name = pe_clipboard_names.entries[format-0xc000].name;
    int count = lstrlenW(name); if (count >= cap) count = cap-1;
    memcpy(out, name, count*2); out[count] = 0; return count;
}
__attribute__((ms_abi)) int GetClipboardFormatNameA(unsigned format, char *out, int cap)
{
    if (!out || cap <= 0) { SetLastError(87); return 0; }
    uint16_t wide[256]; int count = GetClipboardFormatNameW(format, wide, 256);
    if (!count) return 0;
    /* Use complete UTF-8 scalars when truncating a registered name. */
    char utf8[1024]; int n = WideCharToMultiByte(65001, 0, wide, count, utf8, sizeof(utf8), NULL, NULL);
    if (!n) return 0;
    int bytes = n < cap ? n : cap-1;
    while (bytes < n && bytes > 0 && ((unsigned char)utf8[bytes] & 0xc0) == 0x80) bytes--;
    memcpy(out, utf8, bytes); out[bytes] = 0; return bytes;
}
struct pe_wndclass {
    uint32_t style; void *wndproc; int32_t class_extra, window_extra;
    HANDLE instance, icon, cursor, background; const uint16_t *menu_name, *class_name;
};
_Static_assert(sizeof(struct pe_wndclass) == 72, "WNDCLASSW layout");
__attribute__((ms_abi)) uint16_t RegisterClassW(const struct pe_wndclass *c)
{
    if (!c) { SetLastError(87); return 0; }
    struct pe_wndclassex ex = {sizeof(ex), c->style, c->wndproc, c->class_extra, c->window_extra,
        c->instance, c->icon, c->cursor, c->background, c->menu_name, c->class_name, NULL};
    return RegisterClassExW(&ex);
}
__attribute__((ms_abi)) BOOL GetClassInfoW(HANDLE instance, const uint16_t *name, struct pe_wndclass *out)
{
    if (!out) { SetLastError(87); return 0; }
    PE_STATE_GUARD; struct pe_wndclassex ex = {0}; ex.size = sizeof(ex);
    if (!GetClassInfoExW(instance, name, &ex)) return 0;
    *out = (struct pe_wndclass){ex.style, ex.wndproc, ex.class_extra, ex.window_extra,
        ex.instance, ex.icon, ex.cursor, ex.background, ex.menu_name, ex.class_name}; return 1;
}

/* Native folder profile under the existing Z: -> / mapping. The host
 * environment is read privately for folder locations; the Win32 environment
 * block remains intentionally empty until its API surface is implemented. */
static char *pe_host_environment;
static const char *pe_host_env(const char *name)
{
    PE_STATE_GUARD;
    if (!pe_host_environment) {
        char *p = pe_raw_map(65537); if (!p) return NULL;
        long fd = raw_syscall3(SYS_open, (long)"/proc/self/environ", 0, 0);
        unsigned used = 0;
        if (fd >= 0) {
            while (used < 65536) {
                long n = raw_syscall3(SYS_read, fd, (long)(p+used), 65536-used);
                if (n == -4) continue;
                if (n <= 0) break; used += n;
            }
            raw_syscall3(SYS_close, fd, 0, 0);
        }
        p[used] = 0; pe_host_environment = p;
    }
    size_t length = cstring_length(name);
    for (const char *p = pe_host_environment; *p; p += cstring_length(p)+1)
        if (!memcmp(p, name, length) && p[length] == '=') return p+length+1;
    return NULL;
}
static int pe_native_folder_path(const char *path, uint16_t *out, int cap, bool trailing)
{
    if (!path || path[0] != '/') { SetLastError(3); return 0; }
    uint16_t wide[4096];
    int n = MultiByteToWideChar(65001, 8, path, -1, wide, 4096); if (!n) return 0;
    int length = n+1; /* omit source NUL, add Z: */
    if (trailing && wide[n-2] != '/') length++;
    if (cap <= length) return length+1;
    if (!out) { SetLastError(87); return 0; }
    out[0] = 'Z'; out[1] = ':';
    for (int i = 0; i < n-1; i++) out[i+2] = wide[i] == '/' ? '\\' : wide[i];
    if (length > n+1) out[length-1] = '\\';
    out[length] = 0; return length;
}
static const char *pe_folder_location(unsigned id, char *buffer)
{
    const char *home = pe_host_env("HOME"), *suffix = NULL, *override = NULL;
    switch (id) {
    case 0x26: case 0x2a: return "/opt";
    case 0x2b: case 0x2c: return "/usr/share";
    case 0x24: return "/"; case 0x25: case 0x29: return "/usr/lib";
    case 0x14: return "/usr/share/fonts"; case 0x23: return "/var/lib";
    case 0x28: return home;
    case 0x1a: override = pe_host_env("XDG_CONFIG_HOME"); suffix = "/.config"; break;
    case 0x1c: override = pe_host_env("XDG_DATA_HOME"); suffix = "/.local/share"; break;
    case 0: case 0x10: suffix = "/Desktop"; break;
    case 5: suffix = "/Documents"; break;
    case 0x27: suffix = "/Pictures"; break;
    case 0x0d: suffix = "/Music"; break; case 0x0e: suffix = "/Videos"; break;
    default: SetLastError(50); return NULL;
    }
    if (override && override[0] == '/') return override;
    if (!home || home[0] != '/') { SetLastError(3); return NULL; }
    size_t n = cstring_length(home), m = cstring_length(suffix);
    if (n+m >= 4096) { SetLastError(206); return NULL; }
    memcpy(buffer, home, n); memcpy(buffer+n, suffix, m+1); return buffer;
}
__attribute__((ms_abi)) int32_t SHGetFolderPathW(HANDLE owner, int folder, HANDLE token, DWORD flags, uint16_t *out)
{
    (void)owner;
    if (!out || token || flags > 1) return (int32_t)0x80070057;
    out[0] = 0; char buffer[4096];
    const char *location = pe_folder_location(folder & 0xff, buffer);
    if (!location) return (int32_t)0x80070000 | GetLastError();
    if (!(folder & 0x4000)) {
        struct stat s; long r = raw_syscall4(SYS_newfstatat, -100, (long)location, (long)&s, 0);
        if (r < 0 || !S_ISDIR(s.st_mode)) return (int32_t)0x80070003;
    }
    int n = pe_native_folder_path(location, out, 260, false);
    if (!n || n >= 260) { out[0] = 0; return (int32_t)0x800700ce; }
    return 0;
}
__attribute__((ms_abi)) int32_t SHGetFolderPathA(HANDLE owner, int folder, HANDLE token, DWORD flags, char *out)
{
    if (!out) return (int32_t)0x80070057;
    uint16_t wide[260]; out[0] = 0;
    int32_t status = SHGetFolderPathW(owner, folder, token, flags, wide); if (status < 0) return status;
    return WideCharToMultiByte(65001, 0, wide, -1, out, 260, NULL, NULL) ? 0 : (int32_t)0x800700ce;
}
__attribute__((ms_abi)) BOOL SHGetSpecialFolderPathW(HANDLE owner, uint16_t *out, int folder, BOOL create)
{
    if (create) { SetLastError(50); return 0; } /* profile-directory creation is not implemented */
    return SHGetFolderPathW(owner, folder, NULL, 0, out) >= 0;
}
__attribute__((ms_abi)) BOOL SHGetSpecialFolderPathA(HANDLE owner, char *out, int folder, BOOL create)
{
    if (create) { SetLastError(50); return 0; }
    return SHGetFolderPathA(owner, folder, NULL, 0, out) >= 0;
}
__attribute__((ms_abi)) DWORD GetSystemDirectoryW(uint16_t *out, DWORD cap)
{ return pe_native_folder_path("/usr/lib", out, cap, false); }
__attribute__((ms_abi)) DWORD GetWindowsDirectoryW(uint16_t *out, DWORD cap)
{ return pe_native_folder_path("/", out, cap, false); }
__attribute__((ms_abi)) DWORD GetTempPathW(DWORD cap, uint16_t *out)
{
    const char *temp = pe_host_env("TMPDIR");
    return pe_native_folder_path(temp && temp[0] == '/' ? temp : "/tmp", out, cap, true);
}
static DWORD pe_native_folder_ansi(const char *path, char *out, DWORD cap, bool trailing)
{
    uint16_t wide[4096]; int n = pe_native_folder_path(path, wide, 4096, trailing);
    if (!n || n >= 4096) return 0;
    int bytes = WideCharToMultiByte(65001, 0, wide, -1, NULL, 0, NULL, NULL);
    if (!bytes) return 0;
    if (cap < (DWORD)bytes) return bytes;
    if (!out) { SetLastError(87); return 0; }
    return WideCharToMultiByte(65001, 0, wide, -1, out, cap, NULL, NULL) ? bytes-1 : 0;
}
__attribute__((ms_abi)) DWORD GetSystemDirectoryA(char *out, DWORD cap)
{ return pe_native_folder_ansi("/usr/lib", out, cap, false); }
__attribute__((ms_abi)) DWORD GetWindowsDirectoryA(char *out, DWORD cap)
{ return pe_native_folder_ansi("/", out, cap, false); }
__attribute__((ms_abi)) DWORD GetTempPathA(DWORD cap, char *out)
{
    const char *temp = pe_host_env("TMPDIR");
    return pe_native_folder_ansi(temp && temp[0] == '/' ? temp : "/tmp", out, cap, true);
}

__attribute__((ms_abi)) BOOL CopyFileW(const uint16_t *source, const uint16_t *destination, BOOL exclusive)
{
    char src[4096], dst[4096];
    if (!pe_linux_path(source, src, sizeof(src)) || !pe_linux_path(destination, dst, sizeof(dst))) return 0;
    long input = raw_syscall4(SYS_openat, -100, (long)src, 0x80000, 0);
    if (input < 0) { SetLastError(pe_file_error(input)); return 0; }
    struct stat a, b; long error = raw_syscall3(SYS_fstat, input, (long)&a, 0), output = -1;
    void *buffer = NULL;
    if (error < 0) goto done;
    if (!S_ISREG(a.st_mode)) { error = -13; goto done; }
    if (!raw_syscall4(SYS_newfstatat, -100, (long)dst, (long)&b, 0) && a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
        error = -17; goto done;
    }
    output = raw_syscall4(SYS_openat, -100, (long)dst, 0x80000|1|0x40|(exclusive ? 0x80 : 0x200), 0666);
    if (output < 0) { error = output; goto done; }
    buffer = pe_raw_map(65536);
    if (!buffer) { error = -12; goto done; }
    for (;;) {
        long n = raw_syscall3(SYS_read, input, (long)buffer, 65536);
        if (n == -4) continue;
        if (n <= 0) { error = n; break; }
        long pos = 0;
        while (pos < n) {
            long wrote = raw_syscall3(SYS_write, output, (long)buffer+pos, n-pos);
            if (wrote == -4) continue;
            if (wrote <= 0) { error = wrote ? wrote : -5; goto done; }
            pos += wrote;
        }
    }
done:
    if (buffer) raw_syscall3(SYS_munmap, (long)buffer, 65536, 0);
    if (output >= 0) raw_syscall3(SYS_close, output, 0, 0);
    raw_syscall3(SYS_close, input, 0, 0);
    if (error < 0) { SetLastError(pe_file_error(error)); return 0; }
    return 1; /* content copy; complete DOS metadata/ACL preservation is TODO */
}
__attribute__((ms_abi)) BOOL CopyFileA(const char *source, const char *destination, BOOL exclusive)
{
    uint16_t src[4096], dst[4096];
    if (!source || !destination) { SetLastError(87); return 0; }
    if (!MultiByteToWideChar(65001, 8, source, -1, src, 4096) || !MultiByteToWideChar(65001, 8, destination, -1, dst, 4096)) return 0;
    return CopyFileW(src, dst, exclusive);
}
__attribute__((ms_abi)) BOOL DeleteFileA(const char *path)
{
    uint16_t wide[4096];
    if (!path) { SetLastError(87); return 0; }
    if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return 0;
    return DeleteFileW(wide);
}
__attribute__((ms_abi)) BOOL FlushFileBuffers(HANDLE h)
{
    int fd = pe_fd_pin(h); if (fd < 0) return 0;
    long r; do { r = raw_syscall3(SYS_fsync, fd, 0, 0); } while (r == -4);
    raw_syscall3(SYS_close, fd, 0, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; } return 1;
}
struct pe_file_information {
    DWORD attributes; struct pe_filetime creation, access, write;
    DWORD volume, size_high, size_low, links, index_high, index_low;
};
_Static_assert(sizeof(struct pe_file_information) == 52, "BY_HANDLE_FILE_INFORMATION layout");
__attribute__((ms_abi)) BOOL GetFileInformationByHandle(HANDLE h, struct pe_file_information *out)
{
    if (!out) { SetLastError(87); return 0; }
    int fd = pe_fd_pin(h); if (fd < 0) return 0;
    struct stat s; long r = raw_syscall3(SYS_fstat, fd, (long)&s, 0);
    raw_syscall3(SYS_close, fd, 0, 0);
    if (r < 0) { SetLastError(pe_file_error(r)); return 0; }
    *out = (struct pe_file_information){pe_stat_attributes(&s), pe_stat_time(s.st_ctim.tv_sec, s.st_ctim.tv_nsec),
        pe_stat_time(s.st_atim.tv_sec, s.st_atim.tv_nsec), pe_stat_time(s.st_mtim.tv_sec, s.st_mtim.tv_nsec),
        s.st_dev, (uint64_t)s.st_size >> 32, s.st_size, s.st_nlink, (uint64_t)s.st_ino >> 32, s.st_ino}; return 1;
}

struct pe_system_time { uint16_t year, month, weekday, day, hour, minute, second, millis; };
static bool pe_leap(unsigned year) { return !(year%4) && (year%100 || !(year%400)); }
static uint64_t pe_days_before_year(unsigned year)
{ uint64_t y = year-1; return (year-1601ULL)*365+y/4-y/100+y/400-388; }
__attribute__((ms_abi)) BOOL FileTimeToSystemTime(const struct pe_filetime *file, struct pe_system_time *out)
{
    if (!file || !out) { SetLastError(87); return 0; }
    uint64_t ticks = ((uint64_t)file->high << 32) | file->low;
    if (ticks >> 63) { SetLastError(87); return 0; }
    uint64_t seconds = ticks/10000000, days = seconds/86400, remainder = seconds%86400;
    unsigned low = 1601, high = 30829;
    while (low+1 < high) { unsigned mid = (low+high)/2; if (pe_days_before_year(mid) <= days) low = mid; else high = mid; }
    if (low > 30827) { SetLastError(87); return 0; }
    unsigned day = days-pe_days_before_year(low), month = 1;
    static const unsigned lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (;;) {
        unsigned length = lengths[month-1]+(month == 2 && pe_leap(low));
        if (day < length) break; day -= length; month++;
    }
    *out = (struct pe_system_time){low, month, (days+1)%7, day+1, remainder/3600,
        (remainder%3600)/60, remainder%60, (ticks%10000000)/10000}; return 1;
}
__attribute__((ms_abi)) BOOL SystemTimeToFileTime(const struct pe_system_time *time, struct pe_filetime *out)
{
    if (!time || !out || time->year < 1601 || time->year > 30827 || time->month < 1 || time->month > 12 ||
        time->hour > 23 || time->minute > 59 || time->second > 59 || time->millis > 999) { SetLastError(87); return 0; }
    static const unsigned lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned month_days = lengths[time->month-1]+(time->month == 2 && pe_leap(time->year));
    if (!time->day || time->day > month_days) { SetLastError(87); return 0; }
    uint64_t days = pe_days_before_year(time->year);
    for (unsigned m = 1; m < time->month; m++) days += lengths[m-1]+(m == 2 && pe_leap(time->year));
    days += time->day-1;
    uint64_t ticks = (days*86400+time->hour*3600+time->minute*60+time->second)*10000000+time->millis*10000;
    *out = (struct pe_filetime){ticks, ticks >> 32}; return 1;
}
__attribute__((ms_abi)) int32_t CompareFileTime(const struct pe_filetime *a, const struct pe_filetime *b)
{
    if (!a || !b) { SetLastError(87); return 0; }
    uint64_t x = ((uint64_t)a->high << 32) | a->low, y = ((uint64_t)b->high << 32) | b->low;
    return x == y ? 0 : x < y ? -1 : 1;
}
__attribute__((ms_abi)) void GetSystemTime(struct pe_system_time *out)
{
    if (!out) { SetLastError(87); return; }
    struct pe_timespec ts; raw_syscall3(SYS_clock_gettime, 0, (long)&ts, 0);
    struct pe_filetime ft = pe_stat_time(ts.tv_sec, ts.tv_nsec); FileTimeToSystemTime(&ft, out);
}
/* Deterministic UTC profile until native timezone/DST data are implemented. */
__attribute__((ms_abi)) void GetLocalTime(struct pe_system_time *out) { GetSystemTime(out); }
__attribute__((ms_abi)) BOOL FileTimeToLocalFileTime(const struct pe_filetime *a, struct pe_filetime *b)
{ if (!a || !b) { SetLastError(87); return 0; } *b = *a; return 1; }
__attribute__((ms_abi)) BOOL LocalFileTimeToFileTime(const struct pe_filetime *a, struct pe_filetime *b)
{ return FileTimeToLocalFileTime(a, b); }
__attribute__((ms_abi)) DWORD GetTimeZoneInformation(void *out)
{
    if (!out) { SetLastError(87); return UINT32_MAX; }
    memset(out, 0, 172);
    const uint16_t name[4] = {'U','T','C',0};
    memcpy((char *)out+4, name, sizeof(name)); memcpy((char *)out+88, name, sizeof(name)); return 0;
}

struct pe_timezone {
    int32_t bias; uint16_t standard_name[32]; struct pe_system_time standard_date; int32_t standard_bias;
    uint16_t daylight_name[32]; struct pe_system_time daylight_date; int32_t daylight_bias;
};
_Static_assert(sizeof(struct pe_timezone) == 172, "TIME_ZONE_INFORMATION layout");
static BOOL pe_system_time_zone_shift(const struct pe_timezone *zone, const struct pe_system_time *source,
    struct pe_system_time *out, bool to_utc)
{
    if (!source || !out) { SetLastError(87); return 0; }
    if (zone && (zone->standard_date.month || zone->daylight_date.month)) { SetLastError(50); return 0; }
    struct pe_filetime ft;
    if (!SystemTimeToFileTime(source, &ft)) return 0;
    int64_t value = ((uint64_t)ft.high << 32) | ft.low;
    int64_t delta = zone ? ((int64_t)zone->bias+zone->standard_bias)*600000000LL : 0;
    if (!to_utc) delta = -delta;
    if ((delta > 0 && value > INT64_MAX-delta) || (delta < 0 && value < -delta)) { SetLastError(87); return 0; }
    value += delta; ft = (struct pe_filetime){value, (uint64_t)value >> 32};
    return FileTimeToSystemTime(&ft, out);
}
__attribute__((ms_abi)) BOOL SystemTimeToTzSpecificLocalTime(const struct pe_timezone *zone,
    const struct pe_system_time *utc, struct pe_system_time *local)
{ return pe_system_time_zone_shift(zone, utc, local, false); }
__attribute__((ms_abi)) BOOL TzSpecificLocalTimeToSystemTime(const struct pe_timezone *zone,
    const struct pe_system_time *local, struct pe_system_time *utc)
{ return pe_system_time_zone_shift(zone, local, utc, true); }

/* Directory search handles use their own range and pin records across FindClose.
 * Potentially blocking getdents/stat calls hold only a per-search mutex. */
struct pe_find_data_w {
    DWORD attributes; struct pe_filetime creation, access, write;
    DWORD size_high, size_low, reserved[2]; uint16_t name[260], alternate[14];
};
struct pe_find_data_a {
    DWORD attributes; struct pe_filetime creation, access, write;
    DWORD size_high, size_low, reserved[2]; char name[260], alternate[14];
};
_Static_assert(sizeof(struct pe_find_data_w) == 592, "WIN32_FIND_DATAW layout");
_Static_assert(sizeof(struct pe_find_data_a) == 320, "WIN32_FIND_DATAA layout");
struct pe_find_search {
    HANDLE handle; struct pe_find_search *next; unsigned refs; uint32_t busy;
    int fd; unsigned position, used; bool directories_only, sensitive;
    uint16_t pattern[260]; unsigned char buffer[4096];
};
static struct pe_find_search *pe_find_searches;
static unsigned pe_next_find_handle;
static struct pe_find_search *pe_find_pin(HANDLE h)
{
    PE_STATE_GUARD;
    for (struct pe_find_search *s = pe_find_searches; s; s = s->next)
        if (s->handle == h) { s->refs++; return s; }
    SetLastError(6); return NULL;
}
static void pe_find_unpin(struct pe_find_search *s)
{
    PE_STATE_GUARD;
    if (!--s->refs) {
        raw_syscall3(SYS_close, s->fd, 0, 0);
        raw_syscall3(SYS_munmap, (long)s, sizeof(*s), 0);
    }
}
static void pe_find_lock(struct pe_find_search *s)
{
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&s->busy, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        raw_syscall6(SYS_futex, (long)&s->busy, 128, 1, 0, 0, 0);
    }
}
static void pe_find_unlock(struct pe_find_search *s)
{
    __atomic_store_n(&s->busy, 0, __ATOMIC_RELEASE);
    raw_syscall6(SYS_futex, (long)&s->busy, 129, 1, 0, 0, 0);
}
static bool pe_find_match(const uint16_t *pattern, const uint16_t *name, bool sensitive)
{
    const uint16_t *star = NULL, *retry = NULL;
    if (pattern[0] == '*' && pattern[1] == '.' && pattern[2] == '*' && !pattern[3]) pattern += 2;
    while (*name) {
        unsigned p = *pattern, n = *name;
        if (!sensitive) { if (p >= 'A' && p <= 'Z') p += 32; if (n >= 'A' && n <= 'Z') n += 32; }
        if (*pattern == '*') { star = pattern++; retry = name; }
        else if (*pattern == '?' || p == n) { pattern++; name++; }
        else if (star) { pattern = star+1; name = ++retry; }
        else return false;
    }
    while (*pattern == '*') pattern++;
    return !*pattern;
}
__attribute__((ms_abi)) BOOL FindNextFileW(HANDLE h, struct pe_find_data_w *out)
{
    if (!out) { SetLastError(87); return 0; }
    struct pe_find_search *s = pe_find_pin(h); if (!s) return 0;
    pe_find_lock(s); BOOL found = 0; DWORD error = 18;
    for (;;) {
        if (s->position == s->used) {
            long n;
            do { n = raw_syscall3(SYS_getdents64, s->fd, (long)s->buffer, sizeof(s->buffer)); } while (n == -4);
            if (n <= 0) { if (n < 0) error = pe_file_error(n); break; }
            s->position = 0; s->used = n;
        }
        /* linux_dirent64: ino64, off64, reclen16, type8, name[]. */
        unsigned char *entry = s->buffer+s->position;
        uint16_t record_length; memcpy(&record_length, entry+16, 2);
        if (record_length < 20 || record_length > s->used-s->position) { error = 31; break; }
        s->position += record_length;
        const char *name = (const char *)entry+19;
        bool terminated = false;
        for (unsigned i = 0; i < record_length-19; i++) if (!name[i]) { terminated = true; break; }
        if (!terminated) { error = 31; break; }
        uint16_t wide[260];
        if (!MultiByteToWideChar(65001, 8, name, -1, wide, 260) || !pe_find_match(s->pattern, wide, s->sensitive)) continue;
        struct stat st;
        long r = raw_syscall4(SYS_newfstatat, s->fd, (long)name, (long)&st, 0);
        if (r < 0 || (s->directories_only && !S_ISDIR(st.st_mode))) continue;
        memset(out, 0, sizeof(*out)); out->attributes = pe_stat_attributes(&st);
        out->creation = pe_stat_time(st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
        out->access = pe_stat_time(st.st_atim.tv_sec, st.st_atim.tv_nsec);
        out->write = pe_stat_time(st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
        out->size_high = (uint64_t)st.st_size >> 32; out->size_low = st.st_size;
        memcpy(out->name, wide, (lstrlenW(wide)+1)*2); found = 1; break;
    }
    pe_find_unlock(s); pe_find_unpin(s);
    if (!found) SetLastError(error); return found;
}
__attribute__((ms_abi)) BOOL FindClose(HANDLE h)
{
    PE_STATE_GUARD; struct pe_find_search **p = &pe_find_searches;
    while (*p && (*p)->handle != h) p = &(*p)->next;
    if (!*p) { SetLastError(6); return 0; }
    struct pe_find_search *s = *p; *p = s->next; pe_find_unpin(s); return 1;
}
__attribute__((ms_abi)) HANDLE FindFirstFileExW(const uint16_t *pattern, unsigned level, struct pe_find_data_w *out,
    unsigned operation, void *filter, DWORD flags)
{
    if (!out || level > 1 || operation > 1 || filter || (flags & ~7u)) { SetLastError(87); return (HANDLE)(intptr_t)-1; }
    char path[4096]; if (!pe_linux_path(pattern, path, sizeof(path))) return (HANDLE)(intptr_t)-1;
    char *last = NULL; for (char *p = path; *p; p++) if (*p == '/') last = p;
    const char *directory = ".", *basename = path;
    if (last) { basename = last+1; if (last == path) directory = "/"; else { *last = 0; directory = path; } }
    if (!*basename) { SetLastError(123); return (HANDLE)(intptr_t)-1; }
    struct pe_find_search *s = pe_raw_map(sizeof(*s)); if (!s) { SetLastError(8); return (HANDLE)(intptr_t)-1; }
    if (!MultiByteToWideChar(65001, 8, basename, -1, s->pattern, 260)) { raw_syscall3(SYS_munmap, (long)s, sizeof(*s), 0); return (HANDLE)(intptr_t)-1; }
    long fd = raw_syscall4(SYS_openat, -100, (long)directory, 0x90000, 0);
    if (fd < 0) { raw_syscall3(SYS_munmap, (long)s, sizeof(*s), 0); SetLastError(fd == -2 ? 3 : pe_file_error(fd)); return (HANDLE)(intptr_t)-1; }
    s->fd = fd; s->refs = 1; s->directories_only = operation == 1; s->sensitive = !!(flags & 1);
    {
        PE_STATE_GUARD;
        if (pe_next_find_handle >= 0x2000000) { pe_find_unpin(s); SetLastError(8); return (HANDLE)(intptr_t)-1; }
        s->handle = (HANDLE)(uintptr_t)(0x48000000ULL+4*pe_next_find_handle++);
        s->next = pe_find_searches; pe_find_searches = s;
    }
    HANDLE h = s->handle;
    if (FindNextFileW(h, out)) return h;
    DWORD error = GetLastError(); FindClose(h); SetLastError(error == 18 ? 2 : error); return (HANDLE)(intptr_t)-1;
}
__attribute__((ms_abi)) HANDLE FindFirstFileW(const uint16_t *pattern, struct pe_find_data_w *out)
{ return FindFirstFileExW(pattern, 0, out, 0, NULL, 0); }
static bool pe_find_data_ansi(const struct pe_find_data_w *wide, struct pe_find_data_a *out)
{
    memset(out, 0, sizeof(*out)); memcpy(out, wide, 44);
    return WideCharToMultiByte(65001, 0, wide->name, -1, out->name, 260, NULL, NULL) != 0;
}
__attribute__((ms_abi)) BOOL FindNextFileA(HANDLE h, struct pe_find_data_a *out)
{
    if (!out) { SetLastError(87); return 0; }
    struct pe_find_data_w wide; if (!FindNextFileW(h, &wide)) return 0;
    return pe_find_data_ansi(&wide, out);
}
__attribute__((ms_abi)) HANDLE FindFirstFileExA(const char *pattern, unsigned level, struct pe_find_data_a *out,
    unsigned operation, void *filter, DWORD flags)
{
    if (!pattern || !out) { SetLastError(87); return (HANDLE)(intptr_t)-1; }
    uint16_t path[4096]; struct pe_find_data_w wide;
    if (!MultiByteToWideChar(65001, 8, pattern, -1, path, 4096)) return (HANDLE)(intptr_t)-1;
    HANDLE h = FindFirstFileExW(path, level, &wide, operation, filter, flags);
    if (h != (HANDLE)(intptr_t)-1 && !pe_find_data_ansi(&wide, out)) { DWORD error = GetLastError(); FindClose(h); SetLastError(error); return (HANDLE)(intptr_t)-1; }
    return h;
}
__attribute__((ms_abi)) HANDLE FindFirstFileA(const char *pattern, struct pe_find_data_a *out)
{ return FindFirstFileExA(pattern, 0, out, 0, NULL, 0); }

static bool pe_path_filesystem(const uint16_t *path, struct statfs *out, bool parents)
{
    char native[4096];
    if (!path) { native[0] = '.'; native[1] = 0; }
    else if (!pe_linux_path(path, native, sizeof(native))) return false;
    for (;;) {
        long r = raw_syscall3(SYS_statfs, (long)native, (long)out, 0);
        if (!r) return true;
        if (!parents || (r != -2 && r != -20)) { SetLastError(pe_file_error(r)); return false; }
        char *last = NULL;
        for (char *p = native; *p; p++) if (*p == '/') last = p;
        if (!last) { native[0] = '.'; native[1] = 0; parents = false; }
        else if (last == native) {
            if (!native[1]) { SetLastError(pe_file_error(r)); return false; }
            native[1] = 0;
        } else *last = 0;
    }
}
static bool pe_remote_filesystem(const struct statfs *fs)
{
    unsigned long type = fs->f_type;
    return type == 0x6969 /* NFS */ || type == 0xff534d42 /* CIFS */ ||
        type == 0x517b /* SMB */ || type == 0xfe534d42 /* SMB2 */ || type == 0x564c /* NCP */;
}
__attribute__((ms_abi)) BOOL PathIsNetworkPathW(const uint16_t *path)
{
    if (!path) { SetLastError(87); return 0; }
    if (PathIsUNCW(path)) return 1;
    struct statfs fs; return pe_path_filesystem(path, &fs, true) && pe_remote_filesystem(&fs);
}
__attribute__((ms_abi)) BOOL PathIsNetworkPathA(const char *path)
{
    uint16_t wide[4096];
    if (!path) { SetLastError(87); return 0; }
    if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return 0;
    return PathIsNetworkPathW(wide);
}
__attribute__((ms_abi)) unsigned GetDriveTypeW(const uint16_t *root)
{
    if (root && PathIsUNCW(root)) return 4;
    struct statfs fs;
    if (!pe_path_filesystem(root, &fs, true)) return 1;
    if (pe_remote_filesystem(&fs)) return 4;
    return fs.f_type == 0x01021994 /* TMPFS */ || fs.f_type == 0x858458f6 /* RAMFS */ ? 6 : 3;
}
__attribute__((ms_abi)) unsigned GetDriveTypeA(const char *root)
{
    if (!root) return GetDriveTypeW(NULL);
    uint16_t wide[4096]; if (!MultiByteToWideChar(65001, 8, root, -1, wide, 4096)) return 1;
    return GetDriveTypeW(wide);
}
__attribute__((ms_abi)) BOOL GetDiskFreeSpaceExW(const uint16_t *path, uint64_t *available, uint64_t *total, uint64_t *free_bytes)
{
    struct statfs fs; if (!pe_path_filesystem(path, &fs, false)) return 0;
    uint64_t block = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
    if (!block || fs.f_blocks > UINT64_MAX/block || fs.f_bavail > UINT64_MAX/block || fs.f_bfree > UINT64_MAX/block) { SetLastError(534); return 0; }
    if (available) *available = fs.f_bavail*block;
    if (total) *total = fs.f_blocks*block;
    if (free_bytes) *free_bytes = fs.f_bfree*block;
    return 1;
}
__attribute__((ms_abi)) BOOL GetDiskFreeSpaceExA(const char *path, uint64_t *available, uint64_t *total, uint64_t *free_bytes)
{
    if (!path) return GetDiskFreeSpaceExW(NULL, available, total, free_bytes);
    uint16_t wide[4096]; if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return 0;
    return GetDiskFreeSpaceExW(wide, available, total, free_bytes);
}
__attribute__((ms_abi)) BOOL GetDiskFreeSpaceW(const uint16_t *path, DWORD *sectors, DWORD *bytes_per_sector, DWORD *free_clusters, DWORD *total_clusters)
{
    if (!sectors || !bytes_per_sector || !free_clusters || !total_clusters) { SetLastError(87); return 0; }
    struct statfs fs; if (!pe_path_filesystem(path, &fs, false)) return 0;
    uint64_t block = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
    if (block < 512 || block > UINT32_MAX) { SetLastError(50); return 0; }
    *sectors = (block+511)/512; *bytes_per_sector = 512;
    *free_clusters = fs.f_bavail > UINT32_MAX ? UINT32_MAX : fs.f_bavail;
    *total_clusters = fs.f_blocks > UINT32_MAX ? UINT32_MAX : fs.f_blocks; return 1;
}
__attribute__((ms_abi)) BOOL GetDiskFreeSpaceA(const char *path, DWORD *sectors, DWORD *bytes, DWORD *free_clusters, DWORD *total_clusters)
{
    if (!path) return GetDiskFreeSpaceW(NULL, sectors, bytes, free_clusters, total_clusters);
    uint16_t wide[4096]; if (!MultiByteToWideChar(65001, 8, path, -1, wide, 4096)) return 0;
    return GetDiskFreeSpaceW(wide, sectors, bytes, free_clusters, total_clusters);
}

/* Process-local registry. Stable mmap nodes survive deletion until process exit;
 * opened handles pin their node and deleted nodes reject further operations. */
struct pe_reg_value { struct pe_reg_value *next; uint16_t *name; DWORD type, size; size_t allocation; void *data; };
struct pe_reg_node { struct pe_reg_node *next, *children, *parent; struct pe_reg_value *values; uint16_t *name; bool deleted; };
static struct pe_reg_node *pe_reg_roots[5];
static unsigned pe_reg_fold(unsigned c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7) || (c >= 0x410 && c <= 0x42f)) return c+32;
    return c;
}
static bool pe_reg_equal(const uint16_t *a, const uint16_t *b)
{ while (*a && pe_reg_fold(*a) == pe_reg_fold(*b)) { a++; b++; } return *a == *b; }
static uint16_t *pe_reg_string(const void *s, bool ansi)
{
    static const uint16_t empty; if (!s) s = ansi ? (const void *)"" : &empty;
    int n = ansi ? MultiByteToWideChar(65001, 8, s, -1, NULL, 0) : lstrlenW(s)+1;
    if (n <= 0) return NULL;
    uint16_t *w = pe_raw_map((size_t)n*2); if (!w) return NULL;
    if (ansi) MultiByteToWideChar(65001, 8, s, -1, w, n); else memcpy(w, s, n*2);
    return w;
}
static void pe_reg_drop_string(uint16_t *s)
{ if (s) raw_syscall3(SYS_munmap, (long)s, (lstrlenW(s)+1)*2, 0); }
static struct pe_reg_node *pe_reg_walk(struct pe_reg_node *n, const uint16_t *path, bool create, bool *fresh)
{
    *fresh = false;
    while (path && *path) {
        while (*path == '\\') path++;
        unsigned len=0; while (path[len] && path[len]!='\\') len++;
        if (!len) break;
        uint16_t *part=pe_raw_map((len+1)*2); if (!part) return NULL;
        memcpy(part,path,len*2); struct pe_reg_node *c=n->children;
        while (c && !pe_reg_equal(c->name,part)) c=c->next;
        if (!c && create) {
            c=pe_raw_map(sizeof(*c)); if (c) { c->name=part; part=NULL; c->parent=n; c->next=n->children; n->children=c; *fresh=true; }
        } else *fresh=false;
        pe_reg_drop_string(part); if (!c) return NULL; n=c; path+=len;
    }
    return n;
}
static DWORD pe_reg_set(struct pe_reg_node *n, const uint16_t *name, DWORD type, const void *data, DWORD size, bool ansi)
{
    if ((size && !data) || ((type==1 || type==2 || type==7) && !ansi && (size&1))) return 87;
    bool str=type==1 || type==2 || type==7; DWORD bytes=size; void *copy;
    if (str && ansi) { int count=size ? MultiByteToWideChar(65001,8,data,size,NULL,0) : 0; if (size && !count) return 87; bytes=count*2; }
    if (bytes > 0x7ffffff0u) return 87;
    size_t allocation=(size_t)bytes+4;
    copy=pe_raw_map(allocation); if (!copy) return 8;
    if (str && ansi && size) MultiByteToWideChar(65001,8,data,size,copy,bytes/2);
    else if (bytes) memcpy(copy,data,bytes);
    if (str) {
        uint16_t *w=copy; unsigned count=bytes/2;
        if (!count || w[count-1]) w[count++]=0;
        if (type==7 && (count<2 || w[count-2])) w[count++]=0;
        bytes=count*2;
    }
    struct pe_reg_value *v=n->values; while(v && !pe_reg_equal(v->name,name)) v=v->next;
    if (!v) { v=pe_raw_map(sizeof(*v)); if (!v) { raw_syscall3(SYS_munmap,(long)copy,allocation,0); return 8; }
        v->name=pe_reg_string(name,false); if(!v->name) { raw_syscall3(SYS_munmap,(long)v,sizeof(*v),0); raw_syscall3(SYS_munmap,(long)copy,allocation,0); return 8; }
        v->next=n->values; n->values=v;
    } else if(v->data) raw_syscall3(SYS_munmap,(long)v->data,v->allocation,0);
    v->data=copy; v->allocation=allocation; v->size=bytes; v->type=type; return 0;
}
static bool pe_reg_init(void)
{
    if (pe_reg_roots[0]) return true;
    for(unsigned i=0;i<5;i++) if(!(pe_reg_roots[i]=pe_raw_map(sizeof(struct pe_reg_node)))) pe_loader_error("registry roots allocation failed");
    const char *paths[]={"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion","SOFTWARE\\Microsoft\\Cryptography"};
    for(unsigned i=0;i<2;i++) {
        uint16_t *p=pe_reg_string(paths[i],true); bool fresh; struct pe_reg_node *n=p?pe_reg_walk(pe_reg_roots[2],p,true,&fresh):NULL; pe_reg_drop_string(p);
        if(!n) pe_loader_error("registry seed allocation failed");
        const char *names[]={"ProductName","CurrentVersion","CurrentBuild","CurrentBuildNumber","CurrentMajorVersionNumber","CurrentMinorVersionNumber","EditionID","InstallationType","DisplayVersion"};
        const char *values[]={"Windows 10 Pro","6.3",PE_WIN_BUILD_STRING,PE_WIN_BUILD_STRING,"","","Professional","Client","22H2"};
        for(unsigned j=0;j<(i?1:9);j++) {
            const char *name=i?"MachineGuid":names[j], *value=i?"73b34075-94af-4b77-bcf1-c21a692c3a10":values[j];
            uint16_t *w=pe_reg_string(name,true); DWORD number=j==4?PE_WIN_MAJOR:PE_WIN_MINOR;
            DWORD r=w?pe_reg_set(n,w,(!i&&(j==4||j==5))?4:1,(!i&&(j==4||j==5))?(void *)&number:(void *)value,(!i&&(j==4||j==5))?4:cstring_length(value)+1,true):8;
            pe_reg_drop_string(w); if(r) pe_loader_error("registry seed value allocation failed");
        }
    }
    return true;
}
static struct pe_reg_node *pe_reg_lookup(HANDLE h, DWORD required)
{
    pe_reg_init(); uintptr_t x=(uintptr_t)h;
    if(x>=0xffffffff80000000ULL && x<=0xffffffff80000005ULL && x!=0xffffffff80000004ULL) return pe_reg_roots[(unsigned)x==0x80000005?4:(unsigned)x-0x80000000];
    for(struct pe_wait_handle *v=pe_wait_handles;v;v=v->next) if(v->value==h && v->object->kind==PE_REGKEY) {
        if(v->object->u.reg.node->deleted) return NULL;
        if((v->object->u.reg.access & required)!=required) return NULL;
        return v->object->u.reg.node;
    }
    return NULL;
}
static DWORD pe_reg_error(HANDLE h)
{ return pe_reg_lookup(h,0)?5:6; }
static DWORD pe_reg_open(HANDLE h,const void *sub,DWORD options,DWORD access,HANDLE *out,DWORD *disp,bool create,bool ansi)
{
    PE_STATE_GUARD; DWORD saved=GetLastError(), result=0;
    struct pe_reg_node *n=pe_reg_lookup(h,create?4:0);
    if(!n) return pe_reg_error(h); if(!out || options>1) return 87;
    uint16_t *w=pe_reg_string(sub,ansi); if(!w) {SetLastError(saved);return 8;}
    bool fresh; n=pe_reg_walk(n,w,create,&fresh); pe_reg_drop_string(w);
    if(!n) result=create?8:2;
    else {
        struct pe_wait_object *o=pe_raw_map(sizeof(*o));
        if(!o) result=8; else {
            o->kind=PE_REGKEY; o->u.reg.node=n; o->u.reg.access=access;
            /* Expand generic rights; WOW64 view flags select the same tree. */
            if(access&0x10000000) o->u.reg.access=0xf003f;
            if(access&0x80000000) o->u.reg.access|=0x20019;
            if(access&0x40000000) o->u.reg.access|=0x20006;
            o->next=pe_wait_objects; pe_wait_objects=o;
            HANDLE v=pe_wait_handle_new(o); if(!v) { pe_wait_reap(); result=8; } else { *out=v; if(disp)*disp=fresh?1:2; }
        }
    }
    SetLastError(saved); return result;
}
static DWORD pe_reg_value_copy(struct pe_reg_value *v,DWORD *type,void *data,DWORD *size,bool ansi)
{
    if(data&&!size)return 87; if(type)*type=v->type;
    bool str=v->type==1||v->type==2||v->type==7;
    DWORD needed=ansi&&str?WideCharToMultiByte(65001,0,v->data,v->size/2,NULL,0,NULL,NULL):v->size;
    if(!size)return 0; DWORD cap=*size; *size=needed;
    if(!data)return 0; if(cap<needed)return 234;
    if(ansi&&str)WideCharToMultiByte(65001,0,v->data,v->size/2,data,cap,NULL,NULL); else if(needed)memcpy(data,v->data,needed);
    return 0;
}
static DWORD pe_reg_query(HANDLE h,const void *name,DWORD *reserved,DWORD *type,void *data,DWORD *size,bool ansi)
{
    PE_STATE_GUARD; if(reserved)return 87;
    struct pe_reg_node *n=pe_reg_lookup(h,1); if(!n)return pe_reg_error(h);
    DWORD saved=GetLastError(); uint16_t *w=pe_reg_string(name,ansi); if(!w){SetLastError(saved);return 8;}
    struct pe_reg_value *v=n->values; while(v&&!pe_reg_equal(v->name,w))v=v->next; pe_reg_drop_string(w);
    DWORD r=v?pe_reg_value_copy(v,type,data,size,ansi):2; SetLastError(saved); return r;
}
static DWORD pe_reg_write(HANDLE h,const void *name,DWORD reserved,DWORD type,const void *data,DWORD size,bool ansi)
{
    PE_STATE_GUARD; if(reserved)return 87; struct pe_reg_node *n=pe_reg_lookup(h,2); if(!n)return pe_reg_error(h);
    DWORD saved=GetLastError(); uint16_t *w=pe_reg_string(name,ansi); DWORD r=w?pe_reg_set(n,w,type,data,size,ansi):8; pe_reg_drop_string(w); SetLastError(saved);return r;
}
__attribute__((ms_abi)) DWORD RegCloseKey(HANDLE h)
{
    PE_STATE_GUARD;
    if((uintptr_t)h>=0xffffffff80000000ULL) return pe_reg_lookup(h,0)?0:6;
    struct pe_wait_handle *v=pe_wait_handles;
    while(v && v->value!=h)v=v->next;
    if(!v || v->object->kind!=PE_REGKEY)return 6;
    DWORD saved=GetLastError(); CloseHandle(h); SetLastError(saved);return 0;
}
__attribute__((ms_abi)) DWORD RegFlushKey(HANDLE h)
{ PE_STATE_GUARD; return pe_reg_lookup(h,0)?0:6; }
static DWORD pe_reg_name_copy(const uint16_t *w,void *out,DWORD *size,bool ansi)
{
    if(!size || !out)return 87;
    DWORD n=ansi?WideCharToMultiByte(65001,0,w,-1,NULL,0,NULL,NULL):lstrlenW(w)+1;
    DWORD cap=*size; *size=n-1; if(cap<n)return 234;
    if(ansi)WideCharToMultiByte(65001,0,w,-1,out,cap,NULL,NULL);else memcpy(out,w,n*2);return 0;
}
static DWORD pe_reg_enum(HANDLE h,DWORD index,void *name,DWORD *len,DWORD *reserved,DWORD *type,void *data,DWORD *size,void *ft,bool key,bool ansi)
{
    PE_STATE_GUARD; if(reserved)return 87; struct pe_reg_node *n=pe_reg_lookup(h,key?8:1); if(!n)return pe_reg_error(h);
    DWORD saved=GetLastError(),r;
    if(key) {
        n=n->children; while(n&&index--)n=n->next; if(!n)return 259;
        r=pe_reg_name_copy(n->name,name,len,ansi);
        if(ft)memset(ft,0,8);
        if(data&&!size)r=87;
        if(size) { DWORD cap=*size; *size=0; if(data) {if(!cap)r=234;else memset(data,0,ansi?1:2);} }
    } else {
        struct pe_reg_value *v=n->values;while(v&&index--)v=v->next;if(!v)return 259;
        r=pe_reg_name_copy(v->name,name,len,ansi); DWORD q=pe_reg_value_copy(v,type,data,size,ansi);if(q)r=q;
    }
    SetLastError(saved);return r;
}
static DWORD pe_reg_info(HANDLE h,void *cls,DWORD *clen,DWORD *reserved,DWORD *sub,DWORD *maxsub,DWORD *maxcls,DWORD *vals,DWORD *maxname,DWORD *maxdata,DWORD *security,void *ft,bool ansi)
{
    PE_STATE_GUARD; if(reserved||(cls&&!clen))return 87;
    struct pe_reg_node *n=pe_reg_lookup(h,1);if(!n)return pe_reg_error(h);
    DWORD saved=GetLastError(),nc=0,nv=0,mc=0,mn=0,md=0;
    for(struct pe_reg_node *c=n->children;c;c=c->next){nc++;DWORD l=ansi?WideCharToMultiByte(65001,0,c->name,-1,NULL,0,NULL,NULL)-1:lstrlenW(c->name);if(l>mc)mc=l;}
    for(struct pe_reg_value *v=n->values;v;v=v->next){nv++;DWORD l=ansi?WideCharToMultiByte(65001,0,v->name,-1,NULL,0,NULL,NULL)-1:lstrlenW(v->name),bytes=0;pe_reg_value_copy(v,NULL,NULL,&bytes,ansi);if(l>mn)mn=l;if(bytes>md)md=bytes;}
    if(sub)*sub=nc;if(maxsub)*maxsub=mc;if(maxcls)*maxcls=0;if(vals)*vals=nv;if(maxname)*maxname=mn;if(maxdata)*maxdata=md;
    if(security)*security=0;if(ft)memset(ft,0,8);
    DWORD r=0;if(clen){DWORD cap=*clen;*clen=0;if(cls){if(!cap)r=234;else memset(cls,0,ansi?1:2);}}
    SetLastError(saved);return r;
}
static DWORD pe_reg_delete(HANDLE h,const void *name,bool key,bool ansi)
{
    PE_STATE_GUARD;struct pe_reg_node *n=pe_reg_lookup(h,key?4:2);if(!n)return pe_reg_error(h);
    DWORD saved=GetLastError(),r=2;uint16_t *w=pe_reg_string(name,ansi);if(!w){SetLastError(saved);return 8;}
    if(key){ bool fresh;struct pe_reg_node *c=pe_reg_walk(n,w,false,&fresh);
        if(c && c!=n && c->parent){if(c->children)r=5;else{struct pe_reg_node **p=&c->parent->children;while(*p&&*p!=c)p=&(*p)->next;if(*p)*p=c->next;c->deleted=true;r=0;}}
    }else{struct pe_reg_value **p=&n->values;while(*p&&!pe_reg_equal((*p)->name,w))p=&(*p)->next;if(*p){struct pe_reg_value *v=*p;*p=v->next;pe_reg_drop_string(v->name);raw_syscall3(SYS_munmap,(long)v->data,v->allocation,0);raw_syscall3(SYS_munmap,(long)v,sizeof(*v),0);r=0;}}
    pe_reg_drop_string(w);SetLastError(saved);return r;
}
__attribute__((ms_abi)) DWORD RegOpenKeyExW(HANDLE h,const uint16_t *s,DWORD o,DWORD a,HANDLE *v)
{ return pe_reg_open(h,s,o,a,v,NULL,false,false); }
__attribute__((ms_abi)) DWORD RegCreateKeyExW(HANDLE h,const uint16_t *s,DWORD reserved,uint16_t *cls,DWORD o,DWORD a,void *security,HANDLE *v,DWORD *d)
{ return reserved?87:pe_reg_open(h,s,o,a,v,d,true,false); }
__attribute__((ms_abi)) DWORD RegQueryValueExW(HANDLE h,const uint16_t *n,DWORD *r,DWORD *t,void *d,DWORD *z)
{ return pe_reg_query(h,n,r,t,d,z,false); }
__attribute__((ms_abi)) DWORD RegSetValueExW(HANDLE h,const uint16_t *n,DWORD r,DWORD t,const void *d,DWORD z)
{ return pe_reg_write(h,n,r,t,d,z,false); }
__attribute__((ms_abi)) DWORD RegDeleteKeyW(HANDLE h,const uint16_t *n)
{ return pe_reg_delete(h,n,true,false); }
__attribute__((ms_abi)) DWORD RegDeleteValueW(HANDLE h,const uint16_t *n)
{ return pe_reg_delete(h,n,false,false); }
__attribute__((ms_abi)) DWORD RegEnumKeyExW(HANDLE h,DWORD i,uint16_t *n,DWORD *l,DWORD *r,uint16_t *c,DWORD *z,void *ft)
{ return pe_reg_enum(h,i,n,l,r,NULL,c,z,ft,true,false); }
__attribute__((ms_abi)) DWORD RegEnumValueW(HANDLE h,DWORD i,uint16_t *n,DWORD *l,DWORD *r,DWORD *t,void *d,DWORD *z)
{ return pe_reg_enum(h,i,n,l,r,t,d,z,NULL,false,false); }
__attribute__((ms_abi)) DWORD RegQueryInfoKeyW(HANDLE h,uint16_t *c,DWORD *l,DWORD *r,DWORD *s,DWORD *ms,DWORD *mc,DWORD *v,DWORD *mn,DWORD *md,DWORD *sd,void *ft)
{ return pe_reg_info(h,c,l,r,s,ms,mc,v,mn,md,sd,ft,false); }
__attribute__((ms_abi)) DWORD RegGetValueW(HANDLE h,const uint16_t *sub,const uint16_t *name,DWORD flags,DWORD *type,void *data,DWORD *size)
{
    PE_STATE_GUARD; DWORD saved=GetLastError(),cap=size?*size:0,r=0,t=0;
    if(flags&~0x3001ffffu || ((flags&0x30000u)==0x30000u))r=87;
    HANDLE opened=NULL;
    if(!r)r=pe_reg_open(h,sub,0,1,&opened,NULL,false,false);
    if(!r) {
        r=pe_reg_query(opened,name,NULL,&t,NULL,NULL,false);
        DWORD mask=flags&0xffffu;
        if(!r && mask && mask!=0xffffu && !(mask & (t==1?2:t==2?4:t==3?8:t==4?16:t==7?32:t==11?64:1)))r=1630;
        /* Environment is empty in this profile: expanding REG_EXPAND_SZ leaves
         * unresolved references unchanged, as ExpandEnvironmentStrings does. */
        if(!r)r=pe_reg_query(opened,name,NULL,&t,data,size,false);
        if(type)*type=t==2&&!(flags&0x10000000u)?1:t;
        RegCloseKey(opened);
    }
    if(r&&(flags&0x20000000u)&&data&&size)memset(data,0,cap);
    SetLastError(saved);return r;
}
__attribute__((ms_abi)) DWORD RegOpenKeyExA(HANDLE h,const char *s,DWORD o,DWORD a,HANDLE *v)
{ return pe_reg_open(h,s,o,a,v,NULL,false,true); }
__attribute__((ms_abi)) DWORD RegCreateKeyExA(HANDLE h,const char *s,DWORD reserved,char *cls,DWORD o,DWORD a,void *security,HANDLE *v,DWORD *d)
{ return reserved?87:pe_reg_open(h,s,o,a,v,d,true,true); }
__attribute__((ms_abi)) DWORD RegQueryValueExA(HANDLE h,const char *n,DWORD *r,DWORD *t,void *d,DWORD *z)
{ return pe_reg_query(h,n,r,t,d,z,true); }
__attribute__((ms_abi)) DWORD RegSetValueExA(HANDLE h,const char *n,DWORD r,DWORD t,const void *d,DWORD z)
{ return pe_reg_write(h,n,r,t,d,z,true); }
__attribute__((ms_abi)) DWORD RegDeleteKeyA(HANDLE h,const char *n)
{ return pe_reg_delete(h,n,true,true); }
__attribute__((ms_abi)) DWORD RegDeleteValueA(HANDLE h,const char *n)
{ return pe_reg_delete(h,n,false,true); }
__attribute__((ms_abi)) DWORD RegEnumKeyExA(HANDLE h,DWORD i,char *n,DWORD *l,DWORD *r,char *c,DWORD *z,void *ft)
{ return pe_reg_enum(h,i,n,l,r,NULL,c,z,ft,true,true); }
__attribute__((ms_abi)) DWORD RegEnumValueA(HANDLE h,DWORD i,char *n,DWORD *l,DWORD *r,DWORD *t,void *d,DWORD *z)
{ return pe_reg_enum(h,i,n,l,r,t,d,z,NULL,false,true); }
__attribute__((ms_abi)) DWORD RegQueryInfoKeyA(HANDLE h,char *c,DWORD *l,DWORD *r,DWORD *s,DWORD *ms,DWORD *mc,DWORD *v,DWORD *mn,DWORD *md,DWORD *sd,void *ft)
{ return pe_reg_info(h,c,l,r,s,ms,mc,v,mn,md,sd,ft,true); }
__attribute__((ms_abi)) DWORD RegGetValueA(HANDLE h,const char *sub,const char *name,DWORD flags,DWORD *type,void *data,DWORD *size)
{
    PE_STATE_GUARD; DWORD saved=GetLastError(),cap=size?*size:0,r=0,t=0;
    if(flags&~0x3001ffffu || ((flags&0x30000u)==0x30000u))r=87;
    HANDLE opened=NULL;
    if(!r)r=pe_reg_open(h,sub,0,1,&opened,NULL,false,true);
    if(!r) {
        r=pe_reg_query(opened,name,NULL,&t,NULL,NULL,true);
        DWORD mask=flags&0xffffu;
        if(!r && mask && mask!=0xffffu && !(mask & (t==1?2:t==2?4:t==3?8:t==4?16:t==7?32:t==11?64:1)))r=1630;
        /* Environment is empty in this profile: expanding REG_EXPAND_SZ leaves
         * unresolved references unchanged, as ExpandEnvironmentStrings does. */
        if(!r)r=pe_reg_query(opened,name,NULL,&t,data,size,true);
        if(type)*type=t==2&&!(flags&0x10000000u)?1:t;
        RegCloseKey(opened);
    }
    if(r&&(flags&0x20000000u)&&data&&size)memset(data,0,cap);
    SetLastError(saved);return r;
}

#include "bridge/resources.inc"
#include "gui/bridge.inc"
#include "gui/window_state.inc"
#include "gui/properties.inc"
#include "gui/subclass.inc"
#include "gui/send.inc"
#include "gui/controls.inc"
#include "gui/menus.inc"
#include "gui/caret.inc"
#include "bridge/crypto.inc"
#include "bridge/colors.inc"
#include "bridge/directories.inc"
#include "bridge/security.inc"
#include "bridge/trust.inc"
#include "bridge/init_once.inc"

static void *lookup_shim(const char *dll, const char *function, uint16_t ordinal,
		        bool by_ordinal)
{
    if (!by_ordinal && ascii_equal_ci(function, "GetCaretBlinkTime")) return GetCaretBlinkTime;
    if (!by_ordinal && ascii_equal_ci(function, "SetCaretBlinkTime")) return SetCaretBlinkTime;
    if (!by_ordinal && ascii_equal_ci(function, "CreateCaret")) return CreateCaret;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyCaret")) return DestroyCaret;
    if (!by_ordinal && ascii_equal_ci(function, "ShowCaret")) return ShowCaret;
    if (!by_ordinal && ascii_equal_ci(function, "HideCaret")) return HideCaret;
    if (!by_ordinal && ascii_equal_ci(function, "SetCaretPos")) return SetCaretPos;
    if (!by_ordinal && ascii_equal_ci(function, "GetCaretPos")) return GetCaretPos;

    if (!by_ordinal) {
        if (ascii_equal_ci(function, "InitOnceInitialize")) return InitOnceInitialize;
        if (ascii_equal_ci(function, "InitOnceBeginInitialize")) return InitOnceBeginInitialize;
        if (ascii_equal_ci(function, "InitOnceComplete")) return InitOnceComplete;
        if (ascii_equal_ci(function, "InitOnceExecuteOnce")) return InitOnceExecuteOnce;
    }

    if (!by_ordinal && ascii_equal_ci(function, "RemoveMenu")) return RemoveMenu;
    if (!by_ordinal && ascii_equal_ci(function, "DeleteMenu")) return DeleteMenu;
    if (!by_ordinal && ascii_equal_ci(function, "InsertMenuW")) return InsertMenuW;
    if (!by_ordinal && ascii_equal_ci(function, "ModifyMenuW")) return ModifyMenuW;
    if (!by_ordinal && ascii_equal_ci(function, "AppendMenuA")) return AppendMenuA;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenuState")) return GetMenuState;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenuStringW")) return GetMenuStringW;
    if (!by_ordinal && ascii_equal_ci(function, "CheckMenuRadioItem")) return CheckMenuRadioItem;
    if (!by_ordinal && ascii_equal_ci(function, "SetMenuItemBitmaps")) return SetMenuItemBitmaps;
    if (!by_ordinal && ascii_equal_ci(function, "CreateAcceleratorTableW")) return CreateAcceleratorTableW;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyAcceleratorTable")) return DestroyAcceleratorTable;
    if (!by_ordinal && ascii_equal_ci(function, "FindResourceW")) return FindResourceW;
    if (!by_ordinal && ascii_equal_ci(function, "FindResourceA")) return FindResourceA;
    if (!by_ordinal && ascii_equal_ci(function, "FindResourceExW")) return FindResourceExW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadResource")) return LoadResource;
    if (!by_ordinal && ascii_equal_ci(function, "LockResource")) return LockResource;
    if (!by_ordinal && ascii_equal_ci(function, "SizeofResource")) return SizeofResource;
    if (!by_ordinal && ascii_equal_ci(function, "LoadStringW")) return LoadStringW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadStringA")) return LoadStringA;
    if (!by_ordinal && ascii_equal_ci(function, "EnumResourceNamesW")) return EnumResourceNamesW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadBitmapW")) return LoadBitmapW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadMenuW")) return LoadMenuW;
    if (!by_ordinal && ascii_equal_ci(function, "LoadAcceleratorsW")) return LoadAcceleratorsW;
    if (!by_ordinal && ascii_equal_ci(function, "TranslateAcceleratorW")) return TranslateAcceleratorW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateMenu")) return CreateMenu;
    if (!by_ordinal && ascii_equal_ci(function, "CreatePopupMenu")) return CreatePopupMenu;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyMenu")) return DestroyMenu;
    if (!by_ordinal && ascii_equal_ci(function, "SetMenu")) return SetMenu;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenu")) return GetMenu;
    if (!by_ordinal && ascii_equal_ci(function, "GetSubMenu")) return GetSubMenu;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenuItemCount")) return GetMenuItemCount;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenuItemID")) return GetMenuItemID;
    if (!by_ordinal && ascii_equal_ci(function, "AppendMenuW")) return AppendMenuW;
    if (!by_ordinal && ascii_equal_ci(function, "InsertMenuItemW")) return InsertMenuItemW;
    if (!by_ordinal && ascii_equal_ci(function, "GetMenuItemInfoW")) return GetMenuItemInfoW;
    if (!by_ordinal && ascii_equal_ci(function, "SetMenuItemInfoW")) return SetMenuItemInfoW;
    if (!by_ordinal && ascii_equal_ci(function, "CheckMenuItem")) return CheckMenuItem;
    if (!by_ordinal && ascii_equal_ci(function, "EnableMenuItem")) return EnableMenuItem;
    if (!by_ordinal && ascii_equal_ci(function, "DrawMenuBar")) return DrawMenuBar;
    if (!by_ordinal && ascii_equal_ci(function, "AdjustWindowRect")) return AdjustWindowRect;
    if (!by_ordinal && ascii_equal_ci(function, "AdjustWindowRectEx")) return AdjustWindowRectEx;
    if (!by_ordinal && ascii_equal_ci(function,"InitCommonControlsEx")) return InitCommonControlsEx;
    if (!by_ordinal && ascii_equal_ci(function,"InitCommonControls")) return InitCommonControls;

    if (!by_ordinal && ascii_equal_ci(function, "SetWindowSubclass")) return SetWindowSubclass;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowSubclass")) return GetWindowSubclass;
    if (!by_ordinal && ascii_equal_ci(function, "RemoveWindowSubclass")) return RemoveWindowSubclass;
    if (!by_ordinal && ascii_equal_ci(function, "DefSubclassProc")) return DefSubclassProc;
    if(by_ordinal && ascii_equal_ci(dll,"comctl32.dll")) {
        switch(ordinal){case 410:return SetWindowSubclass;case 411:return GetWindowSubclass;case 412:return RemoveWindowSubclass;case 413:return DefSubclassProc;}
    }

    if (!by_ordinal && ascii_equal_ci(function, "SetPropW")) return SetPropW;
    if (!by_ordinal && ascii_equal_ci(function, "SetPropA")) return SetPropA;
    if (!by_ordinal && ascii_equal_ci(function, "GetPropW")) return GetPropW;
    if (!by_ordinal && ascii_equal_ci(function, "GetPropA")) return GetPropA;
    if (!by_ordinal && ascii_equal_ci(function, "RemovePropW")) return RemovePropW;
    if (!by_ordinal && ascii_equal_ci(function, "RemovePropA")) return RemovePropA;
    if (!by_ordinal && ascii_equal_ci(function, "SetWindowPos")) return SetWindowPos;
    if (!by_ordinal && ascii_equal_ci(function, "MoveWindow")) return MoveWindow;
    if (!by_ordinal && ascii_equal_ci(function, "EnableWindow")) return EnableWindow;
    if (!by_ordinal && ascii_equal_ci(function, "IsWindowEnabled")) return IsWindowEnabled;
    if (!by_ordinal && ascii_equal_ci(function, "SetFocus")) return SetFocus;
    if (!by_ordinal && ascii_equal_ci(function, "GetFocus")) return GetFocus;
    if (!by_ordinal && ascii_equal_ci(function, "SetActiveWindow")) return SetActiveWindow;
    if (!by_ordinal && ascii_equal_ci(function, "GetActiveWindow")) return GetActiveWindow;
    if (!by_ordinal && ascii_equal_ci(function, "DragAcceptFiles")) return DragAcceptFiles;
    if (!by_ordinal && ascii_equal_ci(function, "GetParent")) return GetParent;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowLongW")) return GetWindowLongW;
    if (!by_ordinal && ascii_equal_ci(function, "SetWindowLongW")) return SetWindowLongW;
    if (!by_ordinal && ascii_equal_ci(function, "WinVerifyTrust")) return WinVerifyTrust;
    if (!by_ordinal && ascii_equal_ci(function, "AllocateAndInitializeSid")) return AllocateAndInitializeSid;
    if (!by_ordinal && ascii_equal_ci(function, "FreeSid")) return FreeSid;
    if (!by_ordinal && ascii_equal_ci(function, "IsValidSid")) return IsValidSid;
    if (!by_ordinal && ascii_equal_ci(function, "GetLengthSid")) return GetLengthSid;
    if (!by_ordinal && ascii_equal_ci(function, "CopySid")) return CopySid;
    if (!by_ordinal && ascii_equal_ci(function, "EqualSid")) return EqualSid;
    if (!by_ordinal && ascii_equal_ci(function, "GetSidSubAuthority")) return GetSidSubAuthority;
    if (!by_ordinal && ascii_equal_ci(function, "GetSidSubAuthorityCount")) return GetSidSubAuthorityCount;
    if (!by_ordinal && ascii_equal_ci(function, "GetSidIdentifierAuthority")) return GetSidIdentifierAuthority;
    if (!by_ordinal && ascii_equal_ci(function, "CheckTokenMembership")) return CheckTokenMembership;
    if (!by_ordinal && ascii_equal_ci(function, "OpenProcessToken")) return OpenProcessToken;
    if (!by_ordinal && ascii_equal_ci(function, "OpenThreadToken")) return OpenThreadToken;
    if (!by_ordinal && ascii_equal_ci(function, "GetTokenInformation")) return GetTokenInformation;
    if (!by_ordinal && ascii_equal_ci(function, "MulDiv")) return MulDiv;
    if (!by_ordinal && ascii_equal_ci(function, "SetCurrentDirectoryW")) return SetCurrentDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "SetCurrentDirectoryA")) return SetCurrentDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "CreateDirectoryW")) return CreateDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateDirectoryA")) return CreateDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "RemoveDirectoryW")) return RemoveDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "RemoveDirectoryA")) return RemoveDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "GetFullPathNameW")) return GetFullPathNameW;
    if (!by_ordinal && ascii_equal_ci(function, "GetFullPathNameA")) return GetFullPathNameA;
    if (!by_ordinal && ascii_equal_ci(function, "DwmGetColorizationColor")) return DwmGetColorizationColor;
    if (!by_ordinal && ascii_equal_ci(function, "DwmIsCompositionEnabled")) return DwmIsCompositionEnabled;
    if (!by_ordinal && ascii_equal_ci(function, "IsThemeActive")) return IsThemeActive;
    if (!by_ordinal && ascii_equal_ci(function, "IsAppThemed")) return IsAppThemed;
    if (!by_ordinal && ascii_equal_ci(function, "GetThemeAppProperties")) return GetThemeAppProperties;
    if (!by_ordinal && ascii_equal_ci(function, "ColorRGBToHLS")) return ColorRGBToHLS;
    if (!by_ordinal && ascii_equal_ci(function, "ColorHLSToRGB")) return ColorHLSToRGB;
    if (!by_ordinal && ascii_equal_ci(function, "ColorAdjustLuma")) return ColorAdjustLuma;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptOpenAlgorithmProvider")) return BCryptOpenAlgorithmProvider;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptCloseAlgorithmProvider")) return BCryptCloseAlgorithmProvider;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptGetProperty")) return BCryptGetProperty;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptCreateHash")) return BCryptCreateHash;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptHashData")) return BCryptHashData;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptFinishHash")) return BCryptFinishHash;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptDestroyHash")) return BCryptDestroyHash;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptDuplicateHash")) return BCryptDuplicateHash;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptHash")) return BCryptHash;
    if (!by_ordinal && ascii_equal_ci(function, "BCryptGenRandom")) return BCryptGenRandom;
    if (!by_ordinal && ascii_equal_ci(function, "CreateWindowExW")) return CreateWindowExW;
    if (!by_ordinal && ascii_equal_ci(function, "DestroyWindow")) return DestroyWindow;
    if (!by_ordinal && ascii_equal_ci(function, "DefWindowProcW")) return DefWindowProcW;
    if (!by_ordinal && ascii_equal_ci(function, "CallWindowProcW")) return CallWindowProcW;
    if (!by_ordinal && ascii_equal_ci(function, "SendMessageW")) return SendMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "PostMessageW")) return PostMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "PostThreadMessageW")) return PostThreadMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "PostQuitMessage")) return PostQuitMessage;
    if (!by_ordinal && ascii_equal_ci(function, "ShowWindow")) return ShowWindow;
    if (!by_ordinal && ascii_equal_ci(function, "IsWindow")) return IsWindow;
    if (!by_ordinal && ascii_equal_ci(function, "IsWindowVisible")) return IsWindowVisible;
    if (!by_ordinal && ascii_equal_ci(function, "IsWindowUnicode")) return IsWindowUnicode;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowThreadProcessId")) return GetWindowThreadProcessId;
    if (!by_ordinal && ascii_equal_ci(function, "GetClientRect")) return GetClientRect;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowRect")) return GetWindowRect;
    if (!by_ordinal && ascii_equal_ci(function, "SetWindowTextW")) return SetWindowTextW;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowTextW")) return GetWindowTextW;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowTextLengthW")) return GetWindowTextLengthW;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowLongPtrW")) return GetWindowLongPtrW;
    if (!by_ordinal && ascii_equal_ci(function, "SetWindowLongPtrW")) return SetWindowLongPtrW;
    if (!by_ordinal && ascii_equal_ci(function, "InvalidateRect")) return InvalidateRect;
    if (!by_ordinal && ascii_equal_ci(function, "ValidateRect")) return ValidateRect;
    if (!by_ordinal && ascii_equal_ci(function, "GetMessageW")) return GetMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "PeekMessageW")) return PeekMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "TranslateMessage")) return TranslateMessage;
    if (!by_ordinal && ascii_equal_ci(function, "DispatchMessageW")) return DispatchMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "UpdateWindow")) return UpdateWindow;
    if (!by_ordinal && ascii_equal_ci(function, "BeginPaint")) return BeginPaint;
    if (!by_ordinal && ascii_equal_ci(function, "EndPaint")) return EndPaint;
    if (!by_ordinal && ascii_equal_ci(function, "FillRect")) return FillRect;
    if (!by_ordinal && ascii_equal_ci(function, "SetTextColor")) return SetTextColor;
    if (!by_ordinal && ascii_equal_ci(function, "SetBkColor")) return SetBkColor;
    if (!by_ordinal && ascii_equal_ci(function, "SetBkMode")) return SetBkMode;
    if (!by_ordinal && ascii_equal_ci(function, "TextOutW")) return TextOutW;
    if (!by_ordinal && ascii_equal_ci(function, "GetPixel")) return GetPixel;
    if (!by_ordinal && ascii_equal_ci(function, "SetPixel")) return SetPixel;
    if (!by_ordinal && ascii_equal_ci(function, "MoveToEx")) return MoveToEx;
    if (!by_ordinal && ascii_equal_ci(function, "LineTo")) return LineTo;
    if (!by_ordinal && ascii_equal_ci(function, "Rectangle")) return Rectangle;
    if (!by_ordinal && ascii_equal_ci(function, "RegCloseKey")) return RegCloseKey;
    if (!by_ordinal && ascii_equal_ci(function, "RegFlushKey")) return RegFlushKey;
    if (!by_ordinal && ascii_equal_ci(function, "RegOpenKeyExW")) return RegOpenKeyExW;
    if (!by_ordinal && ascii_equal_ci(function, "RegOpenKeyExA")) return RegOpenKeyExA;
    if (!by_ordinal && ascii_equal_ci(function, "RegCreateKeyExW")) return RegCreateKeyExW;
    if (!by_ordinal && ascii_equal_ci(function, "RegCreateKeyExA")) return RegCreateKeyExA;
    if (!by_ordinal && ascii_equal_ci(function, "RegQueryValueExW")) return RegQueryValueExW;
    if (!by_ordinal && ascii_equal_ci(function, "RegQueryValueExA")) return RegQueryValueExA;
    if (!by_ordinal && ascii_equal_ci(function, "RegSetValueExW")) return RegSetValueExW;
    if (!by_ordinal && ascii_equal_ci(function, "RegSetValueExA")) return RegSetValueExA;
    if (!by_ordinal && ascii_equal_ci(function, "RegDeleteKeyW")) return RegDeleteKeyW;
    if (!by_ordinal && ascii_equal_ci(function, "RegDeleteKeyA")) return RegDeleteKeyA;
    if (!by_ordinal && ascii_equal_ci(function, "RegDeleteValueW")) return RegDeleteValueW;
    if (!by_ordinal && ascii_equal_ci(function, "RegDeleteValueA")) return RegDeleteValueA;
    if (!by_ordinal && ascii_equal_ci(function, "RegEnumKeyExW")) return RegEnumKeyExW;
    if (!by_ordinal && ascii_equal_ci(function, "RegEnumKeyExA")) return RegEnumKeyExA;
    if (!by_ordinal && ascii_equal_ci(function, "RegEnumValueW")) return RegEnumValueW;
    if (!by_ordinal && ascii_equal_ci(function, "RegEnumValueA")) return RegEnumValueA;
    if (!by_ordinal && ascii_equal_ci(function, "RegQueryInfoKeyW")) return RegQueryInfoKeyW;
    if (!by_ordinal && ascii_equal_ci(function, "RegQueryInfoKeyA")) return RegQueryInfoKeyA;
    if (!by_ordinal && ascii_equal_ci(function, "RegGetValueW")) return RegGetValueW;
    if (!by_ordinal && ascii_equal_ci(function, "RegGetValueA")) return RegGetValueA;
    if (!by_ordinal && ascii_equal_ci(function, "PathIsNetworkPathW")) return PathIsNetworkPathW;
    if (!by_ordinal && ascii_equal_ci(function, "PathIsNetworkPathA")) return PathIsNetworkPathA;
    if (!by_ordinal && ascii_equal_ci(function, "GetDriveTypeW")) return GetDriveTypeW;
    if (!by_ordinal && ascii_equal_ci(function, "GetDriveTypeA")) return GetDriveTypeA;
    if (!by_ordinal && ascii_equal_ci(function, "GetDiskFreeSpaceExW")) return GetDiskFreeSpaceExW;
    if (!by_ordinal && ascii_equal_ci(function, "GetDiskFreeSpaceExA")) return GetDiskFreeSpaceExA;
    if (!by_ordinal && ascii_equal_ci(function, "GetDiskFreeSpaceW")) return GetDiskFreeSpaceW;
    if (!by_ordinal && ascii_equal_ci(function, "GetDiskFreeSpaceA")) return GetDiskFreeSpaceA;
    if (!by_ordinal && ascii_equal_ci(function, "FindFirstFileW")) return FindFirstFileW;
    if (!by_ordinal && ascii_equal_ci(function, "FindFirstFileA")) return FindFirstFileA;
    if (!by_ordinal && ascii_equal_ci(function, "FindFirstFileExW")) return FindFirstFileExW;
    if (!by_ordinal && ascii_equal_ci(function, "FindFirstFileExA")) return FindFirstFileExA;
    if (!by_ordinal && ascii_equal_ci(function, "FindNextFileW")) return FindNextFileW;
    if (!by_ordinal && ascii_equal_ci(function, "FindNextFileA")) return FindNextFileA;
    if (!by_ordinal && ascii_equal_ci(function, "FindClose")) return FindClose;
    if (!by_ordinal && ascii_equal_ci(function, "SystemTimeToTzSpecificLocalTime")) return SystemTimeToTzSpecificLocalTime;
    if (!by_ordinal && ascii_equal_ci(function, "TzSpecificLocalTimeToSystemTime")) return TzSpecificLocalTimeToSystemTime;
    if (!by_ordinal && ascii_equal_ci(function, "FileTimeToSystemTime")) return FileTimeToSystemTime;
    if (!by_ordinal && ascii_equal_ci(function, "SystemTimeToFileTime")) return SystemTimeToFileTime;
    if (!by_ordinal && ascii_equal_ci(function, "CompareFileTime")) return CompareFileTime;
    if (!by_ordinal && ascii_equal_ci(function, "GetSystemTime")) return GetSystemTime;
    if (!by_ordinal && ascii_equal_ci(function, "GetLocalTime")) return GetLocalTime;
    if (!by_ordinal && ascii_equal_ci(function, "FileTimeToLocalFileTime")) return FileTimeToLocalFileTime;
    if (!by_ordinal && ascii_equal_ci(function, "LocalFileTimeToFileTime")) return LocalFileTimeToFileTime;
    if (!by_ordinal && ascii_equal_ci(function, "GetTimeZoneInformation")) return GetTimeZoneInformation;
    if (!by_ordinal && ascii_equal_ci(function, "CopyFileW")) return CopyFileW;
    if (!by_ordinal && ascii_equal_ci(function, "CopyFileA")) return CopyFileA;
    if (!by_ordinal && ascii_equal_ci(function, "DeleteFileA")) return DeleteFileA;
    if (!by_ordinal && ascii_equal_ci(function, "FlushFileBuffers")) return FlushFileBuffers;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileInformationByHandle")) return GetFileInformationByHandle;
    if (!by_ordinal && ascii_equal_ci(function, "SHGetFolderPathW")) return SHGetFolderPathW;
    if (!by_ordinal && ascii_equal_ci(function, "SHGetFolderPathA")) return SHGetFolderPathA;
    if (!by_ordinal && ascii_equal_ci(function, "SHGetSpecialFolderPathW")) return SHGetSpecialFolderPathW;
    if (!by_ordinal && ascii_equal_ci(function, "SHGetSpecialFolderPathA")) return SHGetSpecialFolderPathA;
    if (!by_ordinal && ascii_equal_ci(function, "GetSystemDirectoryW")) return GetSystemDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "GetSystemDirectoryA")) return GetSystemDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowsDirectoryW")) return GetWindowsDirectoryW;
    if (!by_ordinal && ascii_equal_ci(function, "GetWindowsDirectoryA")) return GetWindowsDirectoryA;
    if (!by_ordinal && ascii_equal_ci(function, "GetTempPathW")) return GetTempPathW;
    if (!by_ordinal && ascii_equal_ci(function, "GetTempPathA")) return GetTempPathA;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterWindowMessageW")) return RegisterWindowMessageW;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterWindowMessageA")) return RegisterWindowMessageA;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterClipboardFormatW")) return RegisterClipboardFormatW;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterClipboardFormatA")) return RegisterClipboardFormatA;
    if (!by_ordinal && ascii_equal_ci(function, "GetClipboardFormatNameW")) return GetClipboardFormatNameW;
    if (!by_ordinal && ascii_equal_ci(function, "GetClipboardFormatNameA")) return GetClipboardFormatNameA;
    if (!by_ordinal && ascii_equal_ci(function, "RegisterClassW")) return RegisterClassW;
    if (!by_ordinal && ascii_equal_ci(function, "GetClassInfoW")) return GetClassInfoW;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcpynA")) return lstrcpynA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcpynW")) return lstrcpynW;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcpyA")) return lstrcpyA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcpyW")) return lstrcpyW;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcatA")) return lstrcatA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcatW")) return lstrcatW;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcmpA")) return lstrcmpA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcmpW")) return lstrcmpW;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcmpiA")) return lstrcmpiA;
    if (!by_ordinal && ascii_equal_ci(function, "lstrcmpiW")) return lstrcmpiW;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileAttributesExW")) return GetFileAttributesExW;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileAttributesExA")) return GetFileAttributesExA;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileAttributesW")) return GetFileAttributesW;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileAttributesA")) return GetFileAttributesA;
    if (!by_ordinal && ascii_equal_ci(function, "CreateFileW")) return CreateFileW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateFileA")) return CreateFileA;
    if (!by_ordinal && ascii_equal_ci(function, "ReadFile")) return ReadFile;
    if (!by_ordinal && ascii_equal_ci(function, "SetFilePointerEx")) return SetFilePointerEx;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileSizeEx")) return GetFileSizeEx;
    if (!by_ordinal && ascii_equal_ci(function, "GetFileSize")) return GetFileSize;
    if (!by_ordinal && ascii_equal_ci(function, "DeleteFileW")) return DeleteFileW;
    if (!by_ordinal && ascii_equal_ci(function, "CloseHandle")) return CloseHandle;
    if (!by_ordinal && ascii_equal_ci(function, "WaitForMultipleObjectsEx")) return WaitForMultipleObjectsEx;
    if (!by_ordinal && ascii_equal_ci(function, "WaitForMultipleObjects")) return WaitForMultipleObjects;
    if (!by_ordinal && ascii_equal_ci(function, "WaitForSingleObjectEx")) return WaitForSingleObjectEx;
    if (!by_ordinal && ascii_equal_ci(function, "WaitForSingleObject")) return WaitForSingleObject;
    if (!by_ordinal && ascii_equal_ci(function, "CreateEventW")) return CreateEventW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateEventA")) return CreateEventA;
    if (!by_ordinal && ascii_equal_ci(function, "SetEvent")) return SetEvent;
    if (!by_ordinal && ascii_equal_ci(function, "ResetEvent")) return ResetEvent;
    if (!by_ordinal && ascii_equal_ci(function, "CreateMutexW")) return CreateMutexW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateMutexA")) return CreateMutexA;
    if (!by_ordinal && ascii_equal_ci(function, "ReleaseMutex")) return ReleaseMutex;
    if (!by_ordinal && ascii_equal_ci(function, "CreateSemaphoreW")) return CreateSemaphoreW;
    if (!by_ordinal && ascii_equal_ci(function, "CreateSemaphoreA")) return CreateSemaphoreA;
    if (!by_ordinal && ascii_equal_ci(function, "ReleaseSemaphore")) return ReleaseSemaphore;
    if (!by_ordinal && ascii_equal_ci(function, "SleepEx")) return SleepEx;
    if (!by_ordinal && ascii_equal_ci(function, "SwitchToThread")) return SwitchToThread;
    if (!by_ordinal && ascii_equal_ci(function, "InterlockedIncrement")) return InterlockedIncrement;
    if (!by_ordinal && ascii_equal_ci(function, "InterlockedDecrement")) return InterlockedDecrement;
    if (!by_ordinal && ascii_equal_ci(function, "InterlockedExchange")) return InterlockedExchange;
    if (!by_ordinal && ascii_equal_ci(function, "InterlockedCompareExchange")) return InterlockedCompareExchange;
    if (!by_ordinal && ascii_equal_ci(function, "CreateThread")) return CreateThread;
    if (!by_ordinal && ascii_equal_ci(function, "ResumeThread")) return ResumeThread;
    if (!by_ordinal && ascii_equal_ci(function, "GetExitCodeThread")) return GetExitCodeThread;
    if (!by_ordinal && ascii_equal_ci(function, "ExitThread")) return ExitThread;
    if (!by_ordinal && ascii_equal_ci(function, "PathRemoveFileSpecW")) return PathRemoveFileSpecW;
    if (!by_ordinal && ascii_equal_ci(function, "PathIsRelativeW")) return PathIsRelativeW;
    if (!by_ordinal && ascii_equal_ci(function, "PathIsUNCW")) return PathIsUNCW;
    if (!by_ordinal && ascii_equal_ci(function, "PathIsRootW")) return PathIsRootW;
    if (!by_ordinal && ascii_equal_ci(function, "PathGetDriveNumberW")) return PathGetDriveNumberW;
    if (!by_ordinal && ascii_equal_ci(function, "PathFindFileNameW")) return PathFindFileNameW;
    if (!by_ordinal && ascii_equal_ci(function, "PathFindExtensionW")) return PathFindExtensionW;
    if (!by_ordinal && ascii_equal_ci(function, "PathRemoveExtensionW")) return PathRemoveExtensionW;
    if (!by_ordinal && ascii_equal_ci(function, "PathAddBackslashW")) return PathAddBackslashW;
    if (!by_ordinal && ascii_equal_ci(function, "PathRemoveBackslashW")) return PathRemoveBackslashW;
    if (!by_ordinal && ascii_equal_ci(function, "GetVersionExW")) return GetVersionExW;
    if (!by_ordinal && ascii_equal_ci(function, "GetVersionExA")) return GetVersionExA;
    if (!by_ordinal && ascii_equal_ci(function, "RtlGetVersion")) return RtlGetVersion;
    if (!by_ordinal && ascii_equal_ci(function, "GetVersion")) return GetVersion;
    if (!by_ordinal && ascii_equal_ci(function, "GetSystemInfo")) return GetSystemInfo;
    if (!by_ordinal && ascii_equal_ci(function, "GetNativeSystemInfo")) return GetNativeSystemInfo;
    if (!by_ordinal && ascii_equal_ci(function, "GetCurrentProcessorNumber")) return GetCurrentProcessorNumber;
    if (!by_ordinal && ascii_equal_ci(function, "IsWow64Process")) return IsWow64Process;
    if (!by_ordinal && ascii_equal_ci(function, "IsWow64Process2")) return IsWow64Process2;
    if (!by_ordinal && ascii_equal_ci(function, "GetProcessAffinityMask")) return GetProcessAffinityMask;
    if (!by_ordinal && ascii_equal_ci(function, "InitializeSRWLock")) return InitializeSRWLock;
    if (!by_ordinal && ascii_equal_ci(function, "TryAcquireSRWLockExclusive")) return TryAcquireSRWLockExclusive;
    if (!by_ordinal && ascii_equal_ci(function, "TryAcquireSRWLockShared")) return TryAcquireSRWLockShared;
    if (!by_ordinal && ascii_equal_ci(function, "AcquireSRWLockExclusive")) return AcquireSRWLockExclusive;
    if (!by_ordinal && ascii_equal_ci(function, "AcquireSRWLockShared")) return AcquireSRWLockShared;
    if (!by_ordinal && ascii_equal_ci(function, "ReleaseSRWLockExclusive")) return ReleaseSRWLockExclusive;
    if (!by_ordinal && ascii_equal_ci(function, "ReleaseSRWLockShared")) return ReleaseSRWLockShared;
    if (!by_ordinal && ascii_equal_ci(function, "InitializeConditionVariable")) return InitializeConditionVariable;
    if (!by_ordinal && ascii_equal_ci(function, "WakeConditionVariable")) return WakeConditionVariable;
    if (!by_ordinal && ascii_equal_ci(function, "WakeAllConditionVariable")) return WakeAllConditionVariable;
    if (!by_ordinal && ascii_equal_ci(function, "SleepConditionVariableSRW")) return SleepConditionVariableSRW;
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
static struct pe_tls_directory64 pe_image_tls;
static bool pe_image_has_tls;
static bool pe_thread_tls_init(struct pe_thread *t)
{
    t->tls_array = pe_raw_map(64*sizeof(void *)); if (!t->tls_array) return false;
    if (pe_image_has_tls) {
        uint64_t raw_size = pe_image_tls.end-pe_image_tls.start;
        uint64_t size = (raw_size+pe_image_tls.zero_fill+15) & ~15ULL;
        if (!size) size = 16;
        unsigned code = (pe_image_tls.characteristics >> 20) & 15;
        uint64_t align = code ? 1ULL << (code-1) : 16;
        if (align < 16) align = 16;
        t->tls_size = size+align-1; t->tls_allocation = pe_raw_map(t->tls_size);
        if (!t->tls_allocation) return false;
        uint8_t *block = (void *)(((uintptr_t)t->tls_allocation+align-1) & ~(align-1));
        if (raw_size) memcpy(block, (void *)(uintptr_t)pe_image_tls.start, raw_size);
        memset(block+raw_size, 0, size-raw_size);
        ((void **)t->tls_array)[0] = block;
    }
    *(void **)(t->teb+0x58) = t->tls_array; return true;
}
static void pe_tls_notify(DWORD reason)
{
    if (!pe_image_has_tls || !pe_image_tls.callbacks) return;
    uintptr_t pos = pe_image_tls.callbacks;
    for (;;) {
        uint64_t callback;
        if (!diagnostic_image_read(pos, &callback, 8)) pe_loader_error("unterminated TLS callbacks");
        if (!callback) break;
        if (callback < pe_text_start || callback >= pe_text_end) pe_loader_error("TLS callback outside .text");
        typedef void (__attribute__((ms_abi)) *tls_callback)(void *, uint32_t, void *);
        ((tls_callback)(uintptr_t)callback)((void *)pe_main_base, reason, NULL);
        pos += 8;
    }
}
static void pe_setup_image_tls(uintptr_t base)
{
    uint32_t nt, count, dd[2]; uint16_t optional_size;
    if (!diagnostic_image_read(base+0x3c, &nt, 4) ||
        !diagnostic_image_read(base+nt+20, &optional_size, 2) || optional_size < 0x70 ||
        !diagnostic_image_read(base+nt+24+0x6c, &count, 4)) pe_loader_error("invalid TLS headers");
    if (count <= 9) {
        if (!pe_thread_tls_init(pe_current_thread())) pe_loader_error("main TLS allocation failed");
        return;
    }
    if (optional_size < 0x70+10*8 ||
        !diagnostic_image_read(base+nt+24+0x70+9*8, dd, sizeof(dd))) pe_loader_error("invalid TLS directory entry");
    if (!dd[0] && !dd[1]) {
        if (!pe_thread_tls_init(pe_current_thread())) pe_loader_error("main TLS allocation failed");
        return;
    }
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
    pe_image_tls = tls; pe_image_has_tls = true;
    if (!pe_thread_tls_init(pe_current_thread())) pe_loader_error("main TLS allocation failed");
    *(uint32_t *)(uintptr_t)tls.index = 0;
    raw_puts("[BRIDGE] TLS: template="); raw_hex(raw_size);
    raw_puts(" zero-fill="); raw_hex(tls.zero_fill); raw_puts(" alignment="); raw_hex(align); raw_puts("\n");
    pe_tls_notify(1);
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
    uintptr_t teb = pe_current_teb();
    long extra = raw_syscall6(SYS_mmap, teb+0x1000, 0x1000, 3, 0x100022, -1, 0);
    if (extra != (long)(teb+0x1000)) pe_loader_error("main TEB extension failed");
    *(uint64_t *)(teb+0x40) = GetCurrentProcessId();
    *(uint64_t *)(teb+0x48) = GetCurrentThreadId();
    if (!pe_init_fls(teb)) pe_loader_error("main FLS allocation failed");
    pe_main_thread_init();
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
