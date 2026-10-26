# Resume state

- Font-enumeration batch complete; commit recorded by git log.
- Last green: `bash tests/run_all.sh > /tmp/enum-fonts.log 2>&1` outside sandbox,
  exit0. enum_fonts and all hard tests, 20/20 stress, diagnostics PASS.
- Notepad++: exit127, 309 resolved /311 unimplemented; COMCTL32!ImageList_Create,
  ret=0x1401ebbb5. No visible-window request/crash/hang/PE exit126.
- Enumeration uses real Fontconfig catalog, FreeType metrics and OS/2 codepage
  signatures; reentrant snapshot tokens; callback ABI 348/100 bytes and unlocked.
  Bitmap fonts without OS/2 coverage use actual Latin glyph availability.
- Pending work: ImageList pixel storage/resource decoding/API batch; ordinal
  0x17d mapping awaiting owner answer (current MinGW def has names, no ordinals).
- Next 3 steps: implement ImageList family; full regression/green commit;
  report and stop after the second batch. No kernel/sudo/reload/push.
