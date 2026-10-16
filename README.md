# binfmt_pe — Native Windows Executables on Linux, Without Wine

> **Status: early research prototype.** The kernel loader maps real Windows binaries and hands control to a userland runtime. It does **not** run Notepad++ (or any real application) yet. This README is written to be honest about that.

---

## The idea:

`binfmt_pe` is a Linux kernel module that teaches the kernel to recognize Windows x64 PE executables (`.exe`) as a **native binary format**, exactly the way it already recognizes ELF programs and scripts. The kernel parses the PE headers, maps the sections at the image's preferred base address, builds a Windows-style process environment (TEB/PEB/heap), and hands execution to a small userland runtime, `win32_bridge.so`, which resolves the program's imports and provides the Win32 API on top of Linux. There is no emulator, no virtual machine, and no Wine or Proton loader or wineserver in the path.

The first target is the portable x64 build of **Notepad++ 8.9.8**.

---

## Why we are doing this: our mission

We believe Linux users and developers deserve to run the software people actually use, **natively and by default**, without having to keep a Windows installation around.

Many people stay on Windows, or keep a dual-boot partition, for one reason: a handful of applications. When the operating system underneath is one you did not choose and cannot fully control, that has real costs:

- **Privacy.** Operating systems increasingly ship with mandatory accounts, extensive diagnostic collection, and features that analyze what you do on your own machine. Whatever one thinks of any individual feature, the common pattern is that you cannot see or control all of it.
- **Terms you did not negotiate.** People accept long, frequently changing terms and policies as the price of using a computer they paid for. We think the people who own hardware should have final say over who can look at their data and what runs on it.
- **Lock-in.** When the apps you depend on only run on one vendor's OS, your choice of operating system is not really a choice.

This is a criticism of **Windows as an operating system and of the terms attached to it**, not of every Microsoft service, and not of the people who use Windows. Many people have good reasons to be there. Our point is narrower: nobody should *need* a particular OS just to run their software.

Linux already gives users auditable code, control over telemetry, and the right to change anything. What it has lacked is easy access to the Windows application ecosystem. Wine, Proton, ReactOS and others have done heroic work toward that, and this project stands on the shoulders of that history. We want to explore a different point in the design space: a **kernel-integrated, loader-level approach** where a Windows binary is simply an executable.

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

Design rule: **keep the kernel part minimal**. Everything that can live in userland (imports, API behavior, relocations, TLS, UI) lives in `win32_bridge.so`.

### Technical notes for contributors

- **Target:** Windows x64 (PE32+) only, executables first, then DLLs.
- **Calling convention:** Windows x64 (RCX, RDX, R8, R9, 32-byte shadow space, callee-saved XMM6–XMM15) versus the Linux System V ABI. Every shim handed to a PE is `__attribute__((ms_abi))`.
- **TEB via GS:** Windows x64 code reads the TEB through `gs:[0x30]` and the PEB through `gs:[0x60]`. Linux uses FS for libc TLS, so GS is ours to use.
- **Address space:** ImageBase `0x140000000`; bridge at `0x70000000`; TEB/PEB/heap near `0x7ff00000`; 8 MB stack below `0x7ffff7000000`.
- **Imports:** the bridge resolves by name and by ordinal; unimplemented functions get a stub that prints `UNIMPLEMENTED: dll!function` and exits, which turns every missing API into a clear to-do item.
- **Exceptions and unwind:** x64 SEH is table-based (`.pdata`); `RtlLookupFunctionEntry`/`RtlVirtualUnwind`/`RtlUnwindEx` will be needed for C++ and structured exceptions.
- **Diagnostics:** `gdb` and core dumps don't work on this non-ELF process, so the bridge installs its own crash handler (registers, stack, `/proc/self/maps`).

---

## Current status

Done and verified by kernel logs:

- [x] MZ/PE header parsing with bounds checks (PE32+), including data directories (import, IAT, exception, reloc, TLS)
- [x] PE headers mapped read-only at the image base
- [x] All 7 sections of `notepad++.exe` mapped at their virtual addresses
- [x] TEB, PEB and a heap initialized
- [x] Kernel-to-userland handoff into `win32_bridge.so` with a correct stack
- [x] Import walk and per-import stubs in the bridge; unimplemented imports are reported by name

In progress or broken right now:

- [ ] The latest bridge build crashes before printing anything (being bisected)
- [ ] Real `ms_abi` shims (starting with `GetStdHandle`, `WriteFile`, `ExitProcess`, `GetSystemTimeAsFileTime`)
- [ ] GS/TEB verification and calling the PE entry as an `ms_abi` function
- [ ] A minimal `hello.exe` test (console, three imports) as the first end-to-end success

