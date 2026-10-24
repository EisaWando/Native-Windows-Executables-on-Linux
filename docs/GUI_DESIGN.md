# Original USER32/GDI layer: first visible window

The owner approved an original implementation on 2026-10-24. No Wine or
ReactOS implementation is used. The kernel remains a PE/bridge loader.

Windows applications call USER32/GDI exports through the existing import bridge.
The bridge owns HWND/HDC validation, window procedures (Windows x64 ABI),
creation/destruction, thread ownership and message queues. Application callbacks
run on the creating thread, outside the bridge state lock. Only custom Unicode
classes are initially supported; unsupported window styles/features fail.

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
needed. Missing helper/display returns an error; no invisible successful window
is fabricated. Existing headless GDI metadata APIs continue to work.

The initial limits are documented in HANDOFF_v3.md after actual verification.
Reference contracts: Microsoft Learn CreateWindowExW, WM_CREATE, BeginPaint,
GetMessageW, DestroyWindow; X.Org libX11 manual. These are API documentation,
not implementation sources.
