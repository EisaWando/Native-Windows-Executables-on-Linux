# binfmt_pe — Native Windows Executables on Linux, Without Wine

> **Status: working research prototype.** Real Windows x64 programs load and run through our own kernel loader. Console programs run end to end, including multi-threaded ones. **Notepad++ 8.9.8 reaches hidden main-window creation, subclass setup and menu resource loading. It does not show a window yet.** This README is honest about what works, what doesn't, and where help is needed.

---

## The idea in one paragraph

`binfmt_pe` is a Linux kernel module that teaches the kernel to recognize Windows x64 PE executables (`.exe`) as a **native binary format**, the same way it already recognizes ELF programs. The kernel parses the PE headers, maps the sections at the image's preferred base address, builds a Windows-style process environment (TEB/PEB), and hands execution to a small freestanding userland runtime, `win32_bridge.so`, which resolves the program's imports and implements the Win32 API on top of raw Linux system calls. There is no emulator, no virtual machine, and no Wine or Proton loader or wineserver in the path.

The first big target is the portable x64 build of **Notepad++ 8.9.8**.

---

## Why we are doing this: our mission

We believe Linux users and developers deserve to run the software people actually use, **natively and by default**, without keeping a Windows installation around.

Many people stay on Windows, or keep a dual-boot partition, for one reason: a handful of applications. When the operating system underneath is one you did not choose and cannot fully control, that has real costs:

- **Privacy.** Operating systems increasingly ship with mandatory accounts, extensive diagnostic collection, and features that analyze what you do on your own machine. Whatever one thinks of any individual feature, the common pattern is that you cannot see or control all of it.
- **Terms you did not negotiate.** People accept long, frequently changing terms and policies as the price of using a computer they paid for. We think the people who own hardware should have the final say over who can look at their data and what runs on it.
- **Lock-in.** When the apps you depend on only run on one vendor's OS, your choice of operating system is not really a choice.

This is a criticism of **Windows as an operating system and of the terms attached to it**, not of every Microsoft service, and not of the people who use Windows. Many people have good reasons to be there. Our point is narrower: nobody should *need* a particular OS just to run their software.

Linux already gives users auditable code, control over telemetry, and the right to change anything. What it has lacked is easy access to the Windows application ecosystem. Wine, Proton, ReactOS and others have done heroic work toward that, and this project stands on the shoulders of that history. We want to explore a different point in the design space: a **kernel-integrated, loader-level approach** where a Windows binary is simply an executable.

---

## How it works

```
 ./notepad++.exe
        |
        v
 Linux execve()  --->  binfmt_pe.ko  (kernel module)
        |               - validates MZ/PE headers (bounds-checked)
        |               - maps headers + sections at ImageBase (0x140000000)
        |               - sets up TEB / PEB, loads win32_bridge.so by its
        |                 ELF PT_LOAD segments at 0x70000000
        |               - builds the stack, start_thread()
        v
 win32_bridge.so  (freestanding userland runtime, no libc)
        - sets up GS (TEB), the PEB and the TLS directory
        - walks the import table, fills the IAT with ms_abi shims
        - Win32 API shims implemented with raw Linux syscalls
        - calls the PE entry point as a Windows x64 function
        v
 PE entry point (mainCRTStartup) runs as native x86-64 code
```

Design rule: **keep the kernel part minimal.** Everything that can live in userland lives in `win32_bridge.so`.

### Technical notes for contributors

