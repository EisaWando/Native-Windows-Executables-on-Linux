# binfmt_pe: Native Windows PE Execution on Linux — Agent Handoff

Last updated: 2026-10-09. Read this whole file before touching code.

---

## 1. Mission

Run Windows x64 PE executables (first target: `npp.8.9.8.portable.x64/notepad++.exe`) **natively on Linux**, with **no Wine/Proton loader, wineserver or emulation layer**, by:

1. A custom kernel binary-format handler, `binfmt_pe.ko` (source: `fs/binfmt_pe.c` in the `linux-7.1.6` tree at `/home/eisa/Desktop/linux-7.1.6`), which recognizes MZ/PE files, parses headers, maps sections at the preferred base, builds a Windows-style process environment (TEB/PEB/heap), and hands off to userland.
2. A userland bridge, `win32_bridge.so`, mapped at `0x70000000`, which is the actual "Windows runtime": import resolution, Win32 API shims (translated to Linux syscalls/libs), TEB/PEB upkeep, later window/message-loop integration with X11/Wayland.

Goal context: reduce Windows lock-in by letting ordinary Windows programs run on Linux. The project is an original, clean-room implementation and is meant to stay legal (no Microsoft code). Reusing LGPL Wine DLL code is a possible option if "no Wine" means "no Wine loader/server"; decide with the owner before doing so.

Constraints: the owner uses free AI tools only and coordinates several AI agents. Prefer small, verifiable steps and exact commands/diffs.

---

## 2. Environment

- Host: Linux, running kernel built from/matching `linux-7.1.6`; module built out-of-tree style:
  `make -C /lib/modules/$(uname -r)/build M=$PWD/fs modules`
- Module path: `/home/eisa/Desktop/linux-7.1.6/fs/binfmt_pe.ko`
- Tools in use: `objdump`, `dmesg`, `su -c`. The kernel is tainted (unsigned out-of-tree module); this is harmless.
- Test binaries: `./npp.8.9.8.portable.x64/notepad++.exe`. `./tests/hello.exe` was planned but **does not exist yet**.
- Logging convention: kernel log lines are prefixed `[BINFMT_PE]`. **Always use `sudo dmesg | grep BINFMT_PE | tail -n 40`**; plain `dmesg | tail` is flooded by UFW firewall lines.

### Reload/run sequence (use this, not `rmmod -f && insmod`)
```bash
lsmod | grep binfmt_pe
sudo rmmod binfmt_pe 2>/dev/null; true     # no -f; chaining with && aborts insmod if not loaded
sudo insmod /home/eisa/Desktop/linux-7.1.6/fs/binfmt_pe.ko; echo "insmod=$?"
./npp.8.9.8.portable.x64/notepad++.exe
sudo dmesg | grep -E "BINFMT_PE|segfault" | tail -n 40
```
If `Exec format error` appears, the module is not loaded or the handler rejected the file. A module/kernel `vermagic` mismatch gives `Invalid module format` (`modinfo -F vermagic binfmt_pe.ko` vs `uname -r`).

---

## 3. What works today

- Module registers as a binfmt handler: `[BINFMT_PE] Registered Native Windows PE Executable Format Handler`.
- MZ signature and PE header parsing succeed (64-bit x86_64, ImageBase `0x140000000`, Entry RVA gives VA `0x1403f83b0`, 7 sections).
- Sections mapped: `.text` 0x140001000, `.rdata` 0x14044d000, `.data` 0x140563000, `.pdata` 0x14057a000, `.fptable` 0x14059a000, `.rsrc` 0x14059b000, `.reloc` 0x140820000.
- Initialized: TEB at `0x7ff00000`, PEB at `0x7ff00800`, 1 MB heap at `0x7ff30000`.
- `win32_bridge.so` mapped at `0x70000000`, entry `0x70002190`.
- Stack: 8 MB `vm_mmap` region, base `0x7ffff6800000`, top `0x7ffff7000000`, **`user_sp = 0x7ffff6fff000`** (top − 0x1000, 16-byte aligned). The old crash (RSP = stack base, `push %rbp` faulting at `0x700022f4`) is **fixed**. Its true cause was passing the mmap base (low address) as the stack pointer; stacks grow down. `MAP_GROWSDOWN`/`MAP_STACK` were never the cause.
- Handoff registers: `IP=0x70002190`, `SP=0x7ffff6fff000`, `RDI=0x1403f83b0` (PE entry), `RSI=0x7ff00800` (PEB).
- The bridge runs and **jumps into the PE's own startup code** (the first real PE code executes).

---

## 4. Current problem (what is broken right now)

