# Resume state


- Current commit: 9b7858bac, opt-in first-paint window-tree diagnostic.
- Full suite: exit 0, 87 PASS, including tests/tab_control.exe.
- Post-commit Notepad++ -noPlugin: timed out after 30 seconds (exit 124), 443 resolved / 177 unimplemented; no CALLED UNIMPLEMENTED line or crash diagnostic in this noninteractive run.
- The owner confirmed that the main window appeared; interaction previously triggered SIGSEGV at 0x1400f1fff after TCM_GETITEMW left lParam unchanged. The new regression verifies that the stored lParam is returned, but the interactive crash has not been reproduced after the fix.
- The screenshot at `Test-Pics/Notepad++.png` shows the menu bar and File popup with a blank client area. `BRIDGE_DUMP_WINDOWS=1` first-paint output showed the Notepad++ main window at 0x0 (visible/realized), toolbar and status bar at 0x0 (visible/not realized), and primary Scintilla at x=2 y=28 size=-4x-54 (visible/not realized). Other tabs, splitters and dialogs were present; most children were not realized. The dump was captured in `/tmp/window-err.txt` in this session.
- Tab limitations: item rectangles and hit testing use approximate fixed widths; custom item-extra storage, full selection/focus semantics and full notification coverage remain incomplete.
- Desktop visual check (fish, from the project directory): `./npp.8.9.8.portable.x64/notepad++.exe -noPlugin`; screenshot from another terminal: `magick import -window root /tmp/notepadpp.png` (ImageMagick 7 syntax).
- Lock limitation: ordinary window drawing and text track damage; other direct helper drawing paths and GetDCEx override are not yet covered.
- Restart limitation: registration is process-local; automatic restart requires an external supervisor.
- Plain Notepad++: exit 127, KERNEL32!RaiseException while loading a plugin; real PE DLL loading is needed for plugins.
- Command-line limitation: the kernel does not populate PEB ProcessParameters, so no existing process-parameter strings were updated.
- Stop condition: Notepad++ raises C++ exception 0xE06D7363 containing `The specified module could not be found.\r\n`. The first catchable type is `std::basic_string<wchar_t>` (`.?AV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@`). The backtrace is `_CxxThrowException` <- plugin loader failure path <- plugin iteration <- startup. Continuing requires x64 exception dispatch/unwinding.
- Limitations: SEC_RESERVE unsupported; VirtualQuery only handles tracked views; mapped paths are host paths; mapping access checks and shared image backing are incomplete; menu-loop MSGFILTER not implemented; VerQueryValueA requires a buffer from GetFileVersionInfoA.
- Keyboard limitation: the layout is fixed to US English and only common scan-code mappings are available.
- Next: fix initial placement while updates are locked and propagate size to the child layout; realize and paint visible child controls; audit input routing and tab behavior.

- Current commit: 2c6fe3fc3, file sections and SEC_IMAGE views.
- Full suite: exit 0, 75 PASS, including tests/file_mapping.exe.
- Post-commit Notepad++: exit 127, dbghelp!ImageNtHeader, 425 resolved / 195 unimplemented.
- Limitations: SEC_RESERVE returns ERROR_NOT_SUPPORTED; VirtualQuery covers tracked views only; mapped filenames use host paths; named mapping access rights and shared image backing are incomplete; image relocations are not applied.
- Next: implement SEC_RESERVE and strengthen mapping semantics; implement VerQueryValueA and hook record reuse; continue Notepad++ at ImageNtHeader.

- Current commit: e3b9d04ed, bounded PE image layout parser and fixture test.
- Full suite: exit 0, 74 PASS, including tests/pe_image.exe.
- Post-commit Notepad++: exit 127, KERNEL32!CreateFileMappingW, 422 resolved / 198 unimplemented.
- Limitations: parser is preparatory and not yet connected to bridge mappings; no file-mapping APIs implemented.
- Next: implement section handles and SEC_IMAGE views; implement normal file/anonymous views and VirtualQuery; test and continue Notepad++.

- Current commit: 8a1657300, Unicode version-resource queries.
- Full suite: exit 0, 73 PASS, including version-query fixed data, strings and translations.
- Post-commit Notepad++: exit 127, KERNEL32!CreateFileMappingW, 422 resolved / 198 unimplemented.
- Limitations: VerQueryValueA remains unimplemented; version file reader selects first numeric resource and language.
- Next: implement and test file mapping APIs, including SEC_IMAGE requested by Notepad++; continue startup; revisit menu-loop hooks.

