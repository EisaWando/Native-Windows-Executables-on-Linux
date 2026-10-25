# Resume state

- Current implementation commit: 0893f916e (real FreeType metrics, lazy X11).
- Last green run: `PE_GUI_TEST=1 bash tests/run_all.sh > /tmp/fonts-r3-native.log 2>&1`,
  exit0. All hard fixtures, real X11 display checks, 20/20 stress, diagnostics PASS.
  Font fixture also passes with DISPLAY empty. Bridge PT_LOAD 334616/1048576.
- Notepad++: exit127, 302 resolved /318 unimplemented imports;
  KERNEL32!GetProductInfo ret=0x1401c3cec. No visible request/crash/hang.
- Uncommitted work: product/profile and wsprintf/wvsprintf A/W shims, format
  fixture, harness/lookup edits and this checkpoint. /tmp/profile-format-r4.log running.
- Next three steps: version-profile API batch; full suite outside sandbox and
  green commit; continue actual control/dialog stops toward main-window show.

Three of six rounds completed this session. Native helper startup is blocked in
sandbox: font PE exits1 (last error126/31), passes outside. Run helper-dependent
regressions outside sandbox; no sudo or module reload. Stop on PE exit126,
new crash/hang, or visible Notepad++ show request; give desktop/screenshot
commands at show request and wait. RT_DIALOG/unwinding remain pending.
