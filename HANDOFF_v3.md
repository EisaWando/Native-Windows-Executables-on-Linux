# binfmt_pe — HANDOFF v3 (supersedes v2)

Last updated: 2026-10-24. Audience: an autonomous AI coding agent (GPT-6.1-Sol) taking over a working prototype. Read this whole file, then read the source files it names before changing anything.

---

## 0. Who you work for and how

- The owner is a hobbyist developer (GitHub: EisaWando; co-developer Eisa404) who coordinates several AI agents. Project started 2026-08-24. Public repo: github.com/EisaWando/Native-Windows-Executables-on-Linux.
- Interactive shell is **fish** on CachyOS. Use `$status`, never `$?`, in anything you tell the owner to type. Put scripts in files and have them run `bash script.sh`.
- Tests that reload the kernel module need `sudo`. If sudo asks for a password you will hang. Ask the owner to run `sudo -v` first (it caches credentials for a while) or ask them to run the test and paste the output. Never invent test output.
- You may edit files, build, and run **non-sudo** commands freely. Commit locally in small, working steps with clear messages. Do **not** `git push` unless the owner asks.
- Be honest in reports: say what was verified by real output, and what is an assumption.

## 1. Mission

Run Windows x64 PE executables (`.exe`) **natively on Linux with no Wine/Proton/emulator**, via:

1. **`fs/binfmt_pe.c` → `binfmt_pe.ko`** (kernel binfmt handler). It validates MZ/PE headers, maps the PE headers and sections at the preferred ImageBase, sets up TEB/PEB/heap page, loads `win32_bridge.so` by its ELF `PT_LOAD` segments at `0x70000000`, builds the stack and `start_thread()`s into the bridge.
2. **`win32_bridge.c` → `win32_bridge.so`** (freestanding userland runtime, no libc, built `ET_EXEC` at `0x70000000`). It resolves the PE's imports to `ms_abi` shims implemented with raw Linux syscalls, then calls the PE entry point.

Motivation (for docs and tone): Linux users should be able to run Windows programs natively; the project criticizes Windows-the-OS and its terms, not every Microsoft service. The code is clean-room and legal: **no Microsoft source, no leaked code**. Reusing Wine/ReactOS code (LGPL/GPL) is a licensing and design decision for the owner; ask before copying any.

First big target: `npp.8.9.8.portable.x64/notepad++.exe` (Notepad++ 8.9.8 x64). Small regression targets live in `tests/`.

## 2. Environment and commands

- Workspace: `/home/eisa/Desktop/linux-7.1.6` (kernel source tree used as the build workspace). Running kernel: `7.2.11-1-cachyos` (clang-built, so **`LLVM=1`** is required). The module is built against the running kernel's headers.
- Module: `make LLVM=1 -C /lib/modules/$(uname -r)/build M=$PWD/fs modules`
- Bridge build (exact flags; keep them):
  ```
  gcc -fno-pic -no-pie -fno-stack-protector -static -nostdlib -ffreestanding -Wl,-Ttext-segment=0x70000000 -Wl,-e,_start -o win32_bridge.so win32_bridge.c
  ```
  Add `-DBRIDGE_VERBOSE` to print every IAT resolution (`BRIDGE: IAT dll!func -> addr`).
- `bash run2.sh` (in the tree root) = build bridge + build module + reload module (needs sudo) + run Notepad++ with stdout→`/tmp/out.txt`, stderr→`/tmp/err.txt`, then print exit code, first 25 stderr lines and the filtered kernel log. Kernel logs are prefixed `[BINFMT_PE]`; bridge stderr is prefixed `[BRIDGE]`/`BRIDGE:`.
- Reload sequence if doing it by hand: `lsmod | grep binfmt_pe`; `sudo rmmod binfmt_pe`; `sudo insmod fs/binfmt_pe.ko`. **Never `rmmod -f`.** `insmod ... File exists` just means it is already loaded. Chaining with `&&` after a failing `rmmod` skips `insmod`.
- The bridge is read from disk at exec time. Changing only `win32_bridge.c` needs a bridge rebuild but not a module reload; changing `binfmt_pe.c` needs a module rebuild + reload. (Check that the module finds the bridge by an absolute path; fix if it is relative to the cwd.)
- Test PEs (mingw-w64: `sudo pacman -S mingw-w64-gcc`):
  ```
  x86_64-w64-mingw32-gcc -O1 -fno-builtin -fno-tree-loop-distribute-patterns -nostdlib -ffreestanding -fno-stack-protector -Wl,--entry=mainCRTStartup -Wl,--subsystem,console -o tests/NAME.exe tests/NAME.c -lkernel32
  ```
