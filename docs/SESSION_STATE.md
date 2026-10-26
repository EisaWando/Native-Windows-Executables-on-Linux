# Resume state

- Ordinal correction batch complete; current implementation is accompanying commit.
- Last green: `bash tests/run_all.sh > /tmp/ordinal-fix.log 2>&1` outside sandbox,
  exit0. commctl_ordinals, ImageLists, all hard fixtures, 20/20 stress, diagnostics
  PASS; visible tests SKIP (pixel scaling/alpha tested through memory DC).
- Notepad++: exit127, 322 resolved /298 unimplemented, USER32!GetScrollInfo,
  ret=0x14022dd72, HWND0x5008036c, SB_VERT=1. No new crash/hang/show request/PE126.
- IMPORTANT: previous owner-approved 0x17d -> ImageList_LoadImageW inference was
  wrong. Disassembly proves fifth argument HICON*, S_OK=0, output icon passed to
  ImageList_ReplaceIcon. Correct binding: LoadIconWithScaleDown; named ImageList
  loader retained. Tests import 381 with actual HRESULT/HICON* ABI, real icon
  resources, alpha and size changes, failures and NULL output on failure.
- COMCTL ordinals: 17 InitCommonControls (now bound to existing ICC_WIN95 wrapper);
  345 TaskDialogIndirect (inferred, unimplemented); 381 LoadIconWithScaleDown;
  410/411/412/413 Set/Get/RemoveWindowSubclass/DefSubclassProc (existing bindings
  verified by actual ordinal tests). Full import table/disassembly evidence:
  docs/COMCTL_ORDINALS.md. Other than 381's decisive ABI, names are ABI-inferred.
- Limits: icon decoder supports DIB resources, not PNG; nearest-neighbor scaling
  from existing resource-loader selection. No fabricated success for unsupported
  images. Existing image-list PNG/RLE/files/overlays/cursor limits remain.
- Next 3 steps: implement/test scrollbar state family; full suite/green commit;
  report and stop after second batch. No sudo/kernel edits/reload/push.
- Uncommitted work: none before post-commit checkpoint refresh.
