# binfmt_pe — Native Windows Executables on Linux, Without Wine

![Status: early research prototype](https://img.shields.io/badge/status-early%20research%20prototype-orange)
![Target: x86-64 PE32+](https://img.shields.io/badge/target-Windows%20x64%20(PE32%2B)-blue)
![Run in a VM](https://img.shields.io/badge/safety-test%20in%20a%20VM-red)

> **Status: early research prototype.** The kernel loader maps real Windows binaries and hands control to a userland runtime. It does **not** run Notepad++ (or any real application) yet. This README is written to be honest about that.

---

## Table of contents

1. [The idea](#the-idea)
2. [Our mission](#why-we-are-doing-this-our-mission)
3. [How it works](#how-it-works)
4. [Current status](#current-status)
5. [Honest caveats](#honest-caveats)
6. [Getting started](#getting-started)
7. [Repository layout](#repository-layout)
8. [How you can help](#how-you-can-help)
9. [License](#license)
10. [Credits](#credits)
11. [Community](#community)

---

## The idea

`binfmt_pe` is a Linux kernel module that teaches the kernel to recognize Windows x64 PE executables (`.exe`) as a **native binary format**, the same way it already recognizes ELF programs and scripts.

The kernel:

1. parses and validates the PE headers,
2. maps the sections at the image's preferred base address,
3. builds a Windows-style process environment (TEB, PEB, heap),
4. hands execution to a small userland runtime, `win32_bridge.so`, which resolves the program's imports and provides the Win32 API on top of Linux.

There is **no emulator, no virtual machine, and no Wine, Proton or wineserver** in the path.

The first target is the portable x64 build of **Notepad++ 8.9.8**.

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
./notepad++.exe
       │
       ▼
Linux execve() ──► binfmt_pe.ko (kernel module)
       │             • validates MZ/PE headers (bounds-checked)
       │             • maps headers + sections at ImageBase (0x140000000)
       │             • sets up TEB / PEB / heap, GS base
       │             • maps win32_bridge.so at 0x70000000
       │             • builds the stack, start_thread()
       ▼
win32_bridge.so (userland runtime)
       • walks the import table, fills the IAT with shims
       • Win32 API shims (ms_abi) translated to Linux syscalls
       • TLS, exceptions (SEH), later windowing (X11/Wayland)
       ▼
PE entry point (mainCRTStartup) runs as native x86-64 code
```

**Design rule: keep the kernel part minimal.** Everything that can live in userland (imports, API behavior, relocations, TLS, UI) lives in `win32_bridge.so`.

### Technical notes

- **Target:** Windows x64 (PE32+) only; executables first, then DLLs.
- **Calling convention:** Windows x64 (RCX, RDX, R8, R9, 32-byte shadow space, callee-saved XMM6–XMM15) differs from the Linux System V ABI. Every shim handed to a PE is `__attribute__((ms_abi))`.
- **TEB via GS:** Windows x64 code reads the TEB through `gs:[0x30]` and the PEB through `gs:[0x60]`. Linux uses FS for libc TLS, so GS is free for us to use.
- **Address space:** ImageBase `0x140000000`; bridge at `0x70000000`; TEB/PEB/heap near `0x7ff00000`; 8 MB stack below `0x7ffff7000000`.
- **Imports:** the bridge resolves by name and by ordinal. Unimplemented functions get a stub that prints `UNIMPLEMENTED: dll!function` and exits, which turns every missing API into a clear to-do item.
- **Exceptions and unwinding:** x64 SEH is table-based (`.pdata`). `RtlLookupFunctionEntry`, `RtlVirtualUnwind` and `RtlUnwindEx` will be needed for C++ and structured exceptions.
- **Diagnostics:** `gdb` and core dumps don't work on this non-ELF process, so the bridge installs its own crash handler (registers, stack, `/proc/self/maps`).
- **Log prefixes:** kernel messages use `[BINFMT_PE]`; bridge messages use `[BRIDGE]`.

---

## Current status

**Done and verified by kernel logs**

- [x] MZ/PE header parsing with bounds checks (PE32+), including data directories (import, IAT, exception, reloc, TLS)
- [x] PE headers mapped read-only at the image base
- [x] All 7 sections of `notepad++.exe` mapped at their virtual addresses
- [x] TEB, PEB and a heap initialized
- [x] Kernel-to-userland handoff into `win32_bridge.so` with a correct stack
- [x] Import walk and per-import stubs in the bridge; unimplemented imports are reported by name

**In progress or broken right now**

- [ ] The latest bridge build crashes before printing anything (being bisected)
- [ ] Real `ms_abi` shims, starting with `GetStdHandle`, `WriteFile`, `ExitProcess`, `GetSystemTimeAsFileTime`
- [ ] GS/TEB verification and calling the PE entry as an `ms_abi` function
- [ ] A minimal `hello.exe` test (console, three imports) as the first end-to-end success

**Planned**

- [ ] CRT startup APIs (heap, TLS/FLS, process/thread IDs, performance counters, module handles)
- [ ] TLS directory and callbacks
- [ ] SEH/C++ exception unwinding from `.pdata`
- [ ] user32/gdi32/comctl32 subset mapped onto X11 and Wayland
- [ ] Notepad++ rendering a window

**Realistic expectation.** Getting a tiny console program to print "hello" is the near-term milestone. A GUI application with hundreds of imports, GDI text rendering, COM and exception handling is a very long road: years if the project stays a one-person effort, months with a team. That is exactly where contributors can make the biggest difference.

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

# Userland bridge
gcc -fPIC -shared -nostdlib -ffreestanding -o win32_bridge.so win32_bridge.c
```

### Load and run

```sh
# Replace any previously loaded copy (avoid `rmmod -f` unless truly stuck)
lsmod | grep binfmt_pe
sudo rmmod binfmt_pe 2>/dev/null; true
sudo insmod fs/binfmt_pe.ko

# Run the target and inspect the output
./npp.8.9.8.portable.x64/notepad++.exe 2> /tmp/err.txt
cat /tmp/err.txt
sudo dmesg | grep BINFMT_PE | tail -n 40
```

If `insmod` reports `File exists`, the module is already loaded; that is harmless.

### What you should see today

The kernel log shows the PE directories being parsed and the sections mapped. The bridge then walks the imports and stops at the first function that has no shim, printing `UNIMPLEMENTED: dll!function`. With the latest bridge build, the process currently crashes before printing anything while we bisect the cause. Reading `/tmp/err.txt` and the `[BINFMT_PE]` / `[BRIDGE]` log lines is the main way to debug.

---

## Repository layout

This repository is built on a Linux kernel source tree, so most top-level directories (`arch`, `drivers`, `mm`, `kernel`, …) are the unmodified upstream layout. The parts specific to this project are:

| Path | Purpose |
| --- | --- |
| `fs/binfmt_pe.c` | The kernel-side PE loader (`binfmt_pe.ko`) |
| `win32_bridge.c` | The userland runtime built into `win32_bridge.so` |
| `npp.8.9.8.portable.x64/` | The Notepad++ 8.9.8 portable x64 build used as the first target |
| `tests/`, `samples/` | Test programs and samples |
| `scripts/` | Helper scripts |
| `Project.md` | Project overview |
| `log.txt` | Captured logs from development runs |
| `patch*.py`, `bisect_patch*.py`, `patch_bridge.sh` | Development helpers used while patching and bisecting the bridge |

For documentation about the surrounding kernel tree, see `Documentation/` and <https://www.kernel.org/doc/html/latest/>.

---

## How you can help

We need people who enjoy hard, low-level problems.

| Area | What's needed |
| --- | --- |
| **Kernel** | Review of `fs/binfmt_pe.c`; safe VMA handling, GS/TEB setup, hardening against malformed PEs, a proper core-dump hook |
| **Win32 API shims** | `kernel32`/`ntdll` first (heap, memory, TLS/FLS, files, time, modules), written with `ms_abi` |
| **Exceptions / SEH** | x64 unwind implementation from `.pdata`, C++ exception support |
| **Graphics / UI** | A path from user32/gdi32 onto X11 and Wayland, font rendering, message loop |
| **Testing** | Small PE test programs (mingw-w64) for each API, CI that runs them in a VM |
| **Docs** | Architecture docs, PE format notes, tutorials for new contributors |
| **Security review** | Fuzzing the loader, reviewing parsing code |

**Good first issues**

- Write `tests/hello.c` (console, three imports) and build it with mingw-w64
- Implement `GetStdHandle` and `WriteFile` as `ms_abi` shims
- Make the bridge's hex printer print all 16 digits (it currently drops the last nibble)
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

Project started 24/08/2026 (DD/MM/YYYY).

---

## Community

A Discord community is coming soon.

If you've ever wished you could just double-click a `.exe` on Linux and have it run natively, we'd love your help.