### 4.1 Crash: unresolved import (IAT not filled)
```
notepad++.exe[...]: segfault at 560a9c ip 0000000000560a9c sp 00007ffff6ffef90 error 14
```
- `error 14` is **hex 0x14**: user-mode instruction fetch from a non-present page.
- `0x560a9c` is an **RVA, not a pointer**. `objdump -s` at `0x140560a90` shows bytes `1a03` + `GetSystemTimeAsFileTime`, so `0x560a9c` is the `IMAGE_IMPORT_BY_NAME` (hint `0x031a` + name) for **kernel32!GetSystemTimeAsFileTime**.
- Meaning: the CRT startup (`__security_init_cookie`) did `call [rip+IAT_slot]`; the IAT slot still held the unresolved hint/name RVA, so the CPU jumped to `0x560a9c`. **Nothing resolves imports yet.**

### 4.2 Bug: kernel parser reports `Import RVA: 0x0`
The real import directory (from `objdump -p`) is **RVA `0x55ead0`, size `0x1b8`** (22 descriptors including the terminator, so about 21 DLLs). For PE32+ the `DataDirectory[]` array starts at **OptionalHeader + 0x70** (PE32 uses +0x60); the import directory is index 1 (OptionalHeader + 0x78). The kernel code probably uses a PE32 offset or checks the wrong field. Fix and verify it prints `0x55ead0`.

### 4.3 PE headers page is not mapped
No section starts at `0x140000000`; the headers (first page, `SizeOfHeaders` bytes) must be mapped **read-only** there. Windows programs, the CRT and `GetModuleHandle(NULL)` read the DOS/NT headers from the image base, and the bridge needs them for import walking.

### 4.4 Latent issues (will bite soon)
- **TEB access is via GS on Windows x64** (`gs:[0x30]` = TEB self pointer, `gs:[0x60]` = PEB pointer). Linux uses FS for TLS. Make sure the TEB is reachable through GS (set GS base, e.g. `ARCH_SET_GS`/`wrgsbase` or kernel equivalent) with a proper `NT_TIB` layout (StackBase `0x7ffff7000000`, StackLimit `0x7ffff6800000`, Self pointer, PEB pointer at TEB+0x60). Earlier notes mention FS; verify.
- **Calling convention**: Windows x64 passes args in RCX, RDX, R8, R9 with 32 bytes of shadow space and 16-byte stack alignment at call sites; Linux SysV uses RDI, RSI, RDX, RCX, R8, R9. Every shim handed to the PE must be `__attribute__((ms_abi))`. Preserve callee-saved registers including XMM6–XMM15.
- Entry ABI: the PE entry point (`mainCRTStartup`/`WinMainCRTStartup`) takes no arguments (it uses the PEB); the bridge should call it as an `ms_abi` function with a valid shadow space, not jump with SysV registers.
- No relocation is needed while the image sits at the preferred base `0x140000000`, but `.reloc` exists and should be applied if the base ever changes.
- **Not yet handled**: TLS directory (RVA `0x502000`, size `0x28`; callbacks and TLS slots), Load Config (`0x501cf0`), exception/unwind data (`.pdata` at `0x57a000`, size `0x1faa0`; needed for SEH and C++ exceptions), Debug directory (`0x501e30`), a Security directory (`0x81fa00`, a file offset, ignore), resources (`0x59b000`, size `0x2848d0`, read via `FindResource`/`LoadResource` shims).
- Core dumps and `gdb` do not work on this non-ELF process (no binfmt core dump hook), so the bridge must provide its own diagnostics.

Not a problem (ignore): "module verification failed / tainting kernel", UFW log spam.

---

## 5. What we are going to build

### 5.1 Kernel module (`fs/binfmt_pe.c`) — keep it minimal
Kernel code is a crash/security risk. The module should only: validate, map headers + sections (with correct protections), map the bridge, build the stack, set TEB/PEB/GS, and `start_thread()`. Push everything else (imports, relocations, TLS, API) into userland.

### 5.2 Userland bridge (`win32_bridge.so`)
1. **Diagnostics first**: a `SIGSEGV`/`SIGILL` handler (`SA_SIGINFO | SA_ONSTACK`, own `sigaltstack`) that dumps all registers from `ucontext_t`, 16 stack qwords, and `/proc/self/maps` to stderr.
2. **Import resolver**: walk `IMAGE_IMPORT_DESCRIPTOR`s; for each DLL read the ILT (OriginalFirstThunk) and write resolved addresses into the IAT (FirstThunk); support by-name and by-ordinal. Look up shims in a table keyed by `dll!function`.
3. **Missing-import trap**: every unimplemented import gets a generated stub that prints `UNIMPLEMENTED: dll!function` and exits. This turns each crash into a work item.
4. **Shim libraries** (all `ms_abi`), in rough order of need: `kernel32` (heap, virtual memory, TLS, file I/O, time, modules, process/thread, console), `ntdll` basics (`Rtl*`, `Nt*` subset incl. exception/unwind helpers), `msvcrt/ucrt` support as imported, then `user32`/`gdi32`/`comctl32`/`shell32`/`ole32`/`advapi32` etc.
5. **Process environment**: correct PEB (ImageBaseAddress, ProcessHeap, ProcessParameters with command line/environment), `GetModuleHandle`, `GetProcAddress`, `LoadLibrary` for built-in shim "DLLs" (and possibly loading real PE DLLs later).
6. **TLS and SEH**: TLS directory processing, `.pdata`-based unwind (`RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlUnwindEx`) for structured/C++ exceptions.
7. **UI path (long-term)**: route `CreateWindowEx`, message loop, painting and GDI to X11/Wayland (and fonts/DirectWrite-like text) so Notepad++ shows a window.