- Debug a crash: the bridge installs a SIGSEGV/SIGILL/SIGBUS handler that dumps signal, fault address, registers, 16 stack qwords and `/proc/self/maps`. Disassemble with `objdump -d -M att win32_bridge.so --start-address=... --stop-address=...`. For crashes inside the PE use `objdump -d` on the .exe. Kernel segfault `error N` is **hex** (`0x14` = user instruction fetch from a non-present page; `0x6` = user write, page not present).
- **Safety:** a bug in the kernel module can hang or crash the machine. Prefer a VM for kernel changes, keep kernel changes small, commit before testing them, and keep everything else in userland.

## 3. Current state (verified by logs unless marked)

### Works
- Module registers as binfmt handler; MZ/PE (PE32+) parsing with bounds checks; data directories logged (Import `0x55ead0`/`0x1b8` for Notepad++); PE headers mapped read-only at `0x140000000`; all sections mapped (all anonymous RW copies, `.text` RWX; permissions not yet refined).
- TEB `0x7ff00000`, PEB `0x7ff00800`, legacy 1 MB heap page `0x7ff30000` (unused by the bridge heap). GS base verified: bridge prints `gs:30=0x7ff00000 gs:60=0x7ff00800 gs:10=0x7ffff6800000`. Bridge fills NT_TIB (StackBase 0x7ffff7000000, StackLimit 0x7ffff6800000, Self, PEB) and PEB.ImageBaseAddress / PEB.ProcessHeap if empty. (Not yet set: TEB ClientId, ThreadLocalStoragePointer `+0x58`.)
- Bridge loader: kernel maps the bridge's `PT_LOAD`s (ET_EXEC, absolute `e_entry`). Initial stack `user_sp = 0x7ffff6fff000`.
- Handoff registers into bridge `_start`: `RDI=PE entry, RSI=PEB, RDX=image base, RCX=import RVA, R8=import size`. `_start` calls the PE entry as `ms_abi`: `and $-16,%rsp; sub $32,%rsp; call *%r12` (**RSP%16==0 before the call; 32-byte shadow space**). The old `sub $40` was a real bug (misaligned stack made `movaps` in `ms_abi` prologues fault).
- Import resolver: walks import descriptors (by name and by ordinal); `lookup_shim()` returns a real shim or NULL; unknown imports get a generated stub that, **when called**, prints `[BRIDGE] CALLED UNIMPLEMENTED: dll!func ret=... rcx=... rdx=... r8=... r9=... w1="..." w2="..."` (UTF-16 previews of RCX/RDX) and exits 127.
- Milestone: **a real Windows `.exe` runs end to end** (`tests/hello.exe` prints `Hello from PE!`, exit 0; root `hello.exe` prints `Hello from a native PE on Linux!`, exit 42).

### Shims implemented in the bridge (all `__attribute__((ms_abi))`, registered in `lookup_shim()`, matched with `ascii_equal_ci`)
GetStdHandle, WriteFile, ExitProcess, GetSystemTimeAsFileTime, QueryPerformanceCounter/Frequency, GetCurrentProcessId, GetCurrentThreadId, SetLastError/GetLastError (`%gs:0x68`), LoadLibraryExW, GetModuleHandleW, GetProcAddress, InitializeCriticalSection / ...AndSpinCount / ...Ex, Enter/TryEnter/Leave/DeleteCriticalSection, TlsAlloc/Free/GetValue/SetValue and FlsAlloc/Free/GetValue/SetValue (shared single-thread slot arrays), VirtualProtect (mprotect; old protection reported as `PAGE_READWRITE`, an approximation), GetProcessHeap, HeapCreate, HeapDestroy, HeapAlloc, HeapFree, HeapSize, HeapReAlloc, HeapSetInformation. Helpers: `raw_syscall3/6`, `memset/memcpy/memmove/memcmp`, `raw_puts`, `raw_hex`.

