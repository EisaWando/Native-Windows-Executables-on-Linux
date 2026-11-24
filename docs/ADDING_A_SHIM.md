# Adding a Shim

1. **Implementation**: Create `bridge/<name>.inc` with `__attribute__((ms_abi))` function. Use `uint16_t*` for WCHAR, `uint32_t` for DWORD/BOOL. Return 0/NULL on failure, set `SetLastError()` for errors.

2. **Registration**: In `win32_bridge.c`, add `#include "bridge/<name>.inc"` near other includes. In `lookup_shim()`, add `if (!by_ordinal && ascii_equal_ci(function, "FunctionName")) return FunctionName;`.

3. **Headless vs GUI**: Header functions (window creation, dialogs) need both headless stubs and GUI implementations in `gui/` directory with X11/Win32 mapping.