# NEXT_TASKS.md — numbered task list

Rules are in `AGENTS.md`. Do the FIRST unchecked task. When it is done and committed, tick its box `[x]`, update `docs/SESSION_STATE.md`, report, and stop (unless the task says "repeat").
Tags: **[any]** = any model can do it. **[strong]** = hard task for a stronger model (do not attempt with a weak model; report "needs strong model" and stop).

## Phase 0 — housekeeping
- [ ] **T0 [any]** Check for uncommitted work: `git status --short`. If `bridge/date_format.inc` and `tests/date_format.c` are uncommitted: build the bridge, run the full suite (`bash tests/run_all.sh > /tmp/run.log 2>&1; tail -n 15 /tmp/run.log; grep FAIL /tmp/run.log`). If there is no FAIL, commit with message "bridge: date and time formatting for the English/UTC profile". If something FAILS, fix only that failure or revert your change and report.
- [ ] **T1 [any]** Create `docs/CODE_MAP.md`: a table listing each source file (`win32_bridge.c`, `bridge/*.inc`, `gui/*.inc`, `gui/host*.c`, `tests/*.c`) with ONE line describing what it contains and its main function names. Use `rg -n '^[a-zA-Z_].*\(' FILE | head -n 40` to find function names; do not read whole files. Commit "docs: add code map".

## Phase 1 — Notepad++ loop (repeat)
- [ ] **T2 [any, repeat up to 5 times per session]** One round:
  1. Build the bridge. Run Notepad++ (command in `AGENTS.md`). Read the first `CALLED UNIMPLEMENTED` line and the `imports: resolved N, unimplemented M` line.
  2. Implement that function plus 2-6 closely related ones (same DLL, same feature) following the shim checklist. If the function belongs to a big area that is not on this list (dialogs, exceptions, drawing a visible window), do not start it: write it in `docs/SESSION_STATE.md` and stop.
  3. Add/extend a test, run the full suite. If green, commit with a message listing the function names.
  4. Run Notepad++ again and note the new stop and counts.
  After 5 rounds, report.

## Phase 2 — larger features [strong]
- [ ] **T3a [strong]** Dialog templates: parse `RT_DIALOG` resources (`DLGTEMPLATE` and `DLGTEMPLATEEX`: styles, items, class names as atoms or strings, text, extra data). Unit test with a `windres`-built dialog PE.
- [ ] **T3b [strong]** `CreateDialogParamW`, `CreateDialogIndirectParamW`: create the dialog window and its child controls, send `WM_INITDIALOG`; `DefDlgProcW`, `GetDlgItem`, `SetDlgItemTextW`, `EndDialog`.
- [ ] **T3c [strong]** `DialogBoxParamW` modal loop (message loop, disabling the owner, `EndDialog` result), `IsDialogMessageW` (Tab/Enter/Esc navigation).
- [ ] **T4a [strong]** x64 unwind data: `RtlLookupFunctionEntry` (binary search of `.pdata` RUNTIME_FUNCTION table) and `RtlVirtualUnwind` (UNWIND_INFO codes: PUSH_NONVOL, ALLOC_LARGE, ALLOC_SMALL, SET_FPREG, SAVE_NONVOL(_FAR), SAVE_XMM128(_FAR), PUSH_MACHFRAME, chained info). Use only public Microsoft documentation and the mingw-w64 headers (CONTEXT layout). Test: walk a known stack from a function with a hand-written prologue. Do NOT copy Wine or ReactOS code.
- [ ] **T4b [strong]** `RtlCaptureContext`, `RtlRestoreContext`, `RaiseException` dispatch: call handlers from UNWIND_INFO (`EXCEPTION_ROUTINE`), `RtlUnwindEx`, two-phase unwinding. Test with an assembly file using `.seh_proc` / `.seh_handler`.
- [ ] **T4c [strong]** Map hardware faults (SIGSEGV, divide by zero, illegal instruction) to Windows exceptions (access violation 0xC0000005 etc.) delivered through the dispatcher.

## Phase 3 — first window (owner involvement)
- [ ] **T5 [any]** When Notepad++ requests its visible main window (the bridge prints a diagnostic at the visible show path), STOP. Write in `docs/SESSION_STATE.md` the exact command for the owner to run it on the desktop and take a screenshot. The owner will report what is on screen.
- [ ] **T6 [strong]** After the owner reports: paint pieces in this order: client background, status bar, tab bar, toolbar, then Scintilla text (fonts, `GetTextMetricsW`, `ExtTextOutW`, clipping, caret blinking). One piece per task, each with a display test (`PE_GUI_TEST=1`).

## Phase 4 — documentation
- [ ] **T7 [any]** Update `README.md` status checklist and import counts (`resolved N of 620`) from `docs/SESSION_STATE.md`. Keep the existing honest tone; do not claim features that are not working. Commit "docs: refresh README status".
