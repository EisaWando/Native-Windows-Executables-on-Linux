# Resume state

- Font commit: e09a0dce8. ImageList batch is the accompanying commit (git log -1).
- Last green: `PE_GUI_TEST=1 bash tests/run_all.sh > /tmp/imagelist-final.log 2>&1`
  outside sandbox, exit0. ImageList/enum_fonts, all hard fixtures, 20/20 thread
  stress, diagnostics and real X11 pixel/text/resize/input checks PASS.
- Earlier batch green: /tmp/enum-fonts.log; preliminary ImageList: /tmp/imagelist-suite.log.
- Notepad++: exit127, 321 resolved /299 unimplemented; COMCTL32 ordinal0x11,
  ret=0x1400c4ebe; rcx=0x7ffff7bab070, rdx=0x140000000, r8=0x10, r9=0.
  No visible main-window request, new crash/hang or PE exit126.
- Bridge: text289370, data4, bss68344; PT_LOAD367384/1048576 (681192 free).
- ImageList: growable typed handles; copied 32-bit pixels; strip/mask addition,
  remove/size/count/background APIs, independent icon copies, pixel drawing and
  blending to memory DCs or native window DCs. Tests cover each requested API,
  DIB resources and an actual ordinal-import fixture. CreateBitmap and 32-bit
  BI_RGB CreateDIBSection provide owned bitmap storage for memory-DC drawing.
- 0x17d -> ImageList_LoadImageW is an owner-approved inference from call ABI.
  Public MinGW comctl32.def lists A/W names and signatures but no ordinal numbers.
- Limits: PNG, RLE, file-loading, overlays and resource cursor loading are not
  implemented; unsupported formats/options fail, never return placeholder success.
  DIB bitmap/icon decoding supports 40+ byte headers and common RGB/bitfields.
  Font enumeration uses real catalog/metrics/OS2 charset data; bitmap-font charset
  fallback only recognizes real Latin glyph coverage. No fake font catalog.
- Uncommitted work: none before post-commit checkpoint refresh.
- Next 3 steps: identify COMCTL32 ordinal0x11; implement/test its related APIs;
  run full suite outside sandbox, commit green and refresh this file.

STOPPED after the two requested batches. Wait for owner to continue.
No sudo/kernel edits/reload/push. Sandbox helper startup is restricted: baseline
font PE exited1, not126; outside-sandbox suites pass. Stop on any PE exit126,
new crash/hang, visible Notepad++ request, or need for dialogs/unwinding/kernel.