Planned:

- [ ] CRT startup APIs (heap, TLS/FLS, process/thread IDs, performance counters, module handles)
- [ ] TLS directory and callbacks
- [ ] SEH/C++ exception unwinding from `.pdata`
- [ ] user32/gdi32/comctl32 subset mapped onto X11 and Wayland
- [ ] Notepad++ rendering a window

**Realistic expectation:** getting a tiny console program to print "hello" is the near-term milestone. A GUI application with hundreds of imports, GDI text rendering, COM and exception handling is a long road measured in months, and it is exactly where contributors can make the biggest difference.

---

## Honest caveats

- **Security.** A PE loader in the kernel parses untrusted input. A bug there is a kernel bug. The design deliberately keeps the in-kernel part small, but this code needs review before anyone runs it on a machine they care about. Test in a VM.
- **Scope.** Win32 is enormous. We are not promising compatibility with arbitrary software, anti-cheat, DRM-protected applications or software that depends on Windows kernel drivers.
- **Overlap with existing work.** Wine and Proton are mature and far ahead on API coverage. This project is an experiment in a different architecture, not a claim that those projects are unnecessary. If a Wine-derived component (LGPL) turns out to be the right way to implement parts of the API layer, that is an open question for the maintainers and the community.
- **Legality.** This is a clean-room effort: no Microsoft source code, no leaked code, no copied proprietary binaries. Contributors must not submit code derived from leaked or proprietary Windows sources. (Licensing: to be chosen by the maintainers; a kernel module must be GPL-compatible to use most kernel symbols.)

---

## Build and run (development)

```bash
# Kernel module (out-of-tree build against the running kernel's headers)
make LLVM=1 -C /lib/modules/$(uname -r)/build M=$PWD/fs modules   # LLVM=1 only if your kernel is clang-built

# Userland bridge
gcc -fPIC -shared -nostdlib -ffreestanding -o win32_bridge.so win32_bridge.c

# Load (never use rmmod -f unless the module is truly stuck)
lsmod | grep binfmt_pe
sudo rmmod binfmt_pe 2>/dev/null; true
sudo insmod fs/binfmt_pe.ko

# Run and look at logs
./npp.8.9.8.portable.x64/notepad++.exe 2> /tmp/err.txt
sudo dmesg | grep BINFMT_PE | tail -n 40
```

Please use a **virtual machine** for development.

---

## How you can help

We need people who enjoy hard, low-level problems:

| Area | What's needed |
|---|---|
| **Kernel** | Review of `fs/binfmt_pe.c`; safe VMA handling, GS/TEB setup, hardening against malformed PEs, a proper core-dump hook |
| **Win32 API shims** | `kernel32`/`ntdll` first (heap, memory, TLS/FLS, files, time, modules), written with `ms_abi` |
| **Exceptions / SEH** | x64 unwind implementation from `.pdata`, C++ exception support |
| **Graphics / UI** | A path from user32/gdi32 onto X11 and Wayland, font rendering, message loop |
| **Testing** | Small PE test programs (mingw-w64) for each API, CI that runs them in a VM |
| **Docs** | Architecture docs, PE format notes, tutorials for new contributors |
| **Security review** | Fuzzing the loader, reviewing parsing code |

**Good first issues:** write `tests/hello.c`; implement `GetStdHandle` + `WriteFile`; make the bridge's hex printer print all 16 digits; add `[BRIDGE]` logging helpers; write the PE-header unit tests.

### Ground rules

1. Small, verifiable changes. Show the logs or output that prove it works.
2. No proprietary or leaked code, ever.
3. Prefix kernel logs with `[BINFMT_PE]` and bridge logs with `[BRIDGE]`.
4. Keep kernel code minimal; put logic in userland where possible.
5. Be kind. People here have different backgrounds and levels of experience.

Linux kernel
============

The Linux kernel is the core of any Linux operating system. It manages hardware,
system resources, and provides the fundamental services for all other software.

Quick Start
-----------

* Report a bug: See Documentation/admin-guide/reporting-issues.rst
* Get the latest kernel: https://kernel.org
* Build the kernel: See Documentation/admin-guide/quickly-build-trimmed-linux.rst
* Join the community: https://lore.kernel.org/

Essential Documentation
-----------------------

All users should be familiar with:

