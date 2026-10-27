# Resume state

- Current commit: 1c8f75787 — ExtTextOutW/A with ETO_OPAQUE, ETO_CLIPPED and lpDx.
- Last green: `bash tests/run_all.sh > /tmp/run.log 2>&1` exit 0, **66 PASS**, no FAIL.
  Display test `timeout 30s tests/windows.exe` (DISPLAY=:0, real X11 + FreeType
  helper) prints `windows ok`, exit 0, including the new ExtTextOutW pixel checks.
- Notepad++: exit 127, stops at **USER32!CreateDialogIndirectParamW**
  (rcx=HINSTANCE 0x14000000, rdx=DLGTEMPLATE, r8=owner HWND, r9=0x1401e9e80).
  **378 resolved / 242 unimplemented.**
- Batches done in this session (one small batch each, suite green after each):
  1. `60b5ec006` clipboard family + HGLOBAL global memory (`bridge/clipboard.inc`,
     `tests/clipboard.c`): OpenClipboard nesting, EmptyClipboard owner
     notification (WM_DESTROYCLIPBOARD), delayed rendering (WM_RENDERFORMAT),
     Enum/Count/Priority, sequence number, GMEM_FIXED/MOVEABLE lock counting.
  2. `cb439baf5` locale identity for the fixed en-US profile (`bridge/locale.inc`).
  3. `25313d2a2` GetLocaleInfoW/A/Ex (`bridge/locale_info.inc`) with the en-US
     value table, LOCALE_RETURN_NUMBER, size queries and error codes.
  4. `8d1f0e0ce` GetStringTypeW/A/Ex and LCMapStringW/A/Ex plus CharUpper/CharLower
     (`bridge/char_type.inc`, `bridge/case_map.inc`) on generated Unicode tables
     (`bridge/char_type_ranges.inc`, generator `bridge/char_type_gen.py`).
  5. `1c8f75787` ExtTextOutW/A (`gui/ext_text_out.inc`) with ETO_OPAQUE, ETO_CLIPPED,
     per-character lpDx, and ETO_GLYPH_INDEX refused.
- Bridge budget: **523704 / 1048576 bytes** (suite check). The generated Unicode
  tables are ~74 KiB of the binary.
- Honest limits of the new code:
  - The clipboard is process-local: no cross-process or X11 selection bridge.
  - Only the en-US locale exists; other locales fail with ERROR_INVALID_PARAMETER.
  - LCMapString implements case mapping only: LCMAP_SORTKEY, LCMAP_BYTEREV, the
    width/kana transformations and the Chinese transformations fail with
    ERROR_INVALID_FLAGS (1004). No one-to-many case mappings (U+00DF stays U+00DF).
  - GetStringTypeA / GetStringTypeExA / LCMapStringA work in the profile's code
    page (UTF-8), so their counts are byte counts (as in Windows' DBCS locales).
  - ExtTextOutW rejects ETO_GLYPH_INDEX (ERROR_INVALID_FUNCTION) because there is
    no glyph-index mapping for the host font; ETO_RTLREADING succeeds but does not
    reorder runs (no bidirectional layout).
- Next 3 steps:
  1. Dialog templates (`DLGTEMPLATE`/`DLGTEMPLATEEX`, CreateDialogIndirectParamW,
     CreateDialogParamW, DefDlgProcW) — **tagged `[strong]` in NEXT_TASKS (T3a/T3b)**.
  2. Then IsDialogMessageW/EndDialog modal loop (T3c) and the remaining 242 imports
     in the same import-by-import loop.
  3. Owner step: run `./npp.8.9.8.portable.x64/notepad++.exe` on the desktop when
     the bridge reports the visible main-window request, and report the screenshot.

STOPPED at the dialog-template boundary. Per AGENTS.md this area goes to a stronger
model, so no dialog code was started. No sudo, no kernel change, no push.
Stop again on PE exit 126, a new crash/hang, or the first visible main-window request.
