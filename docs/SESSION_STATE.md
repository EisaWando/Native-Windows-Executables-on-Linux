# Resume state

- Current implementation commit: 15adb07d5 (product profile/legacy formatting).
- Last green run: `bash tests/run_all.sh > /tmp/profile-format-r4.log 2>&1`
  outside sandbox, exit0. All hard fixtures, 20/20 stress, diagnostics PASS;
  display SKIP. Last native GUI checks: /tmp/fonts-r3-native.log PASS.
  Bridge PT_LOAD338712/1048576.
- Notepad++: exit127, 304 resolved /316 unimplemented;
  KERNEL32!GetDateFormatW ret=0x14004cb45. No visible request/crash/hang.
- Uncommitted work: none
  edits and this refresh. Suite /tmp/date-r5.log running outside sandbox.
- Next three steps: English/UTC date/time formatting; full suite/green commit;
  actual control/dialog calls toward main-window show.

Four of six rounds completed. Run helper-dependent suites outside sandbox;
no sudo/kernel edits/reload/push. Stop on PE exit126, new crash/hang or visible
Notepad++ request. At show request give desktop/screenshot commands and wait.
Dialogs/unwinding and native menu presentation verification remain pending.