### Design decisions already made (keep unless you have a reason)
- **File/std handles (2026-10-24):** `pe_fd_to_handle(fd)` encodes `0x40 + fd*4`; `pe_handle_to_fd(h)` validates alignment, descriptor range (<2^20), and liveness with F_GETFD. Standard handles are 0x40/0x44/0x48. Invalid file handles set error 6. No generation tracking yet; descriptor reuse can reuse a handle. This is separate from module, heap and pseudo handles.
- **Regression fixtures:** run `bash tests/run_all.sh`; all fixtures build from tracked sources, including `tests/hello42.c` as temporary `./hello.exe`. Owner-deleted root files remain deleted. Built test binaries are ignored.
- **Modules:** a small built-in registry maps names `kernel32(.dll)`, `kernelbase(.dll)`, `ntdll(.dll)` and any `api-ms-win-*` name to fake `HMODULE`s `0x6ff00000 + index*0x10000`. `LoadLibraryExW` of an unknown DLL → NULL + last error 126. `GetModuleHandleW(NULL)` → `0x140000000`.
- **GetProcAddress** returns NULL (+ last error 127) when there is no real shim. **Never return an "unimplemented" stub from it**: the static UCRT probes optional APIs and relies on NULL to select fallbacks.
- **Heap:** one fake handle `0x6ffe0000`; 16-byte block header `{usable, req}`, 16-byte-aligned payloads, power-of-two size classes from 1 MiB mmap arenas with per-class free lists, blocks >128 KiB get their own mmap. Single-threaded, no locks.
- **Critical sections / TLS / FLS:** single-threaded counters and arrays (`// TODO: real locks/TLS once CreateThread exists`).
- Approximations to revisit: VirtualProtect old protection, static command line `notepad++.exe`, empty environment block (if Task 8 was applied), no per-thread TLS block.

### Latest confirmed Notepad++ result
`imports: resolved 32, unimplemented 588`, then `CALLED UNIMPLEMENTED: KERNEL32.dll!GetStartupInfoW ret=0x14041a20a`. The CRT startup batch ("Batch A" below) was handed to the previous agent; **it may or may not be applied. Check `git status`, `git diff`, and `grep -n GetStartupInfoW win32_bridge.c` first.** Optional heap regression test `tests/heap.c` (64 mixed allocs, zero-flag checks, frees, HeapReAlloc growth with zeroed extension, one 300000-byte block; prints `heap ok` and exits 0) may or may not exist; create it if missing.

### Import profile of Notepad++ (620 imports)
USER32 216, KERNEL32 192, GDI32 66, ADVAPI32 26, COMCTL32 24, SHLWAPI 17, UxTheme 16, SHELL32 12, ole32 12, IMM32 9, CRYPT32 8, bcrypt 7, VERSION 3, SensApi 2, OLEAUT32 2, dwmapi 2, COMDLG32 2, WINTRUST 1, WININET 1, MSIMG32 1, dbghelp 1.

## 4. Working method (the loop)

1. `bash run2.sh` (after `sudo -v`), read `/tmp/err.txt` and the kernel log.
2. If it ends with `CALLED UNIMPLEMENTED: dll!func ...`, implement that function **and the obvious siblings the same code path will call next** (batch 5-15 related shims per round). Decode arguments from the `rcx/rdx/r8/r9` values printed (they are Windows x64 argument registers; unused ones are leftovers).
3. Rebuild the bridge, rerun the regression tests (below), run Notepad++ again.
4. Commit when green. Stop and report to the owner when behaviour changes class (new kind of crash, GUI calls start, threads needed, a design decision is needed).

Create `tests/run_all.sh` (run with `bash`) that runs `tests/hello.exe`, `./hello.exe`, `tests/heap.exe` and checks their output and exit codes (0, 42, 0), then runs Notepad++ and prints its last `[BRIDGE]` line, with PASS/FAIL per test. Run it after every change.

