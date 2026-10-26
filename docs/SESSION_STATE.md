# Resume state

- Current commit: 4d7e94d23 (scroll state); ordinal correction: 771e27c32.
- Last green: `bash tests/run_all.sh > /tmp/scroll-batch.log 2>&1` outside sandbox,
  exit0. scroll_state, commctl_ordinals, ImageList and all other hard fixtures,
  20/20 thread stress and diagnostics PASS. Visible tests SKIP.
- First batch green: /tmp/ordinal-fix.log (exit0).
- Notepad++: exit127, 329 resolved /291 unimplemented;
  GDI32!CreateCompatibleBitmap ret=0x14000443d7, HDC0x50040468, 316x400.
  No visible main-window request, new crash/hang or PE exit126.
- CRITICAL CORRECTION: 0x17d is LoadIconWithScaleDown, not ImageList_LoadImageW.
  The fifth argument is output HICON*; return EAX is HRESULT (zero=S_OK).
  Existing DIB resource decoder + real nearest-neighbor scaling now supplies the
  HICON; unsupported PNG fails. Named ImageList_LoadImageW remains available.
  Old seven-argument ordinal test was replaced, not preserved as false evidence.
- Complete COMCTL ordinal audit: docs/COMCTL_ORDINALS.md. Seven imports:
  17 InitCommonControls (new binding, ICC_WIN95_CLASSES); 345 TaskDialogIndirect
  (ABI-inferred/unimplemented); 381 LoadIconWithScaleDown (decisive ABI);
  410/411/412/413 Set/Get/RemoveWindowSubclass/DefSubclassProc (ABI-inferred,
  existing bindings checked with actual ordinal imports and forwarding tests).
- Scroll state: Get/SetScrollInfo, Get/SetScrollPos, Get/SetScrollRange,
  ShowScrollBar, EnableScrollBar. Per-HWND mmap state, classic default 0..100,
  range/page/position clamping with 64-bit arithmetic, styles/arrow state, cleanup.
  Native bar rendering/input/nonclient geometry are NOT implemented. TrackPos
  follows programmatic position until actual thumb tracking is implemented.
- Tests include icon resources, scaled pixels/alpha, HRESULT/out-pointer failure,
  named/ordinal calls, subclass identity/data/forwarding, bar/window isolation,
  large integer ranges, short SCROLLINFO and invalid handles/parameters.
- Uncommitted work: this post-commit checkpoint refresh only.
- Next 3 steps: inspect CreateCompatibleBitmap/buffered drawing call path;
  implement/test real bitmap storage/drawing siblings as needed; full regression
  outside sandbox, commit green and refresh this file.

STOPPED after two batches; wait for owner to continue. No sudo/kernel changes,
reload or push. Stop on PE exit126, new crash/hang, visible main-window request,
or need for dialog templates/unwinding/kernel changes. Helper startup requires
outside-sandbox execution. Older ImageList PNG/RLE/files/overlays/cursor limits
remain; no fake success for unsupported decoding/drawing options.