- **Target:** Windows x64 (PE32+) only; executables first, then DLLs.
- **Calling convention:** Windows x64 (RCX, RDX, R8, R9, 32-byte shadow space, RSP 16-byte aligned *before* a call, callee-saved XMM6-XMM15) versus Linux System V. Every function handed to a PE is `__attribute__((ms_abi))`. (An early bug: calling the PE entry with RSP off by 8 made GCC's `movaps` prologues fault.)
- **TEB via GS:** Windows x64 code reads the TEB through `gs:[0x30]`, the PEB through `gs:[0x60]`, TLS through `gs:[0x58]` and `gs:[0x1480]`, and the last error at `gs:[0x68]`.
- **Address map:** ImageBase `0x140000000`; bridge `0x70000000` (fixed 1 MiB window, about 30% used); fake module handles `0x6ff00000+`; TEB/PEB near `0x7ff00000`; main stack below `0x7ffff7000000`.
- **Imports:** resolved by name and by ordinal. A function without a real implementation gets a stub that, *when called*, prints `CALLED UNIMPLEMENTED: dll!function`, its caller, argument registers (with UTF-16 string previews) and a stack backtrace, then exits with status 127. That turns every missing API into a precise to-do item.
- **`GetProcAddress`** returns NULL for anything not really implemented (never a stub), because the C runtime probes for optional APIs.
- **Threads:** real threads via `clone`; each has its own stack, TEB, TLS/FLS storage and signal stack; wait objects (events, mutexes, semaphores, threads) are built on futexes.
- **Handles:** opaque, non-zero multiples of 4 mapped to Linux file descriptors or to bridge objects by helper functions; GUI objects use a separate generation-checked table that grows on demand.
- **Heap:** our own allocator (size-class free lists on `mmap` arenas, thread-safe) behind `GetProcessHeap`/`HeapAlloc`/`HeapFree`/`HeapReAlloc`.
- **Paths:** `Z:\` maps to the Linux root `/`; other drive letters are not supported yet.
- **Diagnostics:** `gdb` and core dumps do not work on this non-ELF process, so the bridge installs its own crash handler (registers, stack qwords, `/proc/self/maps`, thread id), per thread.

---

## Current status

### Verified by real runs (`bash tests/run_all.sh`)

- [x] Hidden/zero-size windows, properties, subclass chains and cross-thread messages
- [x] Headless system/common controls with tested state for selected messages
- [x] Main-image PE resources, menu templates, strings and accelerator tables
- [ ] Complete control behavior, resource image rasterization and dialogs
- [ ] Native menu strip/popup display verification (presenter implemented)

- [x] MZ/PE (PE32+) parsing with bounds checks; PE headers and all sections mapped at their virtual addresses
- [x] Kernel loads the bridge by ELF `PT_LOAD` segments and hands off with a correct stack
- [x] TEB/PEB/GS set up and self-tested; **TLS directory** processed (template, zero-fill, alignment, callbacks)
- [x] Import resolver (by name/ordinal) with diagnostic stubs
- [x] **Milestone: Windows console programs run end to end** (print text, correct exit codes)
- [x] **Milestone: real multi-threading** (`CreateThread`, suspended start, exit codes, per-thread TLS/FLS, crash dumper per thread) with a 20-run stress test: 8 threads, 80,000 atomic increments, 80,000 locked increments, 16,000 mixed heap operations per run
- [x] Synchronization: critical sections, SRW locks, condition variables, events, mutexes, semaphores, `WaitForSingleObject`/`WaitForMultipleObjects`
- [x] Thread-safe heap allocator; module handles (`LoadLibraryExW`, `GetModuleHandleW`, `GetProcAddress`)
- [x] Files: handle model, file metadata, synchronous file I/O, directory enumeration (`FindFirstFileW` family), drive-type and free-space queries, Windows-style path helpers (`Z:\` mapping)
- [x] Text: UTF-8 <-> UTF-16 conversion with edge-case tests, code pages, string helpers
- [x] Time: clocks and file-time conversions (local time is UTC for now), a consistent Windows 10 x64 system profile
- [x] GDI/USER object tables, window class registration and system metrics
- [x] First visible Unicode Win32 windows: message loops, software fills/lines/pixels, FreeType text, X11/XWayland presentation, resize and basic keyboard/mouse input
- [x] C++ exception metadata decoder (diagnostic) and `RtlPcToFileHeader`
- [x] Process-local registry tree with A/W APIs and a seeded Windows 10 profile; Notepad++ reaches `bcrypt!BCryptOpenAlgorithmProvider` for SHA256. It does not yet display its main window.

### In progress / next
- [ ] Registry persistence, complete Unicode name folding and security descriptors
- [ ] Full GUI compatibility: child windows/controls, menus/dialogs, cross-thread SendMessage, complete GDI, complex text shaping and native Wayland
- [ ] x64 structured/C++ exception handling: `.pdata` unwind (`RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlUnwindEx`, `RaiseException`)
- [ ] Local time zones, APCs, `TerminateThread`, cross-process named objects, TLS for loaded DLLs, loading real PE DLLs
- [ ] Case-insensitive path handling for `CreateFileW` and friends
- [ ] Per-section memory protections

### Import profile of Notepad++ (620 imports)
USER32 216, KERNEL32 192, GDI32 66, ADVAPI32 26, COMCTL32 24, SHLWAPI 17, UxTheme 16, SHELL32 12, ole32 12, IMM32 9, CRYPT32 8, bcrypt 7, and about a dozen DLLs with 1-3 imports each. The window system (USER32 + GDI32) is more than 45% of the surface and is the main remaining cost.

### GUI design

The owner selected an original Windows compatibility layer. The bridge owns
Windows handles, window procedures and per-thread message queues. A separate
native `pe_gui_host` process owns X11 and software framebuffers, with Fontconfig
and FreeType for fonts. It runs through the normal Linux loader, leaving the
bridge freestanding. There is no Wine/ReactOS code or Windows graphics kernel ABI.
See [the design](docs/GUI_DESIGN.md).

The first milestone supports custom Unicode top-level windows. The demo draws a
banner and text, repaints on resize, and closes with Escape or the window-manager
close button. This is not yet a general Windows desktop/control implementation;
Notepad++ still stops at its unimplemented SHA256 provider call.

---

## Honest caveats

- **Security.** A PE loader in the kernel parses untrusted input. A bug there is a kernel bug. The design keeps the in-kernel part small, but this code needs review before anyone runs it on a machine they care about. **Test in a virtual machine.**
- **Scope.** Win32 is enormous. We are not promising compatibility with arbitrary software, anti-cheat, DRM-protected applications or anything that needs Windows kernel drivers.
- **Overlap with existing work.** Wine and Proton are mature and far ahead in API coverage. This project is an experiment in a different architecture, not a claim that those projects are unnecessary. If a Wine-derived LGPL component turns out to be the right way to implement parts of the API layer, that is an open question for the maintainers and the community, and it needs a licensing review first.
- **Legality.** This is a clean-room effort: no Microsoft source code, no leaked code, no copied proprietary binaries. Contributors must not submit code derived from leaked or proprietary Windows sources. (License: to be chosen by the maintainers; a kernel module must be GPL-compatible to use most kernel symbols.)
- **Notepad++** is a separate GPL-licensed project used here as a test program. Get it from its official release page; it is not part of this project.

---

## Build and run (development)

```bash
# Kernel module (built against the running kernel's headers; LLVM=1 only if your kernel is clang-built)
make LLVM=1 -C /lib/modules/$(uname -r)/build M=$PWD/fs modules

# Userland bridge + native display helper (Xlib, FreeType, Fontconfig development files)
bash gui/build.sh

# Load the module only if needed (never use rmmod -f)
lsmod | grep binfmt_pe
sudo rmmod binfmt_pe 2>/dev/null; true
sudo insmod fs/binfmt_pe.ko

# Regression suite: builds the test PEs with mingw-w64, runs them, then runs Notepad++ (informational)
bash tests/run_all.sh

# On an X11/XWayland desktop: include visible windows and X-server pixel/input checks
PE_GUI_TEST=1 bash tests/run_all.sh

# Interactive demo (Escape or the close button exits)
./tests/window_demo.exe
```

`bash run2.sh` does the build, module reload and a Notepad++ run in one go. Changing only `win32_bridge.c` needs a bridge rebuild but no module reload (the module reads the bridge from disk at exec time). The helper needs `pkg-config`, Xlib, FreeType and Fontconfig development files. The regression script rebuilds both helper and bridge. `gui/build.sh` records the helper location; use `BINFMT_PE_GUI_HELPER` to override it after moving the build. The test PEs need `mingw-w64` (`sudo pacman -S mingw-w64-gcc` on Arch-based systems). `HANDOFF_v3.md` documents the working method in detail.

Please use a **virtual machine** for development.

---

## How you can help

| Area | What's needed |
|---|---|
| **GUI architecture** | Extend child windows, controls, menus, message semantics, GDI and the Wayland backend |
| **Exceptions / SEH** | x64 unwind and exception dispatch from `.pdata`, MSVC C++ exception support, with assembly-based tests |
| **Registry and system services** | Registry persistence/security, local time zones, COM basics |
| **Kernel** | Review of `fs/binfmt_pe.c`; per-section protections, GS/TEB setup in-kernel, hardening against malformed PEs, a core-dump hook |
| **Win32 API shims** | Remaining `kernel32`/`advapi32`/`shell32` coverage, then `user32`/`gdi32` |
| **Testing** | More PE test programs for each API group, a CI job that runs them in a VM |
| **Security review** | Fuzzing the PE loader, reviewing parsing code |
| **Docs** | Architecture docs, PE format notes, tutorials for new contributors |

**Good first issues:** add a PE test for an API group (registry, time, strings); write a `.seh_handler`-based exception test; review the bridge's bounds checks; add `docs/ARCHITECTURE.md`.

### Ground rules

1. Small, verifiable changes. Show the logs or output that prove it works (`bash tests/run_all.sh` must stay green).
2. No proprietary or leaked code, ever.
3. Prefix kernel logs with `[BINFMT_PE]` and bridge logs with `[BRIDGE]`.
4. Keep kernel code minimal; put logic in userland where possible.
5. Be kind. People here have different backgrounds and levels of experience.

---

## FAQ

**Is this "Wine without Wine"?**
In spirit, an alternative architecture with the same goal: running Windows programs on Linux. The difference is where the loading happens (in the kernel via `binfmt`) and the intent to make a `.exe` behave like any other executable.

**Does Notepad++ run?**
Its startup code creates a hidden main window and loads its menu resource. It does not show a window yet.

**Will it run games / Photoshop / my work software?**
Not today, and possibly not for a long time. Console programs and a small custom-window GUI demo work; complex applications remain unsupported.

**Does this replace Windows?**
No. It aims to remove one reason people feel they can't leave.

**Why not just use Wine/Proton?**
You should, if it works for you; they are excellent. This is a research effort exploring a different design.

---

## Contact and community

*(Add your repository URL, chat channel, and a contact method here.)*

If you've ever wished you could just double-click a `.exe` on Linux and have it run natively, we'd love your help.
