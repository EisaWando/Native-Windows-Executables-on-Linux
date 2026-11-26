# Resume state

- **Source/code baseline:** `f037f3666` (Q8: SESSION_STATE.md updated)
- **Latest full suite:** `bash tests/run_all.sh` exited 0, **92 PASS**, no FAIL, no 126 exit
- **Latest Notepad++:** `BRIDGE_TRACE_MODULES=1 ./npp.8.9.8.portable.x64/notepad++.exe` shows imports: resolved 450, unimplemented 170. Stop: `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."

## Phase 1.0 Complete: BRIDGE_TRACE_MODULES Investigation

### Findings
1. **Failing Module**: `user32.dll` (also `shcore.dll`)
   - Trace shows: `[TRACE] GetModuleHandleW user32.dll -> 0x0 caller=0x000000014023235c rva=0x000000000023235c`
   - Trace shows: `[TRACE] LoadLibraryExW shcore.dll -> 0x0 caller=0x0000000140232494 rva=0x0000000000232494`

2. **Root Cause**: `user32.dll` and `shcore.dll` are NOT in `builtin_module_names[]` array in win32_bridge.c:416-423
   - The array only contains: kernel32, kernelbase, ntdll
   - When GetModuleHandleW returns NULL for user32.dll, Notepad++ raises a C++ exception

3. **Caller RVA where RaiseException is called**: `0x3fa527`
   - Address: `0x1403fa527` (in module)
   - Disassembly shows call through IAT entry at `0x14044d730`
   - Exception code `0xe06d7363` (C++ std::bad_alloc exception "The specified module could not be found.")

4. **Trace output format fix**: Fixed double "0x" prefix in trace output (was ` -> 0x HEX `, now ` -> HEX `)

### Raw Trace Output (NULL returns):
```
[TRACE] GetModuleHandleW user32.dll -> 0x0 caller=0x000000014023235c rva=0x000000000023235c
[TRACE] LoadLibraryExW shcore.dll -> 0x0 caller=0x0000000140232494 rva=0x0000000000232494
```

### Determinism: Verified
Three consecutive runs with `BRIDGE_TRACE_MODULES=1` showed identical NULL returns for user32.dll and shcore.dll.

## Current commit: f037f3666
File: `Q8: SESSION_STATE.md updated`

## Current state
- **Suite result:** 92 PASS, 0 FAIL
- **Notepad++ stop:** `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Imports:** 450 resolved, 170 unimplemented
- **New builtin modules:** user32.dll (handle 0x6ff70000), gdi32.dll (handle 0x6ff80000)
- **Monitor API:** Implemented MonitorFromWindow, MonitorFromPoint, MonitorFromRect, GetMonitorInfoW
  - dwFlags honored (DEFAULTTONULL, DEFAULTTOPRIMARY, DEFAULTTONEAREST)
  - EnumDisplayMonitors unregistered (callback calling convention issue in freestanding bridge)
- **Screen geometry:** Single source in pe_ui (gui/bridge.inc helpers), headless fallback 1366x768/728
- **Window state API:** SetForegroundWindow (BOOL), GetForegroundWindow (HANDLE) implemented

## Next 3 steps (Phase 1 begins)
1. **Diagnose RaiseException stop** - Understand why C++ exception is thrown ("The specified module could not be found.") - likely a DLL that can't be loaded
2. **Implement RaiseException / x64 exception handling** - Required to continue Notepad++ startup past C++ exception handling
3. **Realize child windows** - Toolbar, status bar, tab control, Scintilla need realization

## Phase 1.0 Actions Completed
- [x] Added BRIDGE_TRACE_MODULES tracing infrastructure
- [x] Ran Notepad++ with tracing (multiple runs verified determinism)
- [x] Identified failing module (user32.dll, shcore.dll)
- [x] Identified caller RVA (0x3fa527)
- [x] Fixed trace output format

## Q2-Q8 Summary (this session)
- **Q1:** All 92 tests pass (4 pre-existing failures fixed by updating gdi_objects.c)
- **Q2:** Baseline baaa4a89e: 92 PASS / 0 FAIL confirmed
- **Q3:** gui/host.c now headless-friendly; reports no-display flag; bridge uses single fallback 1366x768
- **Q4:** SetForegroundWindow returns BOOL (TRUE/FALSE + GetLastError); GetForegroundWindow returns HWND; test updated
- **Q5:** No 1920/1080 in bridge/monitor.inc; single fallback in gui/bridge.inc
- **Q6:** DEFAULTTO tests verified in monitor_test.c
- **Q7:** UNIMPLEMENTED_IMPORTS.txt regenerated (450/170); EnumDisplayMonitors listed; SetForegroundWindow/GetForegroundWindow not listed
- **Q8:** This file updated

## Known limitations
No native pointer warping, no RTL mirroring, topmost not propagated through owner chain, PNG/RLE icons unsupported, clipboard process-local, local time = UTC, controls largely headless, no x64 unwinding beyond function lookup, no real DLL loading.

---
## Historical checkpoints (for reference only)
- Round 0: WinVerifyTrust stub, trust diagnostics
- Round 1: First visible USER32/GDI window, message loops, software rasterization
- Round 2-8: Resources, menus, threads, dialogs, hooks, COM, caret, GDI objects, font metrics, etc.
- See HANDOFF_v3.md for full history