### 5.3 Strategy: **small target first**
Notepad++ is a very hard first target (21 DLLs, hundreds of imports, SEH, TLS, C++ runtime, GDI/Scintilla). Build a minimal test first:
```bash
# tests/hello.c uses only GetStdHandle, WriteFile, ExitProcess
x86_64-w64-mingw32-gcc -nostdlib -o tests/hello.exe tests/hello.c -lkernel32
```
Then grow: console app with CRT → app with TLS/SEH → simple Win32 window app → Notepad++.

---

## 6. Ordered next steps

1. **Fix header parsing** in `binfmt_pe.c`: use OptionalHeader + 0x70 for PE32+ `DataDirectory`, log `Import RVA` (expect `0x55ead0` for Notepad++). Map the header page read-only at `0x140000000`.
2. **Add crash diagnostics** to the bridge (signal handler described above).
3. **Create `tests/hello.exe`** with mingw-w64; verify it loads (`[BINFMT_PE]` log lines) and that failures are clean.
4. **Implement the import resolver + `UNIMPLEMENTED` stubs**; implement `GetStdHandle`, `WriteFile`, `ExitProcess` as `ms_abi` shims; get `hello.exe` to print and exit with the right code.
5. **Fix TEB/GS** and verify `gs:[0x30]`/`gs:[0x60]` from test code; call the PE entry as an `ms_abi` function with shadow space.
6. **Return to Notepad++**: iterate "run → see which `dll!function` is UNIMPLEMENTED → implement → repeat", starting with `GetSystemTimeAsFileTime` (the current crash), then the CRT startup set (`GetCurrentProcessId`, `GetCurrentThreadId`, `QueryPerformanceCounter`, `GetStartupInfoW`, `HeapAlloc`/`HeapFree`, `TlsAlloc`/`FlsAlloc`, `GetProcAddress`, `GetModuleHandleW`, etc.).
7. **TLS directory + exceptions/unwinding** support.
8. **Window system integration** (X11 first, Wayland later), message loop, GDI subset.

---

## 7. Rules for agents working on this project

- Do not claim a fix without a log line or output that proves it; ask the owner for `dmesg | grep BINFMT_PE` output and the bridge dump after each test.
- Do not suggest swapping to Wine; if a Wine-derived LGPL component is considered, flag it as a decision for the owner.
- Give exact diffs, exact build/run commands and the expected output lines.
- Keep kernel changes minimal; prefer userland for anything that can live there.
- Never use `rmmod -f` unless the module is stuck; always check `lsmod` first.
- Prefix all new kernel logs with `[BINFMT_PE]` and bridge logs with `[BRIDGE]`.
- Treat `error N` in kernel segfault messages as **hex** (`0x14` = user instruction fetch, page not present; `0x6` = user write, page not present).

---

## 8. Key reference data (Notepad++ 8.9.8 x64)

| Item | Value |
|---|---|
| ImageBase | `0x140000000` |
| Entry point (VA) | `0x1403f83b0` |
| Import directory | RVA `0x55ead0`, size `0x1b8` |
| Import Address Table | RVA `0x44d000`, size `0x1408` |
| Exception (.pdata) | RVA `0x57a000`, size `0x1faa0` |
| TLS directory | RVA `0x502000`, size `0x28` |
| Load Config | RVA `0x501cf0`, size `0x140` |
| Debug | RVA `0x501e30`, size `0x1c` |
| Resources | RVA `0x59b000`, size `0x2848d0` |
| Base relocations | RVA `0x820000`, size `0x6aac` |
| Bridge | mapped `0x70000000`, entry `0x70002190` |
| TEB / PEB / heap | `0x7ff00000` / `0x7ff00800` / `0x7ff30000` (1 MB) |
| Stack | base `0x7ffff6800000`, top `0x7ffff7000000`, initial SP `0x7ffff6fff000` |
| Current crash | ip `0x560a9c` = unresolved IAT slot for `kernel32!GetSystemTimeAsFileTime` |

Useful commands:
```bash
objdump -p notepad++.exe | grep -A20 "Data Directory"
objdump -p notepad++.exe | grep "DLL Name"
objdump -s --start-address=0x140560a90 --stop-address=0x140560ac0 notepad++.exe
sudo dmesg | grep BINFMT_PE | tail -n 40
```
