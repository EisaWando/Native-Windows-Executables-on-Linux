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

- Workspace: `/home/eisa/Desktop/linux-7.1.6` (kernel source tree used as the build workspace). Running kernel: `7.3.0-2-cachyos` (clang-built, so **`LLVM=1`** is required). The module is built against the running kernel's headers.
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

### Latest: Notepad++ continuation, round 3 (2026-10-24)

bridge/directories.inc adds A/W SetCurrentDirectory, CreateDirectory,
RemoveDirectory and GetFullPathName using the existing Z: mapping. Full paths
normalize dot/parent segments syntactically without requiring the target to
exist, report required sizes and return file-part pointers into caller buffers.
Cwd is process-wide (Linux chdir matches this model). Unsupported drives and
security attributes fail explicitly. Added checked/rounded signed MulDiv for
Notepad++ DPI/font initialization. Tests/directories.c verifies cwd restore,
path/size semantics, native directory creation/removal and nonempty errors,
plus rounding/overflow/divide-by-zero. An initial fixture build required moving
a large local buffer to static storage to avoid an unresolved MinGW chkstk.

`bash tests/run_all.sh > /tmp/npp-round3-suite.log 2>&1` -> exit 0; all functional
fixtures including directories pass, 20/20 thread stress and diagnostics pass.
Visible tests explicitly skipped (previous round verified the unchanged GUI).
`git diff --check` passes; bridge PT_LOAD extent 252656 / 1048576 bytes.
Notepad++ now exits 127 at ADVAPI32!AllocateAndInitializeSid,
ret=0x1400d8285, authority 5, subauthorities 32/544 (Administrators). Disassembly
shows CheckTokenMembership(NULL, sid, ...) then FreeSid. Next batch handles SID
and read-only process-token identity. No kernel changes/reload/push.

### Latest: Notepad++ continuation, round 2 (2026-10-24)

Added DwmGetColorizationColor (fixed opaque ARGB ff0078d4 accent),
DwmIsCompositionEnabled (false: no Windows compositor), IsThemeActive /
IsAppThemed / GetThemeAppProperties (classic/no native Windows theme engine),
and SHLWAPI ColorRGBToHLS/ColorHLSToRGB/ColorAdjustLuma. HLS uses 0..240;
luminance adjustment uses documented 0.1-percent units. Tests/colors.c covers
primary colors, quantized roundtrips, endpoints, profile values and validation.

Owner observed demo resize flicker. gui/host.c now disables automatic X11
background clearing, requests NorthWest bit gravity to retain existing pixels,
coalesces pending ConfigureNotify events, skips intermediate expose events,
uploads to a server-side pixmap and presents via a single XCopyArea. This avoids
visible partial XPutImage requests. It reduces known flicker sources; subjective
smoothness during interactive dragging still needs owner feedback. Display tests
now issue a burst of resizes before checking final pixels/repaint.

`PE_GUI_TEST=1 bash tests/run_all.sh > /tmp/npp-round2-suite.log 2>&1` -> exit 0.
All functional fixtures including crypto/colors/windows pass, actual X-server
pixel/text/resize/key/close checks pass, 20/20 thread stress and both diagnostic
fixtures pass. `git diff --check` passes. Bridge text 171304, bss 68336;
PT_LOAD extent 248560 / 1048576. Notepad++ now exits 127 at
KERNEL32!SetCurrentDirectoryW ret=0x14020e73c, portable directory on Z:.
It generated contextMenu.xml, left untracked. No kernel changes/reload/push.

### Latest: Notepad++ continuation, round 1 (2026-10-24)

Implemented original streaming SHA-256 (FIPS 180-4), HMAC (RFC 2104) and BCrypt
provider/property/create/update/finish/destroy/close, duplicate/reusable/one-shot
hash and system RNG exports in bridge/crypto.inc. SHA256 only; unsupported
algorithms/providers fail explicitly. NTSTATUS returns preserve last error.
Handles are monotonic, typed and mmap-backed in 0x46000000–0x47ffffff; global
lock pins lifetime, per-object futex locks serialize hashing without monopolizing
the runtime lock. Closing a provider does not invalidate existing hash state.
No AF_ALG dependency, external crypto library, Wine/ReactOS code or kernel change.
References: https://csrc.nist.gov/pubs/fips/180-4/upd1/final and
https://www.rfc-editor.org/rfc/rfc4231 . Constants generated from the standard's
prime-root definition. Tests cover standard empty/abc/million-a SHA256 vectors,
RFC HMAC short/long keys, duplication, reusable reset, invalid/stale handles,
property sizes, provider lifetime, random bytes and last-error preservation.

`bash tests/run_all.sh > /tmp/npp-round1-suite.log 2>&1` exited 0: crypto and all
functional fixtures pass, 20/20 thread stress runs and diagnostics pass; display
tests explicitly skipped in this headless run. `git diff --check` passes.
`size`: text 168652, data 0, bss 68336; PT_LOAD extent 244464 / 1048576.
Notepad++ now exits 127 at dwmapi.dll!DwmGetColorizationColor,
ret=0x140018691 (theme initialization); HMAC-SHA256 startup got past bcrypt.
Next round handles theme/color profile and resize flicker. No push.

