# Resume state

- Current commit: 20592f2b9 (GetTextExtentExPointW/A); SaveDC commit: c95510968; bitmap commit: c72145af0.
- Last green: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside sandbox,
  exit 0. text_extents, bitmaps, dc_saved, scroll_state, commctl_ordinals, ImageList and all
  other hard fixtures, 20/20 thread stress and diagnostics PASS. Visible tests SKIP.
- Notepad++: exit 127, 346 resolved / 274 unimplemented;
  USER32!GetUpdateRgn ret=0x140228ac9, rcx=0x5008036c, rdx=0x500c0468, r8=0, r9=0.
  No visible main-window request, new crash/hang or PE exit 126.
- Text extent implementation:
  1. Real measurement for GetTextExtentExPointW and GetTextExtentExPointA via native helper FreeType backend.
  2. Character advance width caching (ASCII / 256-entry array) for fast repeated character queries by Scintilla.
  3. Precise cumulative extent tracking (alpDx) and maximum extent fitting (lpnFit) matching Win32 GDI semantics.
- Tests: tests/text_extents.c (GetTextExtentPoint32W parity, lpnFit bounds, empty string, NULL pointers, A variant) passes.
- Uncommitted work: none.
- Next 3 steps: implement GetUpdateRgn and related window invalidation/update region shims;
  run full regression and verify Notepad++ progress; stop at visible window request.

STOPPED after text extents batch; wait for owner to continue. No sudo/kernel changes,
reload or push. Stop on PE exit 126, new crash/hang, visible main-window request,
or need for dialog templates/unwinding/kernel changes. Helper startup requires
outside-sandbox execution.