* Building requirements: Documentation/process/changes.rst
* Code of Conduct: Documentation/process/code-of-conduct.rst
* License: See COPYING

Documentation can be built with make htmldocs or viewed online at:
https://www.kernel.org/doc/html/latest/


Who Are You?
============

Find your role below:

* New Kernel Developer - Getting started with kernel development
* Academic Researcher - Studying kernel internals and architecture
* Security Expert - Hardening and vulnerability analysis
* Backport/Maintenance Engineer - Maintaining stable kernels
* System Administrator - Configuring and troubleshooting
* Maintainer - Leading subsystems and reviewing patches
* Hardware Vendor - Writing drivers for new hardware
* Distribution Maintainer - Packaging kernels for distros
* AI Coding Assistant - LLMs and AI-powered development tools


For Specific Users
==================

Academic Researcher
-------------------

Explore the kernel's architecture and internals:

* Researcher Guidelines: Documentation/process/researcher-guidelines.rst
* Memory Management: Documentation/mm/index.rst
* Scheduler: Documentation/scheduler/index.rst
* Networking Stack: Documentation/networking/index.rst
* Filesystems: Documentation/filesystems/index.rst
* RCU (Read-Copy Update): Documentation/RCU/index.rst
* Locking Primitives: Documentation/locking/index.rst
* Power Management: Documentation/power/index.rst

Security Expert
---------------

Security documentation and hardening guides:

* Security Documentation: Documentation/security/index.rst
* LSM Development: Documentation/security/lsm-development.rst
* Self Protection: Documentation/security/self-protection.rst
* Reporting Vulnerabilities: Documentation/process/security-bugs.rst
* CVE Procedures: Documentation/process/cve.rst
* Embargoed Hardware Issues: Documentation/process/embargoed-hardware-issues.rst
* Security Features: Documentation/userspace-api/seccomp_filter.rst

Backport/Maintenance Engineer
-----------------------------

Maintain and stabilize kernel versions:

* Stable Kernel Rules: Documentation/process/stable-kernel-rules.rst
* Backporting Guide: Documentation/process/backporting.rst
* Applying Patches: Documentation/process/applying-patches.rst
* Subsystem Profile: Documentation/maintainer/maintainer-entry-profile.rst
* Git for Maintainers: Documentation/maintainer/configure-git.rst

System Administrator
--------------------

Configure, tune, and troubleshoot Linux systems:

* Admin Guide: Documentation/admin-guide/index.rst
* Kernel Parameters: Documentation/admin-guide/kernel-parameters.rst
* Sysctl Tuning: Documentation/admin-guide/sysctl/index.rst
* Tracing/Debugging: Documentation/trace/index.rst
* Performance Security: Documentation/admin-guide/perf-security.rst
* Hardware Monitoring: Documentation/hwmon/index.rst

Maintainer
----------

Lead kernel subsystems and manage contributions:

* Maintainer Handbook: Documentation/maintainer/index.rst
* Pull Requests: Documentation/maintainer/pull-requests.rst
* Managing Patches: Documentation/maintainer/modifying-patches.rst
* Rebasing and Merging: Documentation/maintainer/rebasing-and-merging.rst
* Development Process: Documentation/process/maintainer-handbooks.rst
* Maintainer Entry Profile: Documentation/maintainer/maintainer-entry-profile.rst
* Git Configuration: Documentation/maintainer/configure-git.rst

Hardware Vendor
---------------

Write drivers and support new hardware:

* Driver API Guide: Documentation/driver-api/index.rst
* Driver Model: Documentation/driver-api/driver-model/driver.rst
* Device Drivers: Documentation/driver-api/infrastructure.rst
* Bus Types: Documentation/driver-api/driver-model/bus.rst
* Device Tree Bindings: Documentation/devicetree/bindings/
* Power Management: Documentation/driver-api/pm/index.rst
* DMA API: Documentation/core-api/dma-api.rst

Distribution Maintainer
-----------------------

Package and distribute the kernel:

* Stable Kernel Rules: Documentation/process/stable-kernel-rules.rst
* ABI Documentation: Documentation/ABI/README
* Kernel Configuration: Documentation/kbuild/kconfig.rst
* Module Signing: Documentation/admin-guide/module-signing.rst
* Kernel Parameters: Documentation/admin-guide/kernel-parameters.rst
* Tainted Kernels: Documentation/admin-guide/tainted-kernels.rst



## Contact and community

Discord Community is coming soon..

If you've ever wished you could just double-click a `.exe` on Linux and have it run natively, we'd love your help.