### Latest: first visible original USER32/GDI window (2026-10-24)

The owner approved building the Windows graphics layer from scratch. Implemented
an original Windows-facing layer in `gui/bridge.inc` (included by
win32_bridge.c) and a native X11 software-rendering helper in `gui/host.c`.
No Wine/ReactOS code, new kernel graphics ABI, kernel changes, sudo, or module
reload. Windows apps use standard USER32/GDI imports. The freestanding bridge
owns Windows ABI callbacks, generational HWND/HDC validation, window lifetime,
class references and per-thread Windows message queues. A separate native ELF
helper per GUI thread uses Xlib, Fontconfig and FreeType through the normal
Linux loader. This avoids libc/ELF-loader/TLS integration inside the bridge.
XWayland is the initial Wayland-desktop path. See `docs/GUI_DESIGN.md`.

New window/message exports: CreateWindowExW, DestroyWindow, DefWindowProcW,
CallWindowProcW, SendMessageW (same-thread), PostMessageW, PostThreadMessageW,
PostQuitMessage, ShowWindow, IsWindow/IsWindowVisible/IsWindowUnicode,
GetWindowThreadProcessId, GetClientRect/GetWindowRect, SetWindowTextW,
GetWindowTextW/LengthW, Get/SetWindowLongPtrW (userdata/wndproc/extra storage),
InvalidateRect/ValidateRect, GetMessageW, PeekMessageW, TranslateMessage,
DispatchMessageW and UpdateWindow. Creation calls WM_NCCREATE/WM_CREATE with
an ABI-checked CREATESTRUCTW. Rejection and destruction during creation are
tested. Destruction calls WM_DESTROY/WM_NCDESTROY; unregistering a class with
live windows fails. Window callbacks never run with the bridge state lock held.
Headless per-thread message queues also work without opening the display.

Drawing exports: BeginPaint/EndPaint, FillRect, Rectangle, MoveToEx/LineTo,
SetPixel/GetPixel, TextOutW, SetTextColor/SetBkColor/SetBkMode. GetDC/ReleaseDC
now support actual window DCs and present on release. The helper owns bounded
per-window software RGB framebuffers, clips to the client area, rasterizes
solid fills/one-pixel lines and FreeType grayscale glyphs, and presents with
XPutImage. Fontconfig substitutes unavailable faces; the screenshot uses sans.
Default WM_ERASEBKGND uses the registered class brush. Default WM_PAINT validates
and presents through BeginPaint/EndPaint. PAINTSTRUCT/MSG layouts are asserted.

Transport is a pointer-free, version-tagged, fixed-layout SOCK_SEQPACKET channel.
The bridge uses fork + raw-syscall execve of the helper; the child only keeps
its channel at fd 3 (plus stdio). No bridge lock is held over IPC/display waits.
Replies have a five-second timeout, sends are nonblocking/MSG_NOSIGNAL, and
failed helpers are killed/reaped. Parent death terminates the helper; ExitThread
destroys that thread's windows and closes/reaps its helper. X events are polled
with a 16 ms maximum wait so queued cross-thread posts are noticed. Native expose,
resize, focus, basic mouse/button and key events become Windows messages;
WM_DELETE_WINDOW becomes WM_CLOSE. TranslateMessage supports ASCII WM_CHAR.

Build: `bash gui/build.sh` builds the helper with pkg-config x11/freetype2/
fontconfig, then uses the exact section-2 GCC bridge flags. It generates ignored
`gui/host_path.h` recording the helper's absolute location. A direct bridge
compile without this generated header falls back to ./pe_gui_host; the
BINFMT_PE_GUI_HELPER environment variable overrides either default. Host/font
libraries and their development headers are new build dependencies, not bridge
link dependencies. tests/run_all.sh rebuilds helper/bridge and all PE fixtures.
Use `env PE_GUI_TEST=1 bash tests/run_all.sh` for visible display tests; default
runs print an explicit SKIP for those and still test queues headlessly. An
interactive demo is `./tests/window_demo.exe` after running the harness; Escape
or the title-bar close button exits. No module reload is needed.

Verification: first direct tests/windows.exe run passed on the owner's XWayland
screen (:0, 1366x768, 24-bit default depth). Sandbox display connections are
blocked; visible tests were explicitly approved outside it. A first full visible
suite failed in the *test driver* with X BadDrawable because title-only discovery
could select a window-manager frame. The driver now checks the client WM_CLASS
BinfmtPE and handles disappearing unrelated X windows. The corrected complete
`PE_GUI_TEST=1 bash tests/run_all.sh > /tmp/gui-final-suite.log 2>&1` exited **0**.
All existing functional fixtures, queues, windows, missing-helper and unavailable-
display failure tests, 20/20 thread stress runs, guard-page crash diagnostics and
synthetic exception diagnostics pass. tests/windows.c checks Unicode creation,
callback rejection/reentrancy, class lifetime, userdata/extra bytes, message
filtering, auto background erasure, color readback, actual font glyph pixels,
line drawing, paint lifecycle, and two GUI threads with separate helpers.
tests/gui_display.py independently reads the demo window with XGetImage and
verifies background/rectangle/text pixels, resize repaint and injected Escape /
WM_DELETE_WINDOW closure. Both display paths passed. Actual captured client
image: /tmp/pe-window-demo.ppm (PNG preview /tmp/pe-window-demo.png, untracked).
`git diff --check`, shell syntax checks and Python compilation passed.

