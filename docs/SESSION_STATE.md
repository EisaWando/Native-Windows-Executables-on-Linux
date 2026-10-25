# Resume state

- Current implementation commit: 2fa5adb05 (dialog-item helpers); COM b6c84987e;
  caret b9b9e2182; INIT_ONCE 0a7520391; resources/menus 273a7704e.
- Last green suite: `bash tests/run_all.sh > /tmp/r7.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures, 20/20 stress, diagnostics PASS; display SKIP.
  Bridge PT_LOAD 322328 / 1048576 bytes.
- Notepad++: exit 127; 292 resolved / 328 unimplemented imports;
  ole32!RegisterDragDrop ret=0x14022c356. No new crash/hang/visible window.
- Uncommitted work: drop-target registration/lifetime shims and fixture,
  bridge/harness edits and this refresh. Suite /tmp/r8.log exited 0; next stop SetWindowTheme (294/326).
- Next three steps:
  1. Implement/test drag-target registration lifetime (no transport yet).
  2. Run the full suite; commit green work and refresh this file.
  3. Report after sixth round; leave next actual stop for owner's continuation.

Bridge/GUI only; no sudo, kernel edits/reload or push. Stop immediately on any PE
exit 126; stop for owner visual check at first visible Notepad++ window, any new
crash/hang, or after ~6 rounds (5 completed this session). Native menus still
need display verification; caret state has no drawing; dialogs/unwinding absent.
