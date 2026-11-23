# Resume state

- **Source/code baseline:** `3428ffdb4` (docs refresh)
- **Latest full suite:** `bash tests/run_all.sh` exited 0, **90 PASS**, no FAIL, no 126 exit
- **Latest Notepad++:** `BRIDGE_DUMP_WINDOWS=1 ./npp.8.9.8.portable.x64/notepad++.exe -noPlugin` showed window tree with main window visible=1 realized=1, but child controls (toolbar, status bar) had realized=0. Suite's informational run ends at `RaiseException` (unimplemented) with 443 resolved / 177 unimplemented imports. Stop: C++ exception "The specified module could not be found."
- **Bridge budget:** PT_LOAD extent 236240 / 1048576 bytes (812336 bytes free)

## Current commit: 3428ffdb4
File: `docs: refresh project markdown status`

## Current state
- **Suite result:** 90 PASS, 0 FAIL
- **Notepad++ stop:** `RaiseException` code=0xe06d7363 (C++ std::bad_alloc exception) "The specified module could not be found."
- **Imports:** 443 resolved, 177 unimplemented
- **Window tree:** Main Notepad++ window created (visible=1, realized=1). ToolbarWindow32 at 0x580405e4 visible=1 realized=0. Status bar (msctls_statusbar32) visible=1 realized=0. Many child controls with realized=0.
- **Known issues:** Toolbar and status bar stay 0x0 in first-paint (realized=0 despite visible=1). Scintilla and tab controls not realized. Upper tab is laggy and ignores clicks. Three blank glyph boxes after "?".

## Next 3 steps (Phase 0 complete, Phase 1 begins)
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