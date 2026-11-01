# binfmt_pe — project overview

`binfmt_pe` runs selected Windows x64 PE executables on Linux through a small
kernel binfmt loader and a freestanding user-space bridge. The runtime and GUI
compatibility layer are original implementations; this project does not use
Wine or ReactOS code.

## Current verified state (2026-11-01)

- The latest full `bash tests/run_all.sh` run exited 0 with 83 PASS lines.
- The latest Notepad++ `-noPlugin` launch exited 127 with 443 imports resolved
  and 177 unimplemented. It stopped at `USER32!MessageBoxW`; its message text
  reports that `CreateDialogParam()` returned NULL. That run did not reach a
  visible-window request.
- The owner previously reported a first displayed Notepad++ window. The menu
  background, lower controls, text glyphs and tab input were incomplete in that
  desktop run. These owner-reported visuals are distinct from the latest
  automated launch.
- Console PEs, including multithreaded and synchronization regressions, pass
  the test suite. The GUI helper uses X11, Fontconfig and FreeType.

## Architecture

- `fs/binfmt_pe.c` validates/maps PE images and starts the bridge. Keep kernel
  changes small and avoid editing it unless specifically required.
- `win32_bridge.c`, `bridge/*.inc` and `gui/*.inc` implement imports and Win32
  state using raw syscalls and the Windows x64 ABI.
- `gui/host*.c` is a separate native X11 helper for windows, framebuffers and
  fonts. It uses the host's normal Linux loader.
- `tests/*.c` are MinGW-built PEs run by `tests/run_all.sh`.

## Current next work

1. Diagnose the `CreateDialogParam()` failure reported by the latest Notepad++
run, then implement any required message-box behavior with regression coverage.
2. Realize and paint visible child controls with correct clipping and damage.
3. Improve input routing, tab interaction, menu colors and font fallback.

The exact current checkpoint and raw outputs are in
[`docs/SESSION_STATE.md`](docs/SESSION_STATE.md). Source ownership is summarized
in [`docs/CODE_MAP.md`](docs/CODE_MAP.md); detailed GUI architecture is in
[`docs/GUI_DESIGN.md`](docs/GUI_DESIGN.md).

## Development requirements

Build the bridge and display helper with `bash gui/build.sh`. Run regressions
with `bash tests/run_all.sh`; the PE handler must already be loaded. Never use
`rmmod -f` or push unverified changes. Test kernel/module work in a VM. Follow
the shim and clean-room rules in `AGENTS.md`.
