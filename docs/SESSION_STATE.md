# Resume state

- Current implementation commit: b9b9e2182 (caret state); INIT_ONCE 0a7520391;
  resources/menus 273a7704e.
- Last green suite: `bash tests/run_all.sh > /tmp/r5.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures including caret, 20/20 stress, diagnostics PASS;
  display SKIP. Bridge PT_LOAD 318232 / 1048576 bytes.
- Notepad++: exit 127; 275 resolved / 345 unimplemented imports;
  ole32!OleInitialize ret=0x140226e2d. No new target crash/hang/visible window.
- Uncommitted work: COM apartment/task-memory shims and regression;
  gui/bridge.inc, win32_bridge.c, tests/run_all.sh and this file. Suite /tmp/r6.log.
- Next three steps:
  1. Implement/test per-thread COM apartment and allocation state.
  2. Run the full suite; commit green work and refresh this file.
  3. Follow actual control/dialog stops; verify native menu display.

Bridge/GUI only; no sudo, kernel edits/reload or push. Stop immediately on any PE
exit 126; stop for owner visual check at first visible Notepad++ window, any new
crash/hang, or after ~6 rounds (3 completed this session). Native menus still
need display verification; caret state has no drawing; dialogs/unwinding absent.
