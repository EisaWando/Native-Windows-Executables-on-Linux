# Resume state

- Current implementation commit: 0a7520391 (INIT_ONCE); resources/menus: 273a7704e.
- Last green suite: `bash tests/run_all.sh > /tmp/r4.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures, INIT_ONCE, resources, 20/20 stress, diagnostics
  PASS; display SKIP. Bridge PT_LOAD 314104 / 1048576 bytes.
- Notepad++: exit 127; 269 resolved / 351 unimplemented imports;
  USER32!GetCaretBlinkTime ret=0x140226e1b. INIT_ONCE guard now succeeds.
- Uncommitted work: caret state shims/fixture, bridge/harness edits and this
  resume-file refresh. Full suite running in /tmp/r5.log.
- Next three steps:
  1. Implement/test caret state APIs required by the actual startup path.
  2. Run the full suite; commit green work and refresh this file.
  3. Follow real control/dialog stops; verify native menu display.

Bridge/GUI only; no sudo, kernel edits/reload or push. Stop immediately on any PE
exit 126; stop for owner visual check at first visible Notepad++ window, any new
crash/hang, or after ~6 rounds (2 completed this session). New menu presenter
needs display verification; controls remain incomplete, dialogs/unwinding absent.