Measured bridge: text=159581, data=0, bss=68304; PT_LOAD extent=236240 / 1048576,
**812336 bytes free**. Window/queue metadata and framebuffer storage are dynamic;
framebuffers live in the helper, outside the fixed bridge mapping.

Scope/limits: this is the first custom-Unicode-top-level-window milestone, not
complete USER32/GDI compatibility. No child/owned/message-only windows, menus,
standard controls/dialogs, extended window styles, ANSI window creation, cross-
thread SendMessage, native Wayland, IME/complex text shaping, timers or complete
keyboard/mouse semantics. Unsupported creation modes return error 50; cross-
thread synchronous window operations return error 5. Drawing to memory DCs,
bitmap blits, arbitrary clipping, non-solid/wide pens and complete font style/
layout semantics remain unsupported. Invalidation coalesces to the whole client
area; window sizes/rectangles currently describe X11 client geometry and do not
model Windows nonclient frame extents. The screen metrics remain the previous
fixed profile. Each window is limited to 4096x4096; text requests are bounded by
the 1024-byte wire payload; fonts clamp to 256 pixels. Stable window metadata is
retained until process exit to keep reentrant/queued references safe. This is a
future reclamation task, not an unbounded static-BSS allocation.

Notepad++: **191 resolved / 429 unimplemented** imports, still informational exit
127 at bcrypt.dll!BCryptOpenAlgorithmProvider ret=0x140116257, SHA256, flags=8.
The new GUI fixture is independent of this unrelated crypto startup blocker.
Stop after this milestone. No push.

### Latest: Round A — process-local registry (2026-10-24)

Bridge-only registry implementation: UTF-16 node/value names and string payloads in stable mmap allocations; raw binary/DWORD/QWORD bytes preserved. A variants use the bridge UTF-8 converters. Global runtime lock serializes tree and handle operations. Opened keys use PE_REGKEY objects in the existing 0x60000000–0x6dffffff handle registry; each handle stores access rights. All five requested sign-extended predefined HKEYs map to independent roots. RegCloseKey accepts predefined keys without closing them and rejects stale handles; wait APIs reject registry handles. Registry APIs return LSTATUS directly and preserve last error.

Implemented A/W: RegOpenKeyEx, RegCreateKeyEx, RegQueryValueEx, RegSetValueEx, RegEnumKeyEx, RegEnumValue, RegQueryInfoKey, RegDeleteKey, RegDeleteValue and RegGetValue; plus RegCloseKey and RegFlushKey. Supports empty/default names, duplicate opens, creation disposition, required-size queries, MORE_DATA, NO_MORE_ITEMS, empty class metadata, access checks, generic access expansion and zero-on-failure/type filtering for RegGetValue. Strings are normalized to include terminators (double terminator for MULTI_SZ), as requested. Unlike native RegQueryValueEx, malformed unterminated strings cannot be stored verbatim in this model. RegGetValue leaves unresolved environment references unchanged under the existing empty-environment profile. Registry has no persistence, ACL/security-descriptor model, separate WOW64 views, notifications, or remote namespace. Name folding currently covers ASCII, Latin-1 uppercase and basic Cyrillic, not complete Unicode casing. Deleted nodes remain mapped until process exit so existing handles never dangle; deleted handles reject operations but can be closed. Class/security creation parameters are not modeled.

Seeded only HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion and HKLM\SOFTWARE\Microsoft\Cryptography (plus ancestors). ProductName Windows 10 Pro, CurrentVersion 6.3 (legacy compatibility value), CurrentBuild/CurrentBuildNumber 19045, major/minor DWORD 10/0, EditionID Professional, InstallationType Client, DisplayVersion 22H2. Version shims and seed data share PE_WIN_MAJOR/MINOR/BUILD constants. MachineGuid is fixed at 73b34075-94af-4b77-bcf1-c21a692c3a10. Other absent paths return FILE_NOT_FOUND; nothing is read from a host registry.

New tests/registry.c is built/checked by run_all.sh and covers A/W UTF conversion, default/multi/binary/QWORD values, missing/invalid/read-only handles, short buffers, enumeration/info, create/delete/reopen, stale deleted keys, predefined closes, preserved last error, RegGetValue flags and concurrent creation/writes by four threads. Public ABI/semantics checked against installed MinGW winreg.h and Microsoft Learn RegQueryValueExA / RegGetValueA; no Wine/ReactOS code used.

The initial attempt stopped at hello exit 126 because binfmt_pe was unavailable. Owner then loaded the existing module with insmod; no kernel changes or bridge-driven reload occurred. First full suite passed, then `bash tests/run_all.sh > /tmp/registry-round-a-final.log 2>&1` exited 0 and verified the additional non-waitable registry handle check. All functional fixtures, 20/20 thread stress runs, guard-page crash dumper and exception diagnostics passed. `git diff --check` and `bash -n tests/run_all.sh` passed. Notepad++ now resolves 156 imports / leaves 464 unimplemented and exits 127 at bcrypt.dll!BCryptOpenAlgorithmProvider ret=0x140116257, algorithm SHA256, flags=8. Stop here after Round A; no crypto shim added. Measured bridge: text 134338, data 0, bss 68304; PT_LOAD extent 211664 / 1048576 bytes (836912 bytes free).