- Current commit: 868c753fc, PE file version-resource retrieval.
- Full suite: exit 0, 73 PASS, including tests/version.exe.
- Post-commit Notepad++: exit 127, VERSION!VerQueryValueW, 421 resolved / 199 unimplemented.
- Limitations: version resources use the first numeric resource and language; VerQueryValue remains unimplemented; 128 MiB PE file limit.
- Next: implement and test VerQueryValue; continue Notepad++ import loop; revisit menu-loop hooks.

- Current commit: 78a67ba57, CBT, keyboard, mouse and dialog filter hook dispatch.
- Full suite: exit 0, 72 PASS; hook regression covers CBT veto, keyboard and modal filter.
- Post-commit Notepad++: exit 127, VERSION!GetFileVersionInfoSizeW, 419 resolved / 201 unimplemented.
- Limitations: unhooked records remain allocated; global hooks are process-local; menu-loop MSGFILTER dispatch remains to be verified or implemented.
- Next: inspect VERSION APIs and implement version-resource support; cover menu-loop hooks; continue Notepad++ startup.

- Current commit: 746cf4239, window message hook dispatch.
- Full suite: exit 0, 72 PASS; hooks regression passed.
- Post-commit Notepad++: exit 127, VERSION!GetFileVersionInfoSizeW, 419 resolved / 201 unimplemented.
- Limitations: unhooked records remain allocated; CBT, keyboard, mouse and modal filter hooks are not dispatched yet; global hooks are process-local.
- Next: implement CBT/input/filter hook dispatch; run and commit regressions; resume Notepad++ loop.

- Current commit: 1e1dbc985, hook registry and synchronous WH_CALLWNDPROC dispatch.
- Full suite: exit 0, 72 PASS; tests/hooks.exe passed.
- Post-commit Notepad++: exit 127, VERSION!GetFileVersionInfoSizeW, 419 resolved / 201 unimplemented.
- Limitations: unhooked records remain allocated; hooks other than WH_CALLWNDPROC are registered but not dispatched yet; process-local global hooks.
- Next: implement remaining message hook dispatch; add CBT/input/modal hooks; continue Notepad++ loop.

- Current commit: 3e303e433 — modal dialog loops and per-thread timers.
- Full suite: `bash tests/run_all.sh > /tmp/run.log 2>&1` exit 0; 71 PASS, including `tests/modal_dialogs.exe`.
- Post-commit Notepad++: exit 127, USER32!SetWindowsHookExW; 416 resolved / 204 unimplemented.
- Limitations: ANSI dialog-item conversion covers supported text messages; other messages are forwarded unchanged.
- Uncommitted work: this SESSION_STATE refresh.
- Next: implement and test hook chains; run the full suite and commit; resume Notepad++ startup.
- Latest batch: system colors and software drawing (commit 6f5cc209b).
- Last full suite: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside sandbox, exit 0, 70 PASS; tests/syscolors_draw.exe PASS; visible tests SKIP.
- Latest suite and post-commit Notepad++: exit 127, 408 resolved / 212 unimplemented; USER32!SetWindowsHookExW, WH_CALLWNDPROC (4). No visible top-level request or new crash observed.
- Implemented cached shared system brushes, process-local color changes and notification, bitmap FillRect/FrameRect/InvertRect/DrawFocusRect, classic edges/frame controls, icon mask/alpha composition, DrawText layout and FreeType software glyph transport, GrayStringW and DrawStateW; added common fixed-profile SPI getters.
- Limits: classic frame glyphs are approximate; native window-DC inversion is unsupported; text shaping/bidirectional layout and PNG/RLE icons remain unsupported. Unsupported SPI actions fail explicitly. System colors are process-local.
- Next steps: implement the owner-requested modal dialog batch; implement actual WH_CALLWNDPROC hooks; continue the Notepad++ loop.
- Uncommitted work: this post-commit SESSION_STATE refresh.
- Do not access the stash named solar-pro4 freebuff attempt.

- Previous batch: window tree, reparenting and z-order (commit 3f85dd40c).
- Previous full suite: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside sandbox, exit 0, 69 PASS; tests/window_tree.exe PASS; visible tests SKIP.
- Previous suite and post-commit Notepad++: exit 127, 400 resolved / 220 unimplemented, USER32!GetSysColorBrush ret=0x14008ea9d; no visible top-level request or new crash observed.
- Limits: popup owner and parent still share metadata, so SetParent on an owned popup can return the owner instead of the desktop; owner-chain topmost propagation is not implemented. Deferred native updates are sequential, persistent X11 topmost depends on the window manager, and child controls remain headless.
- Next steps: separate popup owners from parents and propagate topmost state; implement GetSysColorBrush; resume the owner-requested modal-dialog batch when quota allows.
- Uncommitted work: this post-commit SESSION_STATE refresh.