Shim rules:
- Windows x64 (`ms_abi`) args: RCX, RDX, R8, R9, then stack; 32-byte shadow space; **RSP%16==0 before `call`**; callee-saved RBX, RBP, RDI, RSI, R12-R15, XMM6-XMM15; return in RAX. System V (bridge C code): RDI, RSI, RDX, RCX, R8, R9.
- Types: `HANDLE`/pointers 64-bit; `DWORD`/`BOOL` 32-bit; **`WCHAR` is 16-bit (`uint16_t`)**, never Linux `wchar_t`.
- Failures: set last error (`gs:0x68`) and return 0/NULL; use real Win32 error codes (5, 6, 8, 87, 126, 127, 998...).
- Freestanding: no libc. GCC can emit calls to `memset/memcpy/memmove/memcmp` (they exist; keep them). Prefer raw syscalls (`mmap` 9, `munmap` 11, `mprotect` 10, `clock_gettime` 228, `getpid` 39, `gettid` 186, `open` 2, `read` 0, `write` 1, `close` 3, `nanosleep` 35, `exit_group` 231).
- Keep unimplemented-stub fallback for anything not registered; do not silently return success for unknown APIs.

## 5. Roadmap

### Batch A — CRT startup (likely already requested)
GetStartupInfoW/A (zero 104 bytes, `cb=104`), IsProcessorFeaturePresent (conservative: true for 2,3,6,10,12,13,14, false otherwise; **23 must be false**), IsDebuggerPresent=0, SetUnhandledExceptionFilter (store, return old), UnhandledExceptionFilter (return 0), EncodePointer/DecodePointer (identity), InitializeSListHead (zero 16 bytes), GetCurrentProcess (-1), GetCurrentThread (-2), GetCommandLineW/A (static `notepad++.exe` for now; TODO from `/proc/self/cmdline`), GetEnvironmentStringsW (empty block `\0\0`)/FreeEnvironmentStringsW.

### Batch B — strings, files, process
WideCharToMultiByte / MultiByteToWideChar (real UTF-8 <-> UTF-16, CP_ACP and CP_UTF8 both treated as UTF-8, correct return-size semantics when the output buffer is 0), GetACP/GetOEMCP/GetCPInfo, lstrlenW/A, CompareStringW, LCMapStringW, GetStringTypeW, GetFileType, CreateFileW/ReadFile/CloseHandle/SetFilePointerEx/GetFileSizeEx/GetFileAttributesW/FindFirstFileW family (decide a **Windows path → Linux path mapping**, e.g. `C:\...` and `Z:\...` mapped under a configurable root; write the decision down), GetModuleFileNameW (consistent with that mapping), GetCurrentDirectoryW, Sleep (nanosleep), GetTickCount/GetTickCount64, GetSystemInfo, GetVersionExW/RtlGetVersion (report a Windows 10 version), VirtualAlloc/VirtualFree/VirtualQuery, GetLocaleInfoW, FormatMessageW, OutputDebugString, GetEnvironmentVariableW, ExpandEnvironmentStringsW.

### Batch C — TLS directory and exceptions (required to get far into Notepad++)
- **TLS directory** (RVA `0x502000`, size `0x28`): allocate a TLS block, copy the template, store the index/array so `gs:[0x58]` (`ThreadLocalStoragePointer`) + `_tls_index` works; run TLS callbacks with `DLL_PROCESS_ATTACH`.
- **x64 SEH/C++ exceptions** from `.pdata` (RVA `0x57a000`, size `0x1faa0`): RtlCaptureContext, RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwindEx, RaiseException, plus `__C_specific_handler` flow as called by compiled code. Write a small test PE using `__try/__except` first (mingw) before Notepad++.
- Apply `.reloc` only if the image ever loads at a non-preferred base.

### Batch D — threads and sync (when the program creates threads)
CreateThread (via `clone` with a per-thread TEB and GS base), real futex-based critical sections, SRW locks, events, mutexes, WaitForSingleObject(Ex)/WaitForMultipleObjects, per-thread TLS/FLS, InterlockedXxx (mostly compiler intrinsics), ExitThread.