### Latest: real-thread integration (2026-10-24)

All requested core thread stages are implemented bridge-only. Kernel source/module were not changed or reloaded. Local commits only; no push. After the thread stages, eight startup API batches were completed (files, strings, USER registration, folders, copy/handle metadata, calendar/timezone, directory enumeration, native filesystem queries), plus a wait-latching correction.

**Stage 0 evidence:** objdump at 0x140067d00 shows SetLastError(0) via IAT 0x14044d7e0, GetFileAttributesExW via 0x14044d898, then GetLastError via 0x14044d850 on failure. It stores attributes/success/error in the caller-provided record and returns 0. The creator at 0x140067d50 creates it at 0x140067e50, waits at 0x140067e6d, optionally calls TerminateThread on timeout, and closes the handle. Initial stack-scan candidates connect CRT/static initialization (0x140417d2a -> 0x1400014b9) through singleton initialization at 0x140017cc0 / constructor code around 0x14014cd03 to that timeout helper. These are disassembly-based roles, not verified source-level symbols.

**Stage 1 (`8f351a719`):** main TEB's original 4 KiB mapping is extended by a second page without replacing the PEB. API TLS values live in TEB+0x1480 (64 slots); allocation/generations are global and locked. Indices >=64 fail with error 87. FLS has a separate 128-slot allocation/callback registry and per-thread array at TEB+0x17c8. Generation checks prevent stale values after slot reuse. FlsFree snapshots and clears live values under lock, then invokes destructors outside the lock. Thread exit makes up to four destructor passes. The shared heap, GUI/class/resource tables and builtin HMODULE registry use a recursive TID-owned global spin/futex lock. Critical sections are recursive TID-owned locks with real contention waits and try-enter behavior. Do not hold the bridge lock across PE callbacks or blocking I/O.

**Stage 2 (`94cc9efed`):** assembly `pe_clone` uses flags 0x350f00 (VM/FS/FILES/SIGHAND/THREAD/SYSVSEM/PARENT_SETTID/CHILD_CLEARTID). The child pops its start record from the new stack, installs GS with arch_prctl in assembly, then enters C. Each thread has a zeroed 0x2000-byte TEB, PID/TID, shared PEB pointer, stack bounds, API TLS/FLS storage, separate compiler-TLS array/template block, and a private 64 KiB signal alt stack. Stack reserve comes from the PE optional header or dwStackSize, at least 1 MiB, plus a PROT_NONE guard page. Compiler TLS callbacks run with THREAD_ATTACH before entry and THREAD_DETACH before FLS teardown at exit. PE entry is called via ms_abi. Bridge-private TEB fields: +0x1700 current thread record, +0x1800 API-TLS generations.

Thread/event/mutex/semaphore objects share a refcounted registry with non-reused handles in 0x60000000–0x6dffffff. APIs: CreateThread (including CREATE_SUSPENDED), ResumeThread, ExitThread, GetExitCodeThread, GetCurrentThread pseudo-handle resolution, CloseHandle, single/multiple wait APIs and Ex variants, event/mutex/semaphore A/W creation and signaling/release, Sleep/SleepEx, SwitchToThread, 32-bit Interlocked exports. One global sequence/futex drives wait objects. Wait-all acquisition is atomic; wait-any chooses the lowest ready index; abandoned mutexes are reported. Pending waits are satisfied/latched under the state lock when signaled, so ResetEvent or repeated auto-event signals cannot revoke a granted wake (`e7855c679`). Timeouts use monotonic relative futex deadlines. Handle close pins and waits through the done-to-kernel-exit interval before freeing stack/TEB/TLS/FLS/alt-stack mappings. The kernel-cleared TID, not the logical done flag, is the reclamation fence. Handles closed while still running are reaped on later object operations. Main process metadata is retained until process exit.

**Stage 3 (`998de78cd`, plus final ABI checks):** run_all.sh runs tests/threads.exe 20 times. Each run starts eight workers, each performing 10,000 Interlocked increments, 10,000 critical-section counter increments and 2,000 mixed heap allocations/frees (including large blocks and realloc/zero-extension checks). It verifies private TLS/FLS values and compiler TLS, callbacks/destructors, last errors, event handshake, semaphore and named-object semantics, suspended/resumed and explicitly exited threads, wait-all/any, abandoned mutexes, closing a live thread handle, and exported Interlocked functions. Direct GS reads check TlsSlots/FlsData, distinct TEBs, shared PEB, PID/TID and stack bounds. Additional PEs verify PE entry return -> exit_group(37), child ExitProcess -> exit_group(23), and a child fault with RSP in its guard page -> private alt-stack dumper with matching TID and exit 139. Crash handling covers SIGSEGV/SIGILL/SIGBUS and prints the current TID. All regressions pass; Notepad++ remains informational.

