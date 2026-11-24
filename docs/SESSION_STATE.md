# Resume state

- **Source/code baseline:** `209e9d20e` (N5-N6: Add monitor API shims and headless test)
- **Latest full suite:** `bash tests/run_all.sh` exited 0, **92 PASS**, no FAIL, no 126 exit
- **Latest Notepad++:** `BRIDGE_TRACE_MODULES=1 ./npp.8.9.8.portable.x64/notepad++.exe` shows imports: resolved 445, unimplemented 175. Stop: `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."

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

## Current commit: 209e9d20e
File: `N5-N6: Add monitor API shims and headless test`

## Current state
- **Suite result:** 92 PASS, 0 FAIL
- **Notepad++ stop:** `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Imports:** 445 resolved, 175 unimplemented
- **New builtin modules:** user32.dll (handle 0x6ff70000), gdi32.dll (handle 0x6ff80000)
- **Monitor API:** Implemented MonitorFromWindow, MonitorFromPoint, MonitorFromRect, GetMonitorInfoW, EnumDisplayMonitors
- **Trace format fixed:** rva= only shown if caller inside PE image, otherwise omitted

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

## Known limitations
No native pointer warping, no RTL mirroring, topmost not propagated through owner chain, PNG/RLE icons unsupported, clipboard process-local, local time = UTC, controls largely headless, no x64 unwinding beyond function lookup, no real DLL loading.

---
## Historical checkpoints (for reference only)
- Round 0: WinVerifyTrust stub, trust diagnostics
- Round 1: First visible USER32/GDI window, message loops, software rasterization
- Round 2-8: Resources, menus, threads, dialogs, hooks, COM, caret, GDI objects, font metrics, etc.
- See HANDOFF_v3.md for full history