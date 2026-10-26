# Resume state

- Current commit: c95510968 (SaveDC/RestoreDC, mapping modes, clipping); bitmap commit: c72145af0.
- Last green: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside sandbox,
  exit 0. bitmaps, dc_saved, scroll_state, commctl_ordinals, ImageList and all
  other hard fixtures, 20/20 thread stress and diagnostics PASS. Visible tests SKIP.
- Notepad++: exit 127, 344 resolved / 276 unimplemented;
  GDI32!GetTextExtentExPointW ret=0x1402bcf33, rcx=0x50080468, rdx=0x7ffff6ff8d70, r8=0x63, r9=0x7fffffff.
  No visible main-window request, new crash/hang or PE exit 126.
- Bitmap and DC state implementation:
  1. Real software raster backing for CreateCompatibleBitmap, GetDIBits, SetDIBits,
     BitBlt, StretchBlt, PatBlt, SetStretchBltMode, GetPixel/SetPixel on memory DCs.
  2. Device context state stack: SaveDC, RestoreDC with object ref-counting,
     Set/Get/OffsetViewportOrgEx, Set/GetWindowOrgEx, Set/GetMapMode,
     GetClipBox, IntersectClipRect, ExcludeClipRect, SelectClipRgn, CreateRectRgn(Indirect), SetRectRgn.
- Tests: tests/bitmaps.c (pixel roundtrip, blits, DIBits) and tests/dc_saved.c
  (Save/Restore stack, origins, MapMode, ClipBox, regions) both pass.
- Uncommitted work: none.
- Next 3 steps: implement GetTextExtentExPointW and related font metric functions;
  run full regression and verify Notepad++ progress; stop at visible window request.

STOPPED after bitmap & SaveDC batches; wait for owner to continue. No sudo/kernel changes,
reload or push. Stop on PE exit 126, new crash/hang, visible main-window request,
or need for dialog templates/unwinding/kernel changes. Helper startup requires
outside-sandbox execution.

