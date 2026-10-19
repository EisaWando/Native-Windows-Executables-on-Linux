# binfmt_pe — Native Windows Executables on Linux, Without Wine

![Milestone: hello.exe runs](https://img.shields.io/badge/milestone-hello.exe%20runs-brightgreen)
![Status: early research prototype](https://img.shields.io/badge/status-early%20research%20prototype-orange)
![Target: x86-64 PE32+](https://img.shields.io/badge/target-Windows%20x64%20(PE32%2B)-blue)
![Run in a VM](https://img.shields.io/badge/safety-test%20in%20a%20VM-red)

> **🎉 Milestone reached: a real Windows PE executable now runs end-to-end through our own kernel loader.**
> `./hello.exe` is recognized by the Linux kernel as a native binary format, mapped by `binfmt_pe.ko`, handed to a userland runtime, and prints its message and exits cleanly. No Wine, no emulator, no VM in the path.
>
> **Still early:** Notepad++ does not run yet. It loads and executes its startup code until it reaches the first Win32 function we haven't implemented. This README is written to be honest about both halves of that sentence.

---

## Table of contents

1. [The milestone](#the-milestone-hello-from-pe)
2. [The idea](#the-idea)
3. [Our mission](#why-we-are-doing-this-our-mission)
4. [How it works](#how-it-works)
5. [Current status](#current-status)
6. [The road to Notepad++](#the-road-to-notepad)
7. [How we got here](#how-we-got-here-bugs-worth-knowing-about)
8. [Honest caveats](#honest-caveats)
9. [Getting started](#getting-started)
10. [Repository layout](#repository-layout)
11. [How you can help](#how-you-can-help)
12. [License](#license)
13. [Credits](#credits)
14. [Community](#community)

---

## The milestone: "Hello from PE!"

A tiny Windows console program (`tests/hello.exe`) imports exactly three functions from `KERNEL32.dll`: `GetStdHandle`, `WriteFile` and `ExitProcess`. It now runs like this:

```
$ ./tests/hello.exe
Hello from PE!
$ echo $status        # (bash: echo $?)
0
```

What happens between those two lines:

1. The shell calls `execve()` on a file that is **not** an ELF binary.
2. The kernel offers it to registered binary-format handlers; `binfmt_pe` recognizes the `MZ`/`PE` headers.
3. The loader validates the headers, maps the sections at their preferred address, builds the TEB/PEB/heap, loads `win32_bridge.so`, and starts the thread with a correct stack.
4. The bridge resolves the three imports to `ms_abi` shims written for Linux and calls the PE's entry point as native x86-64 code.
5. `WriteFile` writes to the real stdout; `ExitProcess` exits with the program's own exit code. A second sample returns exit code 42, and it comes through correctly.

**What this does and doesn't claim.** Wine has run Windows programs on Linux for decades, and we build on its history. Our claim is narrower: this is a Windows executable running through a **kernel-level loader** (`execve` → `binfmt_pe`) with a small, purpose-built userland runtime, and **no Wine, wineserver, emulator or VM involved**. That is the architecture experiment this project exists to test, and it now works end to end for a first real program.

---

## The idea

`binfmt_pe` is a Linux kernel module that teaches the kernel to recognize Windows x64 PE executables (`.exe`) as a **native binary format**, the same way it already recognizes ELF programs and scripts.

The kernel:

1. parses and validates the PE headers,
2. maps the sections at the image's preferred base address,
3. builds a Windows-style process environment (TEB, PEB, heap),
4. hands execution to a small userland runtime, `win32_bridge.so`, which resolves the program's imports and provides the Win32 API on top of Linux.

There is **no emulator, no virtual machine, and no Wine, Proton or wineserver** in the path.

The long-term target is the portable x64 build of **Notepad++ 8.9.8**.

---

## Why we are doing this: our mission

We believe Linux users and developers deserve to run the software they actually use, **natively and by default**, without having to keep a whole Windows installation around.

Many people (developers, content creators, gamers and others) stay on Windows, or keep a dual-boot partition, for one reason: a handful of applications. When the operating system underneath is one you did not choose and cannot fully control, that has real costs:

- **Privacy.** Operating systems increasingly require accounts, collect extensive diagnostic data, and ship features that analyze what you do on your own machine. Whatever one thinks of any individual feature, the common pattern is a lack of control over them.
- **Terms you did not negotiate.** People accept long, frequently changing terms and policies as the price of using a machine they paid for. We think the people who own the hardware should have the final say over who can access their data and what runs on it.
- **Lock-in.** When the applications you depend on only run on one vendor's OS, your choice of operating system is not a real choice any more.
- **Forced updates.** Users should decide when, or whether, to update their OS. Mandatory, system-breaking updates that add bloat are not a fair trade.
- **AI you did not ask for.** Shipping AI inside an OS does not mean users must be forced to use it. AI is a great tool for creativity and for making hard things easier, but it should not be mandatory, running in the background and consuming resources on hardware users own.

This is a criticism of **Windows as an operating system and of the terms attached to it**, not of every Microsoft service, and not of the people who use Windows. Many people have good reasons to be there. Our point is narrower: **nobody should *need* a particular OS just to run their software.**

Linux already gives its users auditable code, control over telemetry, and the right to change everything. What it has lacked is easy access to the Windows application ecosystem. Wine, Proton, ReactOS and others have done heroic work toward that, and this project stands on the shoulders of that history. We want to explore a different point in the design space: a **kernel-integrated, loader-level approach** where a Windows binary is simply an executable.

---

## How it works

```
./hello.exe   (or ./notepad++.exe)
       │
       ▼
Linux execve() ──► binfmt_pe.ko (kernel module)
       │             • validates MZ/PE headers (bounds-checked)
       │             • maps headers (read-only) + sections at ImageBase (0x140000000)
       │             • sets up TEB / PEB / heap, GS base
       │             • loads win32_bridge.so (PT_LOAD segments) at 0x70000000
       │             • builds the stack, start_thread()
       ▼
win32_bridge.so (userland runtime)
       • walks the import table, fills the IAT with ms_abi shims
       • implements Win32 APIs on top of Linux syscalls
       • unimplemented imports get a named stub that reports itself
       ▼
PE entry point runs as native x86-64 code
```

**Design rule: keep the kernel part minimal.** Everything that can live in userland (imports, API behavior, relocations, TLS, UI) lives in `win32_bridge.so`.

### Technical notes

- **Target:** Windows x64 (PE32+) only; executables first, then DLLs.
- **Calling convention:** Windows x64 (RCX, RDX, R8, R9, 32-byte shadow space, callee-saved XMM6–XMM15) differs from the Linux System V ABI. Every shim handed to a PE is `__attribute__((ms_abi))`.
- **TEB via GS:** Windows x64 code reads the TEB through `gs:[0x30]` and the PEB through `gs:[0x60]`. Linux uses FS for libc TLS, so GS is free for us. The bridge verifies this at startup (`gs:30`, `gs:60` and the stack bounds in the TIB all read back correctly) and keeps the last-error value at `gs:[0x68]`.
- **Address space:** ImageBase `0x140000000`; bridge at `0x70000000`; TEB `0x7ff00000`, PEB `0x7ff00800`, heap from `0x7ff30000`; 8 MB stack just below `0x7ffff7000000`.
- **The bridge is a freestanding static ELF** (no libc, no PIE) linked at a fixed address and loaded by the kernel from its `PT_LOAD` segments, using its real entry point.
- **Imports:** the bridge resolves by name. Unimplemented functions get a stub that prints `UNIMPLEMENTED: dll!function` along with the return address, the first four arguments and, for wide-string arguments, a text preview. Each missing API becomes a concrete to-do item.
- **Exceptions and unwinding:** x64 SEH is table-based (`.pdata`). `RtlLookupFunctionEntry`, `RtlVirtualUnwind` and `RtlUnwindEx` will be needed for C++ and structured exceptions.
- **Diagnostics:** `gdb` and core dumps don't work on this non-ELF process, so the bridge installs its own crash handler (registers, stack, `/proc/self/maps`).
- **Log prefixes:** kernel messages use `[BINFMT_PE]`; bridge messages use `[BRIDGE]`.

---

## Current status

*Last verified: 18/10/2026 (DD/MM/YYYY).*

**Working**

- [x] MZ/PE header parsing with bounds checks (PE32+), including data directories (import, IAT, exception, reloc, TLS)
- [x] PE headers mapped read-only at the image base; all 7 sections of `notepad++.exe` mapped at their virtual addresses
- [x] TEB, PEB and heap initialized; GS/TEB verified from userland
- [x] Bridge loaded from its `PT_LOAD` segments, with a correct stack handoff
- [x] Import walk for all **620** Notepad++ imports, with named stubs for the unimplemented ones
- [x] `ms_abi` shims, so far: `GetStdHandle`, `WriteFile`, `ExitProcess`, `GetSystemTimeAsFileTime`, `QueryPerformanceCounter` / `QueryPerformanceFrequency`, `GetCurrentProcessId` / `GetCurrentThreadId`, last-error handling, a built-in module registry (`LoadLibraryExW`, `GetModuleHandleW`, `GetProcAddress`), critical sections, TLS/FLS, `VirtualProtect` (via `mprotect`), the process-heap family (`GetProcessHeap`, `HeapAlloc`/`Free`/`ReAlloc`/`Size`/`Create`, backed by an `mmap` allocator), and `memset`/`memcpy`/`memmove`/`memcmp`
- [x] **`hello.exe` runs end to end** (see the [milestone](#the-milestone-hello-from-pe))

**In progress**

- [ ] Notepad++ startup. It currently runs through CRT initialization with **32 imports resolved and 588 still stubs**, and stops at `KERNEL32!GetStartupInfoW`
- [ ] Remaining CRT startup APIs (process/thread info, performance counters, module handles, environment)
- [ ] The rest of the ~190 `KERNEL32` imports, written as one batch from the generated import list

**Planned**

- [ ] TLS directory and callbacks
- [ ] SEH/C++ exception unwinding from `.pdata`
- [ ] A `user32`/`gdi32`/`comctl32` subset mapped onto X11 and Wayland
- [ ] Notepad++ rendering a window

---

## The road to Notepad++

Notepad++ is a good, deliberately hard target. Its 620 imports break down like this:

| DLL | Imports | | DLL | Imports |
| --- | ---: | --- | --- | ---: |
| `USER32` | 216 | | `SHELL32` | 12 |
| `KERNEL32` | 192 | | `ole32` | 12 |
| `GDI32` | 66 | | `IMM32` | 9 |
| `ADVAPI32` | 26 | | `CRYPT32` | 8 |
| `COMCTL32` | 24 | | `bcrypt` | 7 |
| `SHLWAPI` | 17 | | `VERSION` | 3 |
| `UxTheme` | 16 | | others | 1–2 each |

The order of attack follows what the program actually asks for, one stub at a time:

1. **`KERNEL32` and the CRT startup path.** The next blocker is `GetStartupInfoW`; after that, the rest of the batch. This is where we are now.
2. **TLS callbacks and exception unwinding**, because the C++ runtime needs them before `WinMain`.
3. **`USER32`/`GDI32`/`COMCTL32`/`UxTheme`** onto X11 and Wayland: windows, the message loop, text rendering.
4. **`ole32`, `SHELL32`, `ADVAPI32`** and the long tail.

**Realistic expectation.** The "hello" milestone is behind us. A full GUI application with hundreds of imports, GDI text rendering, COM and exception handling is still a very long road: years if the project stays a one-person effort, months with a team. That is exactly where contributors can make the biggest difference.

---

## How we got here: bugs worth knowing about

Kernel loaders fail silently and spectacularly. These three bugs cost the most time and are good examples of what this work looks like:

1. **The stack that started at its own base.** The first handoff faulted on the very first `push` because the stack pointer pointed at the *bottom* of the stack mapping instead of near the top. The fix was to place the user stack pointer explicitly after `start_thread()` instead of relying on the mapping flags.
2. **The entry point that was relocated twice.** The bridge is a non-PIE ELF with an absolute entry address, but the kernel was adding the load base to it and copying the file flat, ignoring the `PT_LOAD` offsets. The process died silently before printing anything. The loader now maps real segments and uses `e_entry` directly.
3. **Eight bytes off.** The bridge's `_start` adjusted the stack by the wrong amount before calling into the PE entry, so the Win64 shadow-space/alignment contract was broken and `WriteFile` crashed. Correcting the adjustment made `hello.exe` print.

---

## Honest caveats

- **Security.** A PE loader in the kernel parses untrusted input, so a bug there is a kernel bug. The design deliberately keeps the in-kernel part small, but this code needs review before anyone runs it on a machine they care about. **Test in a VM.**
- **Scope.** Win32 is enormous. We are not promising compatibility with arbitrary software, anti-cheat, DRM-protected applications or software that depends on Windows kernel drivers, at least for now.
- **Overlap with existing work.** Wine and Proton are mature and far ahead on API coverage. This project is an experiment in a different architecture, not a claim that those projects are unnecessary. If a Wine-derived component (LGPL) turns out to be the right way to implement parts of the API layer, that is an open question for the maintainers and the community.
- **Legality.** This is a clean-room effort: no Microsoft source code, no leaked code, no copied proprietary binaries. Contributors must not submit code derived from leaked or proprietary Windows sources.

---

## Getting started

> ⚠️ Use a **virtual machine**. Loading an experimental kernel module can crash or corrupt a running system.

### Requirements

- An x86-64 Linux machine (a VM is strongly recommended)
- Kernel headers / build tree matching your running kernel (`/lib/modules/$(uname -r)/build`)
- `make`, a C toolchain (`gcc`, or `clang`/LLVM if your kernel was built with clang)
- Root access (`insmod`, `rmmod`, `dmesg`)
- Secure Boot disabled, or a way to sign the module (unsigned modules will not load otherwise)

### Build

```sh
# Kernel module (out-of-tree build against the running kernel's headers)
# Use LLVM=1 only if your kernel itself was built with clang (e.g. CachyOS).
make LLVM=1 -C /lib/modules/$(uname -r)/build M=$PWD/fs modules

# Userland bridge: a freestanding, static, non-PIE ELF linked at 0x70000000
gcc -fno-pic -no-pie -static -nostdlib -ffreestanding \
    -Wl,-Ttext-segment=0x70000000 -Wl,-e,_start \
    -o win32_bridge.so win32_bridge.c
```

### Load and run

```sh
# Replace any previously loaded copy (avoid `rmmod -f` unless truly stuck)
lsmod | grep binfmt_pe
sudo rmmod binfmt_pe 2>/dev/null; true
sudo insmod fs/binfmt_pe.ko

# 1. The milestone: a minimal console program
./tests/hello.exe
# → Hello from PE!

# 2. The long-term target (currently stops at the first unimplemented API)
./npp.8.9.8.portable.x64/notepad++.exe 2> /tmp/err.txt
cat /tmp/err.txt
sudo dmesg | grep BINFMT_PE | tail -n 40
```

If `insmod` reports `File exists`, the module is already loaded; that is harmless. (In fish, use `$status` instead of `$?` to read an exit code.)

### What you should see today

- **`hello.exe`:** the message, then a clean exit.
- **Notepad++:** the kernel log shows the PE directories parsed and the sections mapped. The bridge reports how many imports it resolved and how many are still stubs (`resolved 32, unimplemented 588` at the time of writing), then runs the program until it calls an unimplemented function and prints `CALLED UNIMPLEMENTED: dll!function` with the return address and arguments. That line is the next to-do item.

Build the bridge with `-DBRIDGE_VERBOSE` to also print every IAT slot it patches (`BRIDGE: IAT dll!function -> address`), which is how the full import list was generated.

---

## Repository layout

This repository is built on a Linux kernel source tree, so most top-level directories (`arch`, `drivers`, `mm`, `kernel`, …) are the unmodified upstream layout. The parts specific to this project are:

| Path | Purpose |
| --- | --- |
| `fs/binfmt_pe.c` | The kernel-side PE loader (`binfmt_pe.ko`) |
| `win32_bridge.c` | The userland runtime built into `win32_bridge.so` |
| `tests/hello.exe` | The first end-to-end test: a console program with three imports |
| `npp.8.9.8.portable.x64/` | The Notepad++ 8.9.8 portable x64 build used as the long-term target |
| `samples/` | Samples |
| `scripts/` | Helper scripts |
| `Project.md` | Project overview |
| `log.txt` | Captured logs from development runs |
| `patch*.py`, `bisect_patch*.py`, `patch_bridge.sh` | Development helpers used while patching and bisecting the bridge |

For documentation about the surrounding kernel tree, see `Documentation/` and <https://www.kernel.org/doc/html/latest/>.

---

## How you can help

We need people who enjoy hard, low-level problems. Every missing API shows up by name in the log, so there is always a concrete next task.

| Area | What's needed |
| --- | --- |
| **Kernel** | Review of `fs/binfmt_pe.c`; safe VMA handling, hardening against malformed PEs, a proper core-dump hook |
| **Win32 API shims** | The rest of `kernel32`, then `advapi32`/`shlwapi`/`ntdll`, written with `ms_abi` |
| **Exceptions / SEH** | x64 unwind implementation from `.pdata`, C++ exception support |
| **Graphics / UI** | A path from `user32`/`gdi32` onto X11 and Wayland, font rendering, message loop |
| **Testing** | Small PE test programs (mingw-w64) for each API, CI that runs them in a VM |
| **Docs** | Architecture docs, PE format notes, tutorials for new contributors |
| **Security review** | Fuzzing the loader, reviewing parsing code |

**Good first issues**

- Implement `GetStartupInfoW`, the current Notepad++ blocker
- Pick any unimplemented `KERNEL32` import from the verbose build output and write its `ms_abi` shim
- Write a second test program (for example one that prints command-line arguments or calls `GetLastError`) and build it with mingw-w64
- Add `[BRIDGE]` logging helpers
- Write unit tests for the PE-header parser

### Ground rules

1. **Small, verifiable changes.** Include the logs or output that prove it works.
2. **No proprietary or leaked code, ever.**
3. Prefix kernel logs with `[BINFMT_PE]` and bridge logs with `[BRIDGE]`.
4. Keep kernel code minimal; put logic in userland wherever possible.
5. Test in a VM and say how you tested.
6. Be kind. People here have different backgrounds and levels of experience.

### Reporting security issues

Because this is kernel code that parses untrusted input, security reports are especially welcome. Please use the repository's **Security** tab to report vulnerabilities privately rather than opening a public issue.

---

## License

See [`LICENSE`](LICENSE) for the license that covers this project's original code and [`COPYING`](COPYING) plus the `LICENSES/` directory for the Linux kernel's own licensing. Code inherited from the Linux kernel remains under its original license terms. Third-party software in the repository (such as Notepad++) is distributed under its own license.

---

## Credits

- **Developers:** EisaWando, Eisa404
- **Standing on the shoulders of:** Wine, Proton, ReactOS, and the Linux kernel community
- **Tools:** Visual Studio Code, OpenCode, Antigravity
- **AI agents used during development:** Claude Sonnet 4.5, Claude Opus 4.6, Codex OSS 120B (High), Gemini Flash 3.6 (Medium), Gemini Pro 3.1 (High), Qwen3-Coder-next

Project started 24/08/2026 (DD/MM/YYYY). First end-to-end run (`hello.exe`): 18/10/2026.

---

## Community

A Discord community is coming soon.

If you've ever wished you could just double-click a `.exe` on Linux and have it run natively, we'd love your help.
