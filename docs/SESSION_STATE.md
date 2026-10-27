# Resume state

- Current commit: 1bec2df5c — exact update regions and paint-cycle validation.
- Last green: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside sandbox,
  exit 0, 64 PASS lines. update_rgn and every existing hard fixture PASS;
  20/20 thread stress and intentional crash/exception diagnostics PASS. Display tests SKIP.
- Notepad++: suite and post-commit standalone run exit 127; USER32!IsClipboardFormatAvailable,
  ret=0x14022ef68, rcx=13 (CF_UNICODETEXT). 349 resolved / 271 unimplemented.
  No PE exit 126, new crash/hang, or visible main-window request observed.
- Implemented exact mmap-backed disjoint rectangle geometry in existing HRGN objects;
  client-clipped update union/subtraction, GetUpdateRgn/GetUpdateRect, InvalidateRgn,
  ValidateRgn/Rect, RedrawWindow requested flags and child coordinate translation,
  ExcludeUpdateRgn, and empty-update suppression in UpdateWindow.
- BeginPaint transfers the update region to its DC, reports its bounding rcPaint,
  consumes pending erasure and sets fErase from WM_ERASEBKGND's result. EndPaint
  preserves invalidations made during painting. Callbacks run outside the state lock.
- DC clipping retains disjoint geometry through SaveDC/RestoreDC; software blits
  honor it. Native helper PG_CLIP applies rectangle sets with menu offset to drawing.
  Native clipping was built but not visually exercised (display tests SKIP).
- Scope limits: RDW_INTERNALPAINT/NOINTERNALPAINT/FRAME/NOFRAME remain unsupported
  (error 50); desktop NULL-HWND RedrawWindow is not implemented. Existing non-MM_TEXT
  mapping and complete child/sibling visibility clipping remain future work.
- Bridge budget: 408952 / 1048576 bytes (suite check); region arrays allocate with mmap.
- Uncommitted work: none (this refresh is committed together with the batch).
- Next 3 steps: inspect clipboard call requirements; implement clipboard state with
  explicit ownership/format semantics and a fixture; run suite and resume Notepad++
  only when the owner authorizes the next batch.

STOPPED after the single requested paint-cycle batch. No sudo/kernel changes or push.
Stop on PE exit 126, new crash/hang, first visible main-window request, or need for
kernel/dialog-template/unwinding work. Native helper tests require outside-sandbox execution.
