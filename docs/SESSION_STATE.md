# Resume state

- **Source/code baseline:** `3be1bcaff` (WIP unverified behavior changes)
- **Latest full suite:** `bash tests/run_all.sh` exited 0, **91 PASS**, no FAIL, no 126 exit
- **Latest Notepad++:** `BRIDGE_TRACE_MODULES=1 ./npp.8.9.8.portable.x64/notepad++.exe` shows imports: resolved 445, unimplemented 175. Stop: `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Bridge budget:** PT_LOAD extent 236240 / 1048576 bytes (812336 bytes free)

## Current commit: e14911ab5
File: `R2-R6: Add MessageBoxW, fix trace format for builtin modules`

## Current state
- **Suite result:** 91 PASS, 0 FAIL (added user32_dll test)
- **Notepad++ stop:** `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Imports:** 445 resolved, 175 unimplemented
- **New builtin modules:** user32.dll (handle 0x6ff70000), gdi32.dll (handle 0x6ff80000)
- **MessageBoxW:** Implemented in bridge/msgbox.inc, registered in lookup_shim for user32.dll
- **Trace format fixed:** rva= only shown if caller inside PE image, otherwise omitted

## Next 3 steps (Phase 1 begins)
1. **Diagnose RaiseException stop** - Understand why C++ exception is thrown ("The specified module could not be found.") - likely a DLL that can't be loaded
2. **Implement RaiseException / x64 exception handling** - Required to continue Notepad++ startup past C++ exception handling
3. **Realize child windows** - Toolbar, status bar, tab control, Scintilla need realization

## Phase 1 bugs to fix (ordered)
1.1 Find why CreateDialogParamW returns NULL in plugin-enabled run (BRIDGE_TRACE_DIALOGS) - NOTE: Plain run (-noPlugin) now reaches RaiseException, not CreateDialogParam failure
1.2 Find why toolbar and status bar stay 0x0 (BRIDGE_TRACE_MOVES)
1.3 Toolbar and rebar: full message sets
1.4 Realization and paint of child windows, with dirty-rect presentation (fixes lag)
1.5 Input routing and tab clicks

## Known limitations
No native pointer warping, no RTL mirroring, topmost not propagated through owner chain, PNG/RLE icons unsupported, clipboard process-local, local time = UTC, controls largely headless, no x64 unwinding beyond function lookup, no real DLL loading.

---
## Historical checkpoints (for reference only)
- Round 0: WinVerifyTrust stub, trust diagnostics
- Round 1: First visible USER32/GDI window, message loops, software rasterization
- Round 2-8: Resources, menus, threads, dialogs, hooks, COM, caret, GDI objects, font metrics, etc.
- See HANDOFF_v3.md for full history