**Follow-up support:** synchronous native file attributes/open/read/write/seek/size/copy/flush and find-first/next/close APIs, data-only message/clipboard/legacy-class registration, lstr helpers, shell folder/system/temp paths, Gregorian FILETIME/SYSTEMTIME conversions, and native filesystem/drive queries. Directory searches use pinned records and per-search futex locks outside the global runtime lock. Windows search handles occupy 0x48000000–0x4fffffff. File I/O pins descriptors with F_DUPFD_CLOEXEC across concurrent CloseHandle. Directory wildcard support is * / ? with ASCII case folding and the *.* special case, not all DOS wildcard semantics; alternate 8.3 names are empty.

The existing Z: -> Linux / mapping remains. Program Files maps to /opt, common program resources to /usr/share, system to /usr/lib, Windows root to /, common data to /var/lib, and user data to HOME/XDG locations. Unsupported CSIDLs fail. Folder APIs do not create profile directories. The host environment is read privately for these locations; the Win32 environment block remains the previous empty approximation. File sharing/ACLs/overlapped I/O and complete CopyFile metadata preservation are not implemented. Comparisons/case folding remain approximate (ordinal/ASCII for lstr). Local-time APIs use an explicit UTC profile; supplied fixed timezone biases work, DST rules return error 50. Named synchronization and USER registries are process-local. APC delivery, forced TerminateThread, thread pools, TLS expansion slots, dynamically loaded DLL TLS and SEH/C++ unwinding are still unsupported; no fake successful thread termination is returned.

**Final actual verification:** section-2 GCC build; `bash tests/run_all.sh > /tmp/thread-final-suite.log 2>&1` -> exit 0. All original tests, files/calendar, thread smoke, entry-return/group-exit, 20/20 stress runs, guard-page crash dumper and synthetic exception diagnostics pass. `git diff --check` passes. `size win32_bridge.so`: text=117,796, data=0, bss=68,240. PT_LOAD extent=191,120 / 1,048,576 bytes, **857,456 bytes free**; dynamic tables and per-thread allocations are outside that window.

**Stopping point after eight startup batches:** Notepad++ exits 127 at `ADVAPI32.dll!RegOpenKeyExA ret=0x140116116`, root RCX=0xffffffff80000002 (HKLM), subkey RDX=0x1404a39f8 = `SOFTWARE\Microsoft\Cryptography`, desired access R9=0x20119. Imports: 144 resolved / 476 unimplemented. No CreateWindowExW or real-window backend has been implemented. Stop here for the next registry/API round. Runtime-created langs.xml, stylers.xml and shortcuts.xml in the portable directory are left untracked; do not commit them. The owner's deleted root hello.c remains deleted and unstaged.

### Works
- Module registers as binfmt handler; MZ/PE (PE32+) parsing with bounds checks; data directories logged (Import `0x55ead0`/`0x1b8` for Notepad++); PE headers mapped read-only at `0x140000000`; all sections mapped (all anonymous RW copies, `.text` RWX; permissions not yet refined).
- TEB `0x7ff00000`, PEB `0x7ff00800`, legacy 1 MB heap page `0x7ff30000` (unused by the bridge heap). GS base verified: bridge prints `gs:30=0x7ff00000 gs:60=0x7ff00800 gs:10=0x7ffff6800000`. Bridge fills NT_TIB (StackBase 0x7ffff7000000, StackLimit 0x7ffff6800000, Self, PEB) and PEB.ImageBaseAddress / PEB.ProcessHeap if empty. (Round 3 now sets ThreadLocalStoragePointer `+0x58` for main-image TLS; thread integration now fills TEB ClientId and per-thread slot storage.)
- Bridge loader: kernel maps the bridge's `PT_LOAD`s (ET_EXEC, absolute `e_entry`). Initial stack `user_sp = 0x7ffff6fff000`.
- Handoff registers into bridge `_start`: `RDI=PE entry, RSI=PEB, RDX=image base, RCX=import RVA, R8=import size`. `_start` calls the PE entry as `ms_abi`: `and $-16,%rsp; sub $32,%rsp; call *%r12` (**RSP%16==0 before the call; 32-byte shadow space**). The old `sub $40` was a real bug (misaligned stack made `movaps` in `ms_abi` prologues fault).
- Import resolver: walks import descriptors (by name and by ordinal); `lookup_shim()` returns a real shim or NULL; unknown imports get a generated stub that, **when called**, prints `[BRIDGE] CALLED UNIMPLEMENTED: dll!func ret=... rcx=... rdx=... r8=... r9=... w1="..." w2="..."` (UTF-16 previews of RCX/RDX) and exits 127.
- Milestone: **a real Windows `.exe` runs end to end** (`tests/hello.exe` prints `Hello from PE!`, exit 0; root `hello.exe` prints `Hello from a native PE on Linux!`, exit 42).

