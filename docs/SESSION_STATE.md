# Resume state

- Current repository commit: 3f2533947 (resolved documentation merge).
- Current implementation commit: 66b64edb0 (drop-target registration).
  Earlier green commits: 2fa5adb05 dialog items, b6c84987e COM bookkeeping,
  b9b9e2182 caret state, 0a7520391 INIT_ONCE, 273a7704e resources/menus.
- Last green suite: `bash tests/run_all.sh > /tmp/r8.log 2>&1`, exit 0,
  2026-10-25. All hard fixtures, 20/20 stress, diagnostics PASS; display SKIP.
  Bridge PT_LOAD 326424 / 1048576 bytes (722152 free).
- Notepad++: exit 127; 294 resolved / 326 unimplemented imports;
  UxTheme!SetWindowTheme ret=0x1401c368e, HWND=0x5008036c.
  No new crash/hang/visible window and no PE exit 126 this session.
- Uncommitted work: theme associations/shims, tests/theme.c, harness/bridge edits
  and this checkpoint refresh. Suite running: /tmp/theme-r1.log.
- Next three steps:
  1. Implement/test theme data state starting with SetWindowTheme.
  2. Follow actual control/dialog calls; RT_DIALOG templates and
     CreateDialogParamW/DialogBoxParamW are not implemented or reached yet.
  3. Verify native menu strip/popups on a display, run the full suite and commit
     green work; refresh this file after committing.

Resumed by owner: theme/control/dialog loop, up to six new rounds.
Bridge/GUI only; no sudo, kernel edits/reload or push. Stop immediately on any PE
exit 126; stop for visual check at the first visible Notepad++ window or any new
crash/hang. Native menu rendering still needs display verification. Caret state
has no pixel/blink rendering; drop registration has no drag transport/delivery;
COM has no servers/marshaling. Dialog construction and unwinding remain absent.