- Previous batch: coordinates and hit testing (commit 01764a6a6).
- Previous full suite: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside the sandbox, exit 0, 68 PASS; tests/coords.exe PASS, visible tests SKIP.
- Previous suite and post-commit Notepad++: exit 127, 389 resolved / 231 unimplemented, USER32!SetParent ret=0x14008c9f3.
- Coordinate mapping uses stored parent-client positions and the 24-pixel menu strip; pointer events update the logical cursor and queued-message coordinates. SetCursorPos does not warp X11. Hit testing uses the newest-first window list; explicit z-order changes and RTL mirroring are not implemented.
- Next steps: conditional modal-dialog batch if the owner quota permits; investigate SetParent; repeat the regression and Notepad++ loop.
- Previous post-commit SESSION_STATE refresh was incorporated in the current batch.

- Latest batch: modeless dialog templates and creation (commit a24f76fae). Standard and extended resource/indirect templates use
  bounded guarded reads, a 4096-item limit, and bounded UTF-16 strings.
- Previous full regression: `bash tests/run_all.sh > /tmp/run.log 2>&1` outside the
  sandbox, exit 0, 67 PASS; `tests/dialogs.exe` PASS. Visible tests SKIP.
- Previous suite and post-commit Notepad++: exit 127, 382 resolved / 238 unimplemented;
  USER32!ScreenToClient ret=0x140089408. No PE exit 126 or new crash/hang.
- Dialog-unit base: 8x16 without a template font; a specified font uses its
  52-letter mean width and text height at 96 DPI. If host measurement fails,
  the existing implementation falls back to 8x16.
- Limitations: resource IDs in control titles are currently treated as string
  IDs, not icon/bitmap resources; a missing string becomes an empty caption.
  SendDlgItemMessageA converts WM_SETTEXT/WM_GETTEXT only. Custom dialog classes
  must supply dispatch and 24 extra bytes. Modal dialogs and IsDialogMessage
  remain unimplemented. The indirect API supplies no true byte length, so the
  parser cannot identify an object boundary inside otherwise readable memory.
- Next three steps: review and complete ordinal control-resource handling;
  implement coordinate conversions (ScreenToClient/ClientToScreen); then resume
  Notepad++ under the next owner-authorized batch.

- Clipboard follow-up: existing `bridge/clipboard.inc` now synthesizes UTF-8 CF_TEXT/CF_OEMTEXT and UTF-16 CF_UNICODETEXT, tracks the opener thread, reports error 1418 for closed operations, validates global handles through a locked registry, preserves clipboard-owned memory, and posts WM_CLIPBOARDUPDATE to registered listeners.
- Latest outside-sandbox `/tmp/run.log`: 66 PASS lines, no FAIL, including `tests/clipboard.exe`; visible tests skipped. The process exit status was lost when the session was interrupted after the log completed. The first sandboxed run failed three native font-helper tests; the outside-sandbox retry passed them.
- Clipboard remains process-local; text synthesis uses UTF-8 for ANSI/OEM. Delayed-rendered text gains synthesized formats only after rendering.


- Earlier commit: a24f76fae — modeless dialog templates and creation.
- Previous recorded green: `bash tests/run_all.sh > /tmp/run.log 2>&1` exit 0, **66 PASS**, no FAIL.
  Display test `timeout 30s tests/windows.exe` (DISPLAY=:0, real X11 + FreeType
  helper) prints `windows ok`, exit 0, including the new ExtTextOutW pixel checks.
- Earlier Notepad++ stop: exit 127, **USER32!CreateDialogIndirectParamW**
  (rcx=HINSTANCE 0x14000000, rdx=DLGTEMPLATE, r8=owner HWND, r9=0x1401e9e80).
  **380 resolved / 240 unimplemented** before this batch.
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
- Earlier next steps (before this batch):
  1. Dialog templates (`DLGTEMPLATE`/`DLGTEMPLATEEX`, CreateDialogIndirectParamW,
     CreateDialogParamW, DefDlgProcW) — **tagged `[strong]` in NEXT_TASKS (T3a/T3b)**.
  2. Then IsDialogMessageW/EndDialog modal loop (T3c) and the remaining 240 imports
     in the same import-by-import loop.
  3. Owner step: run `./npp.8.9.8.portable.x64/notepad++.exe` on the desktop when
     the bridge reports the visible main-window request, and report the screenshot.

STOPPED after the owner-authorized modeless dialog batch. No sudo, no kernel change, no push.
Stop again on PE exit 126, a new crash/hang, or the first visible main-window request.