### Shims implemented in the bridge (all `__attribute__((ms_abi))`, registered in `lookup_shim()`, matched with `ascii_equal_ci`)
GetStdHandle, WriteFile, ExitProcess, GetSystemTimeAsFileTime, QueryPerformanceCounter/Frequency, GetCurrentProcessId, GetCurrentThreadId, SetLastError/GetLastError (`%gs:0x68`), LoadLibraryExW, GetModuleHandleW, GetProcAddress, InitializeCriticalSection / ...AndSpinCount / ...Ex, Enter/TryEnter/Leave/DeleteCriticalSection, TlsAlloc/Free/GetValue/SetValue and FlsAlloc/Free/GetValue/SetValue (per-thread TEB storage; global allocation/callback registries), VirtualProtect (mprotect; old protection reported as `PAGE_READWRITE`, an approximation), GetProcessHeap, HeapCreate, HeapDestroy, HeapAlloc, HeapFree, HeapSize, HeapReAlloc, HeapSetInformation. Helpers: `raw_syscall3/6`, `memset/memcpy/memmove/memcmp`, `raw_puts`, `raw_hex`.

### Design decisions already made (keep unless you have a reason)
- **Paths (2026-10-24):** Linux `/` is exposed as `Z:\`, separators become backslashes and case is preserved; no C: alias or configurable root yet. GetModuleFileNameW/A reads `/proc/self/exe`, GetCurrentDirectoryW/A reads getcwd. Future file APIs must reverse this mapping and reject unsupported drive letters. Environment remains intentionally empty.
- **File/std handles (2026-10-24):** `pe_fd_to_handle(fd)` encodes `0x40 + fd*4`; `pe_handle_to_fd(h)` validates alignment, descriptor range (<2^20), and liveness with F_GETFD. Standard handles are 0x40/0x44/0x48. Invalid file handles set error 6. No generation tracking yet; descriptor reuse can reuse a handle. This is separate from module, heap and pseudo handles.
- **Regression fixtures:** run `bash tests/run_all.sh`; all fixtures build from tracked sources, including `tests/hello42.c` as temporary `./hello.exe`. Owner-deleted root files remain deleted. Built test binaries are ignored.
- **Modules:** a small built-in registry maps names `kernel32(.dll)`, `kernelbase(.dll)`, `ntdll(.dll)` and any `api-ms-win-*` name to fake `HMODULE`s `0x6ff00000 + index*0x10000`. `LoadLibraryExW` of an unknown DLL → NULL + last error 126. `GetModuleHandleW(NULL)` → `0x140000000`.
- **GetProcAddress** returns NULL (+ last error 127) when there is no real shim. **Never return an "unimplemented" stub from it**: the static UCRT probes optional APIs and relies on NULL to select fallbacks.
- **Heap:** one fake handle `0x6ffe0000`; 16-byte block header `{usable, req}`, 16-byte-aligned payloads, power-of-two size classes from 1 MiB mmap arenas with per-class free lists, blocks >128 KiB get their own mmap. Protected by the recursive bridge state lock.
- **Critical sections / TLS / FLS:** real recursive TID-owned critical sections, 64 per-thread API TLS slots, 128 per-thread FLS slots and destructor callbacks (see latest thread integration above).
- Approximations to revisit: VirtualProtect old protection, static command line `notepad++.exe`, empty Win32 environment block, and dynamically loaded DLL TLS. Per-thread compiler/API TLS and FLS are implemented.

### Historical Round 3 state (2026-10-24)
- Round 2 plus informational harness committed separately as `7e73ea75c`. Notepad++ always emits `INFO Notepad++: exit N, last stop: ...`; crashes/unimplemented APIs do not fail the suite. Functional PE and synthetic diagnostic tests remain hard PASS/FAIL. Harness also checks every bridge PT_LOAD against the fixed 1 MiB window.
- **PE TLS completed for the initial thread** (`aba79480a`): parse PE32+ directory 9 (40-byte IMAGE_TLS_DIRECTORY64), range-check template/index/callback addresses, honor alignment bits 20–23, allocate zero-filled mmap storage, copy the template, append zero fill, publish a separate 64-pointer array at TEB+0x58 with module index 0, then call ms_abi callbacks with PROCESS_ATTACH before PE entry. Uses gs:0x30 for TEB. No kernel change/reload. TlsGetValue/FlsGetValue continue using separate API slot arrays; tests verify no conflict. TODO: per-thread blocks for CreateThread and TLS for subsequently loaded DLLs. No unwind machinery added.
- **Capacity** (`22552837b`): GUI table is now an mmap-backed vector of stable entry pointers, initially 1,024, doubling up to 65,536 slots. New entry chunks never move; resizing frees only the old vector, preserving pointers held during nested object creation. Handles reserve 16 slot bits and 10 generation bits within the existing 0x50000000–0x5fffffff range; generation 1023 retires a slot after deletion. Unknown-import metadata is mmap-backed and doubles from 128 to 65,536 entries. Stub code lives in separate stable executable mappings and uses an absolute jump through volatile R11 (mmap can be out of rel32 reach). The 64-entry class table also moved to mmap. GUI growth test holds live/selected objects across growth while creating 4,096 brushes.
- Final measured bridge: `size` reports text=60,438, data=0, bss=75,880 bytes (previous bss=475,232). PT_LOAD extent including alignment gaps: **145,512 / 1,048,576 bytes; 903,064 bytes free**. Growth of GUI/import tables consumes mmap space outside this fixed window. The harness fails if future static PT_LOAD growth exceeds it.
- Follow-up shim rounds: SRW exclusive/shared and try locks plus condition variables use atomic state and futex wait/wake (`65ef84a6f`); tested on the single initial thread, not yet with concurrent PE threads. Existing critical-section, heap, GUI, API TLS/FLS state still need a concurrency audit before threads. Windows 10 build 19045, x64, single-CPU system/version profile (`16ffd5071`); SHLWAPI path-string helpers for rooted/relative paths, filenames/extensions, slash manipulation (`f37257811`). No filesystem mapping change.
- Regression suite is green: hello 0, temporary ./hello.exe 42, heap 0, utf 0, gdi_objects 0, **tls 0**, **sync 0**, **paths 0**, and synthetic exception diagnostics (intentional 127). tests/tls.c handcrafts _tls_used/.tls, verifies initialized values 123/-7, zero fill, 64-byte alignment, one callback before entry (changes zero value to 42), and API TLS/FLS separation. tests/sync.c covers lock exclusion/shared counts, condition timeout and reacquisition, plus system profile; tests/paths.c covers pure path manipulation.
- **Current Notepad++ stop (informational):** 100 imports resolved / 520 unimplemented; exit 127 at `KERNEL32.dll!CreateThread ret=0x140067e56`, start routine R8=0x140067d00, argument R9=0x7ffff6ffe910 (stack address varies). TLS setup logs template size 8, zero-fill 0, alignment 16; the former 0x140017cd4 null-TLS crash is gone. Objdump at 0x140067e50 shows CreateThread followed by a thread wait. Stopped at the handoff's Batch D/thread design boundary, before CreateWindowExW. Do not fake a thread handle or run the worker synchronously. Proper clone/TEB/GS/TLS, wait/exit lifetimes and concurrency work is required next. No push.

### Historical Round 2 headless GUI object layer (2026-10-24)
- Implemented one 1,024-slot table shared by GDI and USER object kinds, handles in `[0x50000000, 0x60000000)`, aligned to 4. Handles encode slot plus generation; deleted handles do not validate after reuse. Generation exhaustion retires a slot. Type checks reject file/module/heap handles with last error 6. Tags cover brushes, pens, fonts, bitmaps, palettes, regions, DCs, icons, cursors, menus and windows; no menu/window creation APIs are implemented yet. Single-threaded.
- Implemented CreateSolidBrush, CreatePen, CreateFontIndirectW/CreateFontW, GetStockObject, DeleteObject, SelectObject, CreateCompatibleDC/DeleteDC, GetDeviceCaps, GetObjectW/GetObjectType/GetCurrentObject. Parameters are retained; selected objects cannot be deleted, shared/stock objects survive DeleteObject, DC teardown releases references and its private default bitmap.
- Data-only USER32: GetSystemMetrics, SystemParametersInfoW (explicit subset; other actions fail with error 50), GetSysColor, RegisterClassExW/GetClassInfoExW/UnregisterClassW, LoadCursorW/LoadIconW/LoadImageW, DestroyCursor/DestroyIcon, GetDC/ReleaseDC. Class records retain copied names/menu names, wndproc, style and extra-byte sizes; class extra storage is allocated. Image resources are metadata-only dummy objects; LR_LOADFROMFILE is unsupported. No resource pixels, font rendering, display connection, HWND manufacture, drawing, callbacks or message dispatch.
- Desktop profile: 1920x1080, 32 bpp, 96 DPI, work area 1920x1040, Segoe UI logical font. Device-specific capabilities and unsupported system metrics return zero. Dummy resource names are limited to 63 UTF-16 units, classes to 255, 64 registered classes. Shared resource handles are cached. Some Win32 behavior remains approximate (ASCII-only class-name folding, fixed metrics/colors/fonts).
- Added tests/gdi_objects.c and harness linking with kernel32/gdi32/user32. All five functional PE tests pass (hello 0, hello42 42, heap 0, utf 0, gdi_objects 0) and synthetic exception diagnostics pass. The GUI test covers creation/query, invalid/wrong-type/stale handles, selected-object lifetime, stocks, DC cleanup, metrics, classes, dummy images, table exhaustion/reuse and 1,100 repeated DC lifetimes.
- **New blocker / FAIL:** Notepad++ progresses past dark-mode brush/pen static initialization and crashes with exit 139 at `0x140017cd4`: `mov %gs:0x58,%rax` at 0x140017ccb followed by `mov (%rax),%rax` with RAX=0. This is the missing PE TLS directory setup from Batch C, before CreateWindowExW. Final import count: 85 resolved / 535 unimplemented. At the end of Round 2 the full harness exited 1. Round 3 step 0 makes Notepad++ informational: its crash does not fail the regression suite; test PEs retain hard PASS/FAIL. No TLS workaround, unwinding, drawing or kernel change was made. Stop for owner direction on proper TLS initialization; no reload required for these bridge changes.

### Previous Notepad++ results (before the headless GUI round)
2026-10-24: Batch A is present. Three Batch B rounds completed (9 + 8 + 8 shims): descriptor type/code pages/timing, real UTF-8/UTF-16 conversion, and module/current-directory paths/environment/debug output. `tests/run_all.sh` builds all test PEs and passes hello (0), temporary ./hello.exe (42), heap (0), and utf (0). The utf fixture also checks opaque handles, malformed conversion input, buffer sizing, and paths.

Latest diagnostic round (2026-10-24): implemented RtlPcToFileHeader using SizeOfImage from mapped PE headers. Added a bounded stack scan (512 qwords, up to 10 .text candidates) to unknown-stub diagnostics and an MSVC RaiseException metadata decoder; RaiseException remains unimplemented and exits 127. Reads use pread64 on /proc/self/mem to fail safely on unreadable addresses. Public layout reference: https://devblogs.microsoft.com/oldnewthing/20100730-00/?p=13273 and the 2016 correction https://devblogs.microsoft.com/oldnewthing/20160915-00/?p=94316 . No Wine/ReactOS code used.

Correction to previous diagnosis: RtlPcToFileHeader at ret 0x1403ddfcd was called by an exception-record preparation helper (0x1403ddf60), not proof of an active throw. Static initializer 0x140001ac0 calls it with ThrowInfo 0x14055de98, whose first catchable type is `.?AVbad_exception@std@@`; initializer 0x140001a70 prepares `.?AVbad_alloc@std@@`. The helper returns without RaiseException. With the image-header shim implemented, Notepad++ reaches its first GDI call without invoking RaiseException.

Current stop: exit 127, GDI32!CreateSolidBrush, return 0x1400180c3, color 0x202020. Stack-scan candidates: 0x1400180c3 0x14044bc80 0x140001297 0x140417d2a 0x1403f82b8 0x1403f83b9 0x1403f83b0. These are candidates, not an unwound call stack. Disassembly: CRT initializer iteration 0x140417d08 -> static initializer 0x140001280 -> color/brush constructor 0x140018090 -> IAT 0x14044d408 (CreateSolidBrush). Likely NppDarkMode::Theme::Theme / inlined Brushes construction, matching public upstream NppDarkMode.cpp (master, not exact binary symbols).

All four original regressions pass. New tests/exception_diag.c validates image membership boundaries and a synthetic MSVC exception record (type `.?AVdiagnostic_test@@`, object bytes 00 through 1f, intentional exit 127); this is decoder testing, not a Notepad++ throw or an unwind test. Stopped after this diagnostic round. No unwinding, GUI implementation, kernel changes, or reloads.

### Import profile of Notepad++ (620 imports)
USER32 216, KERNEL32 192, GDI32 66, ADVAPI32 26, COMCTL32 24, SHLWAPI 17, UxTheme 16, SHELL32 12, ole32 12, IMM32 9, CRYPT32 8, bcrypt 7, VERSION 3, SensApi 2, OLEAUT32 2, dwmapi 2, COMDLG32 2, WINTRUST 1, WININET 1, MSIMG32 1, dbghelp 1.

## 4. Working method (the loop)

1. With binfmt_pe already loaded, build only the bridge with the GCC command in section 2 and run `bash tests/run_all.sh`; read `/tmp/err.txt`. Notepad++ is informational. Use `bash run2.sh` with sudo only when a module rebuild/reload is actually needed.
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
- **TLS directory (initial-thread support completed in Round 3; child threads completed in thread integration)** (RVA `0x502000`, size `0x28`): allocate a TLS block, copy the template, store the index/array so `gs:[0x58]` (`ThreadLocalStoragePointer`) + `_tls_index` works; run TLS callbacks with `DLL_PROCESS_ATTACH`.
- **x64 SEH/C++ exceptions** from `.pdata` (RVA `0x57a000`, size `0x1faa0`): RtlCaptureContext, RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwindEx, RaiseException, plus `__C_specific_handler` flow as called by compiled code. Write a small test PE using `__try/__except` first (mingw) before Notepad++.
- Apply `.reloc` only if the image ever loads at a non-preferred base.

### Batch D — threads and sync (core implemented; see latest state)
Completed core: CreateThread via clone with per-thread TEB/GS/TLS, recursive futex critical sections, SRW locks, events, mutexes, semaphores, wait APIs, per-thread TLS/FLS/destructors, 32-bit Interlocked exports and ExitThread. Pending: APCs, forced cancellation, thread pools, full interprocess object namespaces and expansion/DLL TLS. Existing 20-run stress suite is mandatory after synchronization changes.

### Stage 2 — GUI (original implementation selected; first window completed)
Historical design options (owner has now chosen the original layer and the first visible-window milestone above supersedes this decision gate): USER32 (216 imports) + GDI32 (66) + COMCTL32/UxTheme/SHELL32/etc. need a display path: windows, message loop, painting, fonts, input. Options: (a) minimal in-house layer on X11 (Xlib/xcb via dynamic loading or raw protocol) first, Wayland later; (b) selectively reuse LGPL Wine code (requires explicit owner approval and licensing review); (c) first prove the path with a tiny Win32 test app (RegisterClassW/CreateWindowExW/message loop/`TextOutW`) before Notepad++. Bring a short written design and a recommendation to the owner before building this.

### Hygiene (do when convenient)
- Per-section memory protections from PE `Characteristics` (text RX, rdata R, data RW) done in the kernel; `/proc/self/maps` shows the PE's non-text sections labelled `[heap]` (the kernel set `mm->brk` there; cosmetic, but the bridge must never call `brk`).
- Consider moving initial main-thread GS/TEB setup fully into the kernel. The bridge now fills ClientId/TLS fields and initializes GS/TEBs for clone-created child threads itself.
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