### Stage 2 — GUI (the long road; decision needed from the owner)
USER32 (216 imports) + GDI32 (66) + COMCTL32/UxTheme/SHELL32/etc. need a display path: windows, message loop, painting, fonts, input. Options: (a) minimal in-house layer on X11 (Xlib/xcb via dynamic loading or raw protocol) first, Wayland later; (b) selectively reuse LGPL Wine code (requires explicit owner approval and licensing review); (c) first prove the path with a tiny Win32 test app (RegisterClassW/CreateWindowExW/message loop/`TextOutW`) before Notepad++. Bring a short written design and a recommendation to the owner before building this.

### Hygiene (do when convenient)
- Per-section memory protections from PE `Characteristics` (text RX, rdata R, data RW) done in the kernel; `/proc/self/maps` shows the PE's non-text sections labelled `[heap]` (the kernel set `mm->brk` there; cosmetic, but the bridge must never call `brk`).
- Move GS/TEB setup fully into the kernel (the bridge currently sets GS via `arch_prctl` if empty); fill TEB ClientId and TLS pointer.
- Bridge loader validation in the kernel: ELF magic, `EM_X86_64`, `ET_EXEC`, `p_vaddr + p_memsz` inside the 1 MiB window, `p_filesz <= p_memsz`, overflow-safe arithmetic. Keep the whole PE parser bounds-checked; it parses untrusted files in kernel space.
- Docs: keep `README.md` status checklist honest (milestone: first Windows exe running through the loader); keep this handoff updated; add a `docs/ARCHITECTURE.md`.
- License is undecided (a kernel module must be GPL-compatible to use most kernel symbols). Ask the owner before adding a LICENSE file.

## 6. Reference data

### notepad++.exe 8.9.8 x64
| Item | Value |
|---|---|
| ImageBase / entry VA | `0x140000000` / `0x1403f83b0` |
| Import directory | RVA `0x55ead0`, size `0x1b8` |
| IAT | RVA `0x44d000`, size `0x1408` |
| Exception (.pdata) | RVA `0x57a000`, size `0x1faa0` |
| TLS directory | RVA `0x502000`, size `0x28` |
| Load Config | RVA `0x501cf0`, size `0x140` |
| Debug | RVA `0x501e30`, size `0x1c` |
| Resources | RVA `0x59b000`, size `0x2848d0` |
| Base relocations | RVA `0x820000`, size `0x6aac` |
| Sections | `.text 0x140001000`, `.rdata 0x14044d000`, `.data 0x140563000`, `.pdata 0x14057a000`, `.fptable 0x14059a000`, `.rsrc 0x14059b000`, `.reloc 0x140820000` |

### Address map
Bridge `0x70000000` (1 MiB window, entry = `_start`, changes per build); fake HMODULEs `0x6ff00000+`; fake heap handle `0x6ffe0000`; TEB `0x7ff00000`; PEB `0x7ff00800`; legacy heap page `0x7ff30000`; stack `0x7ffff6800000`-`0x7ffff7000000`, initial SP `0x7ffff6fff000`.

### TEB (x64) offsets
`0x08` StackBase, `0x10` StackLimit, `0x30` Self, `0x40` ClientId.pid, `0x48` ClientId.tid, `0x58` ThreadLocalStoragePointer, `0x60` PEB, `0x68` LastErrorValue, `0x1480` TlsSlots[64], `0x17c8` FlsData.
### PEB offsets
`0x10` ImageBaseAddress, `0x18` Ldr, `0x20` ProcessParameters, `0x30` ProcessHeap.

### Struct sizes
RTL_CRITICAL_SECTION 40 bytes (LockCount `0x08`, RecursionCount `0x0c`, OwningThread `0x10`, SpinCount `0x20`); STARTUPINFOW 104 bytes; SLIST_HEADER 16 bytes.

## 7. Report format after each round

1. What you changed (files, shims), 2. exact commands you ran and their real outputs (trimmed), 3. regression results (PASS/FAIL list), 4. the new stopping point (`CALLED UNIMPLEMENTED ...` or crash dump summary), 5. decisions or risks that need the owner. Keep it short; the owner pastes logs back to a second assistant for review.
