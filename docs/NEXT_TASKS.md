# NEXT_TASKS.md — current resume plan

Follow the rules in `AGENTS.md`. Make one tested, reviewable change at a time;
commit each green code batch, update `docs/SESSION_STATE.md`, and do not push
unless the owner explicitly asks.

## Current blocker

The latest automated `-noPlugin` run (2026-11-01) exited 127 at
`USER32!MessageBoxW`. Its text says `CreateDialogParam() return NULL`; investigate
why dialog creation failed before adding unrelated GUI work. That run reported
443 resolved and 177 unimplemented imports. The owner previously reported a
visible Notepad++ window, but it had white menus, incomplete lower-area drawing,
missing glyphs and slow/unresponsive tab clicks. The latest automated run did
not reach that window path.

## Ordered work after the blocker

1. Reproduce and fix the dialog-creation failure; implement any required
   `MessageBoxW/A` behavior with normal modal-window semantics and a regression.
2. Realize and paint visible child controls. Preserve measured nonzero toolbar,
   status-bar, tab and Scintilla geometry; add child clipping and dirty-rectangle
   presentation.
3. Route pointer and keyboard events to the hit-tested child, including tab
   selection notifications, capture, focus and button commands.
4. Finish menu colors and font fallback; never claim a visual result without a
   desktop run or pixel test.
5. Continue Notepad++ import batches. Start exception unwinding only if an
   actual caught SEH/C++ exception requires it; real plugin loading remains a
   separate larger feature.

## Verification baseline

On 2026-11-01, `bash tests/run_all.sh` exited 0 with 83 PASS lines. The suite's
informational Notepad++ run exited 127 at the dialog/MessageBox failure above.
See `docs/SESSION_STATE.md` for the raw latest command output and exact resume
command.

## Completed owner batches

## Explicit owner batches completed (2026-10-26)
- [x] Real EnumFontFamiliesExW via Fontconfig/FreeType; commit e09a0dce8.
- [x] ImageList family, real ARGB storage/resource DIBs, pixel tests and the
  named ImageList_LoadImageW. Earlier 0x17d inference was wrong; see COMCTL_ORDINALS.md.
- [x] Audit ordinal0x11 (InitCommonControls), correct 0x17d to LoadIconWithScaleDown;
  verify existing subclass ordinals. See COMCTL_ORDINALS.md and current session tests.

## Ordinal audit continuation completed
- [x] Correct 381 to LoadIconWithScaleDown, bind 17 InitCommonControls; prove
  410-413 by ordinal tests and document all seven imports (COMCTL_ORDINALS.md).
- [x] Headless scrollbar state family; green /tmp/scroll-batch.log.
- [ ] Next owner-authorized session: CreateCompatibleBitmap (316x400) and related
  real memory bitmap/drawing support. Stop after the current two green batches.
