# Resume state

- Current implementation commit: b6c84987e (COM bookkeeping); caret b9b9e2182;
  INIT_ONCE 0a7520391; resources/menus 273a7704e.
- Last green suite: `bash tests/run_all.sh > /tmp/r6.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures including COM, 20/20 stress, diagnostics PASS;
  display SKIP. Bridge PT_LOAD 318232 / 1048576 bytes.
- Notepad++: exit 127; 282 resolved / 338 unimplemented imports;
  USER32!GetDlgCtrlID ret=0x14022c328. No new crash/hang/visible window.
- Uncommitted work: gui/dialog_items.inc, tests/dialog_items.c, lookup/harness
  edits and this resume refresh. Suite running in /tmp/r7.log.
- Next three steps:
  1. Implement/test child-control ID, lookup and dialog-item helpers.
  2. Run the full suite; commit green work and refresh this file.
  3. Follow RT_DIALOG creation when called; verify native menu display.

Bridge/GUI only; no sudo, kernel edits/reload or push. Stop immediately on any PE
exit 126; stop for owner visual check at first visible Notepad++ window, any new
crash/hang, or after ~6 rounds (4 completed this session). Native menus still
need display verification; caret state has no drawing; dialogs/unwinding absent.
