# Resume state

- Current implementation commit: 273a7704e (Round 3 PE resources and menus).
- Last green suite: `bash tests/run_all.sh > /tmp/r3.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures, resources, 20/20 thread stress, diagnostics PASS;
  native display tests SKIP. Bridge PT_LOAD 314104 / 1048576 bytes.
- Notepad++: exit 127, 267 resolved / 353 unimplemented imports;
  KERNEL32!InitOnceBeginInitialize, ret=0x140231a62. GetMenu now succeeds.
- Uncommitted work: INIT_ONCE shims in bridge/init_once.inc, tests/init_once.c,
  lookup/harness edits and this file. Suite running in /tmp/r4.log.
- Next three steps:
  1. Implement/test real INIT_ONCE begin/complete/execute state transitions.
  2. Run the suite; stop immediately on any PE exit 126 or new target crash/hang.
  3. Commit green work, refresh this file, follow real control/dialog stops.

Bridge/GUI only; no sudo, kernel edits, module reload or push. Stop for owner
visual check at the first visible Notepad++ window, new crash/hang, or ~6 rounds.
New menu presenter still needs display verification. Existing controls are
headless and incomplete; no dialog parser or x64 unwinding yet.
