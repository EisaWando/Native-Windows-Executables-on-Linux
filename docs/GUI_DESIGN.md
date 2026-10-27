# Original USER32/GDI layer: first visible window

The owner approved an original implementation on 2026-10-24. No Wine or
ReactOS implementation is used. The kernel remains a PE/bridge loader.

Windows applications call USER32/GDI exports through the existing import bridge.
The bridge owns HWND/HDC validation, window procedures (Windows x64 ABI),
creation/destruction, thread ownership and message queues. Application callbacks
run on the creating thread, outside the bridge state lock. Custom Unicode classes are supported. Every style bit is retained as logical
state, although not every style has a native visual effect. Hidden HWNDs do not
open X11 connections or allocate native windows; ShowWindow realizes a surface.

A native `pe_gui_host` ELF helper owns the X11 connection, window-manager
interaction and software RGB buffers. It is a separate executable, using Xlib,
Fontconfig and FreeType through the normal Linux loader. This avoids introducing
an ELF dynamic loader or libc TLS assumptions into the freestanding bridge.
This is an original compatibility layer using Linux libraries, not Wine reuse.
XWayland is the first Wayland desktop path; a native Wayland backend can replace
the helper without changing the Windows ABI.

One helper/ordered Unix SOCK_SEQPACKET connection belongs to each GUI thread.
The bridge forks only to exec the helper; the fork child calls raw syscalls and
never acquires inherited locks. The helper inherits only its channel at fd 3,
uses close-on-exec for unrelated descriptors and terminates on channel EOF.
All requests and responses are bounded, fixed-layout packets. The native side
validates sizes, handles and dimensions. Blocking transport never holds the
bridge global lock. Requests have a timeout, and send uses MSG_NOSIGNAL.
Display events are polled with a bounded wait so posted Windows messages can
wake a message loop without waiting indefinitely for X events.

GDI initially draws into a native per-window software framebuffer: solid fills,
lines, rectangle outlines, pixels and FreeType grayscale text. XPutImage presents
it. COLORREF is converted to RGB, clipping is to the client rectangle, and text
uses Fontconfig substitution (Segoe UI may be replaced). X11 events become a
small explicit subset of Windows messages. Expose invalidates the client area;
WM_DELETE_WINDOW becomes WM_CLOSE. Only the owning thread dispatches callbacks.

Build using `bash gui/build.sh`. The exact handoff bridge GCC flags remain.
The script generates the helper's absolute path in gui/host_path.h, with a
BINFMT_PE_GUI_HELPER environment override for relocation. No kernel reload is
needed. Missing helper/display returns an error when a native window is requested;
hidden logical windows remain fully usable without a display. Existing headless GDI metadata APIs continue to work.

The initial limits are documented in HANDOFF_v3.md after actual verification.
Reference contracts: Microsoft Learn CreateWindowExW, WM_CREATE, BeginPaint,
GetMessageW, DestroyWindow; X.Org libX11 manual. These are API documentation,
not implementation sources.

Hidden-window semantics and per-window properties/subclass chains were verified
on 2026-10-25. Zero/negative sizes are retained in logical state; physical X11
surfaces use at least one pixel and are bounded to 4096x4096. Stored styles such
as LAYERED/ACCEPTFILES do not imply those effects/transport are implemented.

Resource/menu layer (2026-10-25): main-image PE resources use guarded reads and
bounds-checked directory offsets. Class menu resources are instantiated before
WM_NCCREATE. HMENUs and accelerators use typed, growable mmap-backed objects.
The helper draws the menu bar inside the X11 surface; the bridge reserves 24
pixels and offsets application drawing/input so GetClientRect, WM_SIZE,
WM_NCCALCSIZE and AdjustWindowRect agree. Mouse popups send WM_COMMAND.
Keyboard menu navigation, owner draw and complete frame metrics remain pending.
Resource fixtures passed headlessly; native menu rendering is not yet verified.

Startup state support also includes per-thread carets (position, hide nesting,
shape metadata; rendering pending), COM apartment/refcount bookkeeping, and
child-control IDs/dialog-item messages and text. OLE drop-target registration
stores interface references and releases them on revoke/window destruction.
Application AddRef/Release callbacks run outside the global lock. This does not
provide drag/drop transport, clipboard, COM servers or marshaling. Dialog item
helpers operate on existing HWNDs; RT_DIALOG construction remains pending.

Font metrics/extent queries now use the native helper's Fontconfig/FreeType path
without an X display. The helper initializes FreeType on startup and connects
X11 only for PG_CREATE; polling without a surface stays display-independent.
Measurements use the same simple glyph advances as rendering, not complex text
shaping. The full visible suite passed after this change (2026-10-25).

Modeless dialog templates use the exact RT_DIALOG resource size, or guarded
reads capped at 1 MiB for an indirect template. The latter API supplies no
allocation length, so readable memory beyond the caller's intended object
cannot be distinguished from template bytes. Item count is capped at 4096 and
strings at 1024 UTF-16 units. Indirect reads check page permissions before the
existing guarded copy.

Dialog units use the requested font's measured 52-letter mean width and text
height at 96 DPI. Templates without a font use fixed base units of 8 by 16;
the same fallback is used if the font helper cannot measure the requested font.
Modal dialogs, the modal loop, and IsDialogMessage remain unimplemented.

Current limitations: an ordinal control title is looked up as a string-table
ID; icon/bitmap ordinal titles are not yet loaded into static controls. If
that string is absent, the current implementation creates an empty caption.
SendDlgItemMessageA converts WM_SETTEXT and WM_GETTEXT; other messages are
forwarded without message-specific ANSI conversion. The dialog default proc
inherits the existing limited DefWindowProc behavior. Custom dialog window
classes must provide suitable dialog-procedure dispatch and 24 extra bytes.
