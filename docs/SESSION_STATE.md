# Resume state

- **Source/code baseline:** `c06ad3441` (Q3-Q8: Headless screen geometry, SetForegroundWindow/GetForegroundWindow, test fixes)
- **Latest full suite:** `bash tests/run_all.sh` exited 0, **93 PASS**, no FAIL, no 126 exit
- **Latest Notepad++:** `BRIDGE_TRACE_MODULES=1 ./npp.8.9.8.portable.x64/notepad++.exe` shows imports: resolved 445, unimplemented 175. Stop: `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."

## Phase 1.0 Complete: BRIDGE_TRACE_MODULES Investigation

### Findings
1. **Failing Module**: `shcore.dll` (user32.dll was already in builtin_module_names)
   - Trace showed: `[TRACE] LoadLibraryExW shcore.dll -> 0x0 caller=0x0000000140232494 rva=0x0000000000232494`

2. **Root Cause**: `shcore.dll` was NOT in `builtin_module_names[]` array in win32_bridge.c:422-433
   - The array had: kernel32, kernelbase, ntdll, user32, gdi32
   - shcore.dll was missing, causing LoadLibraryExW to return NULL

3. **Caller RVA where RaiseException is called**: `0x3fa527`
   - Address: `0x1403fa527` (in module)
   - Disassembly shows call through IAT entry at `0x14044d730`
   - Exception code `0xe06d7363` (C++ std::bad_alloc exception "The specified module could not be found.")

4. **Trace output format fix**: Fixed double "0x" prefix in trace output (was ` -> 0x HEX `, now ` -> HEX `)

### Fix Applied
- Added `shcore` and `shcore.dll` to `builtin_module_names[]` array (commit 13d24b7d2)
- user32.dll and gdi32.dll were already present

### Determinism: Verified
Three consecutive runs with `BRIDGE_TRACE_MODULES=1` showed identical NULL returns for shcore.dll before fix.

## Reverted: Plugin Loading Fakery (S2)

### Problem
Previous commits (14b77e404, 13d24b7d2) introduced fakery:
- Fake plugin API stubs for Z-path modules (isUnicode, setInfo, getName, getFuncsArray, beNotified, messageProc)
- Fake Z:\ DLL handles returned by LoadLibraryExW when file exists
- D2D1, DWrite, D3D11, shcore added to builtin_module_names

### Correction (this session)
- **Removed** all plugin API stubs for Z-path modules
- **Removed** fake Z:\ DLL handles from LoadLibraryExW (now returns NULL + GetLastError=126)
- **Removed** D2D1, DWrite, D3D11, shcore from builtin_module_names (LoadLibrary/GetModuleHandle return NULL again)
- **Kept** Z:\ to Linux path translation (pe_zpath_to_linux) for future use
- **Kept** trace improvements (pe_trace_caller_rva_w/a, GetProcAddress tracing)
- **Fixed** is_fake_module_handle() range: [0x6ff00000, 0x70000000) - does not overlap bridge image at 0x70000000
- **Added** test: fake_handle_test.exe verifies shim pointers never overlap fake module handle range

### Test Results
- **93 PASS**, 0 FAIL (new tests: builtin_dlls.exe, fake_handle_test.exe)
- LoadLibraryExW/GetModuleHandleW for shcore, d2d1, dwrite, d3d11 return NULL + GetLastError=126
- Fake module handle range: [0x6ff00000, 0x70000000) - does not overlap bridge image at 0x70000000
- Shim pointers (GetProcAddress results) never overlap fake module handle range

## Current commit: a9de0b857 (docs: Update README.md, NEXT_TASKS.md, GUI_DESIGN.md, UNIMPLEMENTED_IMPORTS.txt with latest import counts)

## Current state
- **Suite result:** 93 PASS, 0 FAIL
- **Notepad++ stop:** `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Imports:** 445 resolved, 175 unimplemented
- **Builtin modules:** kernel32, kernelbase, ntdll, user32, gdi32
- **Monitor API:** Implemented MonitorFromWindow, MonitorFromPoint, MonitorFromRect, GetMonitorInfoW
  - dwFlags honored (DEFAULTTONULL, DEFAULTTOPRIMARY, DEFAULTTONEAREST)
  - EnumDisplayMonitors unregistered (callback calling convention issue in freestanding bridge)
- **Screen geometry:** Single source in pe_ui (gui/bridge.inc helpers), headless fallback 1366x768/728
- **Window state API:** SetForegroundWindow (BOOL), GetForegroundWindow (HANDLE) implemented
- **Fake module handle range:** [0x6ff00000, 0x70000000) - 1MB, does not overlap bridge image at 0x70000000
- **Z:\ path translation:** Available in pe_zpath_to_linux() for future use

## Next 3 steps (Phase 1 begins)
1. **Diagnose RaiseException stop** - Notepad++ throws C++ std::bad_alloc "The specified module could not be found" at 0x3fa527; missing DLL loading is the root cause
2. **Implement real DLL loading** - LoadLibraryExW for Z:\ paths needs actual PE loading, not fake handles
3. **Implement x64 exception handling** - RtlDispatchException, RtlVirtualUnwind, RtlUnwindEx for C++ exception propagation

## Phase 1.0 Actions Completed
- [x] Added BRIDGE_TRACE_MODULES tracing infrastructure
- [x] Ran Notepad++ with tracing (multiple runs verified determinism)
- [x] Identified failing module (shcore.dll)
- [x] Added shcore.dll to builtin_module_names (later reverted per S2)
- [x] Fixed trace output format
- [x] Implemented Z:\ path translation (pe_zpath_to_linux) for future real DLL loading
- [x] Fixed fake module handle range to not overlap bridge image
- [x] Added test verifying shim pointers don't overlap fake handle range

## S1-S5 Summary (this session)
- **S1:** Raw git status/log/stat reported
- **S2:** Reverted fakery - removed plugin stubs, fake handles, extra builtin modules; kept Z:\ translation + trace improvements
- **S3:** is_fake_module_handle() range fixed to [0x6ff00000, 0x70000000); test fake_handle_test.exe verifies no overlap
- **S4:** SESSION_STATE.md updated with real PASS/FAIL, HEAD hash, plain-run stop verbatim
- **S5:** docs/EH_DESIGN.md created, additional docs updated (README.md, NEXT_TASKS.md, GUI_DESIGN.md)

## Known limitations
No native pointer warping, no RTL mirroring, topmost not propagated through owner chain, PNG/RLE icons unsupported, clipboard process-local, local time = UTC, controls largely headless, no x64 unwinding beyond function lookup, no real DLL loading for arbitrary Z:\ paths.

---
## Historical checkpoints (for reference only)
- Round 0: WinVerifyTrust stub, trust diagnostics
- Round 1: First visible USER32/GDI window, message loops, software rasterization
- Round 2-8: Resources, menus, threads, dialogs, hooks, COM, caret, GDI objects, font metrics, etc.
- See HANDOFF_v3.md for full history