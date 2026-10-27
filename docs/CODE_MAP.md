# CODE_MAP.md — source file overview

| File | Contents / purpose | Key functions |
|---|---|---|
| `win32_bridge.c` | Main bridge: startup (`_start`), PE loader, TEB/PEB init, syscall helpers, core KERNEL32 shims, `lookup_shim()` dispatch table, import resolution | `_start`, `raw_syscall3/4/6`, `lookup_shim`, `GetStdHandle`, `WriteFile`, `ExitProcess`, `SetLastError`, `pe_state_enter/leave`, `install_crash_handler`, `crash_handler` |
| `bridge/clipboard.inc` | Process-local clipboard, HGLOBAL handles, UTF-8/UTF-16 text synthesis and listeners | `OpenClipboard`, `SetClipboardData`, `GetClipboardData`, `GlobalAlloc`, `AddClipboardFormatListener` |
| `bridge/colors.inc` | DWM/UX theme colour helpers; HLS↔RGB conversions | `DwmGetColorizationColor`, `DwmIsCompositionEnabled`, `IsThemeActive`, `ColorRGBToHLS`, `ColorHLSToRGB`, `ColorAdjustLuma` |
| `bridge/com.inc` | COM/OLE initialisation stubs; CoTask heap; CoCreateInstance/CoGetClassObject (returns `E_NOINTERFACE`) | `CoInitializeEx`, `OleInitialize`, `CoTaskMemAlloc/Free/Realloc`, `CoCreateInstance`, `CoGetClassObject` |
| `bridge/crypto.inc` | KERNEL32/ADVAPI32 Crypt* shims; internal SHA-256; CNG hash provider objects | `pe_sha_init`, `pe_sha_update`, `pe_sha_finish`, `pe_crypto_new/pin/drop`, `CryptCreateHash`, `CryptHashData`, `CryptGetHashParam` |
| `bridge/date_format.inc` | GetDateFormatW/A and GetTimeFormatW/A for the English/UTC locale profile; pattern-based formatter | `pe_datetime`, `pe_date_put`, `pe_date_number`, `GetDateFormatW/A`, `GetTimeFormatW/A`, `GetDateFormatEx`, `GetTimeFormatEx` |
| `bridge/directories.inc` | Directory and path shims: SetCurrentDirectory, CreateDirectory, GetFullPathName, MulDiv | `SetCurrentDirectoryW/A`, `CreateDirectoryW/A`, `RemoveDirectoryW/A`, `GetFullPathNameW/A`, `MulDiv` |
| `bridge/format.inc` | wsprintfW/A and wvsprintf* (printf-subset formatter, freestanding) | `pe_format`, `wsprintfW/A`, `wvsprintfW/A` |
| `bridge/bitmaps.inc` | Software raster bitmap and blit shims for memory DCs (BitBlt, StretchBlt, PatBlt, GetDIBits, SetDIBits, SetStretchBltMode) | `CreateCompatibleBitmap`, `BitBlt`, `StretchBlt`, `PatBlt`, `GetDIBits`, `SetDIBits`, `SetStretchBltMode`, `GetStretchBltMode` |
| `bridge/dc_saved.inc` | Device context state stack (SaveDC/RestoreDC), origins, mapping modes and clipping regions | `SaveDC`, `RestoreDC`, `SetViewportOrgEx`, `GetViewportOrgEx`, `OffsetViewportOrgEx`, `SetWindowOrgEx`, `GetWindowOrgEx`, `SetMapMode`, `GetMapMode`, `GetClipBox`, `IntersectClipRect`, `ExcludeClipRect`, `SelectClipRgn`, `CreateRectRgn`, `CreateRectRgnIndirect`, `SetRectRgn` |
| `bridge/init_once.inc` | One-time initialisation (InitOnce*): spin/wait on a uintptr_t flag, single-threaded callback form | `InitOnceInitialize`, `InitOnceBeginInitialize`, `InitOnceComplete`, `InitOnceExecuteOnce` |
| `bridge/product.inc` | GetProductInfo — always returns "Professional" edition | `GetProductInfo` |
| `bridge/resources.inc` | PE resource-tree walker; FindResourceEx/W/A, LoadResource, SizeofResource, LockResource, LoadString*, LoadImage stub | `pe_resource_root`, `pe_resource_find`, `FindResourceExW`, `FindResourceW/A`, `LoadResource`, `LoadStringW/A` |
| `bridge/security.inc` | SID helpers: IsValidSid, AllocateAndInitializeSid, EqualSid, token/privilege stubs, OpenProcessToken | `IsValidSid`, `AllocateAndInitializeSid`, `FreeSid`, `EqualSid`, `CopySid`, `GetSidSubAuthority`, `OpenProcessToken`, `LookupPrivilegeValueW` |
| `bridge/trust.inc` | WinVerifyTrust stub — always returns `TRUST_E_NOSIGNATURE` | `WinVerifyTrust` |
| `bridge/gdi_enum.inc` | Minimal EnumFontFamiliesExW stub (superseded by `gui/enum_fonts.inc` in GUI builds) | `EnumFontFamiliesExW` |
| `gui/region_set.inc` | Growable disjoint rectangle sets; transactional union/intersection/subtraction, bounds and point containment | `pe_region_combine`, `pe_region_cut`, `pe_region_box` |
| `gui/update_region.inc` | Exact client update regions, erase/paint cycles, child redraw propagation and DC exclusion | `GetUpdateRgn`, `GetUpdateRect`, `InvalidateRgn`, `ValidateRgn`, `RedrawWindow`, `ExcludeUpdateRgn`, `BeginPaint`, `EndPaint` |
| `tests/update_rgn.c` | Empty/simple/complex update regions, partial validation, erase callbacks, children, DC clipping and invalid handles | `mainCRTStartup`, `proc` |
| `gui/bridge.inc` | Core GUI data structures (HWND/HDC object tables, message loop, pe_window/pe_dc, RegisterClass, CreateWindow, DestroyWindow, message pump) | `pe_window_create`, `pe_dc_get`, `RegisterClassExW`, `CreateWindowExW`, `DestroyWindow`, `GetMessageW`, `DispatchMessageW` |
| `gui/caret.inc` | Text-insertion caret: creation, show/hide, blink timer | `GetCaretBlinkTime`, `SetCaretBlinkTime`, `CreateCaret`, `DestroyCaret`, `ShowCaret`, `HideCaret` |
| `gui/controls.inc` | Built-in window class registration (BUTTON, EDIT, LISTBOX, etc.) and InitCommonControls | `InitCommonControlsEx`, `InitCommonControls`, `pe_builtin_class`, `pe_control_proc` |
| `gui/dc_state.inc` | Per-DC text/background rendering attributes | `SetTextAlign`, `GetTextAlign`, `GetTextColor`, `GetBkColor`, `GetBkMode`, `SetTextCharacterExtra`, `GetTextCharacterExtra` |
| `gui/dialog_template.inc` | Guarded standard/extended dialog template parsing, modeless creation and dialog unit mapping | `CreateDialogParamW/A`, `CreateDialogIndirectParamW/A`, `DefDlgProcW/A`, `MapDialogRect` |
| `gui/dialog_items.inc` | Dialog item helpers: GetDlgItem, SendDlgItemMessage, SetDlgItemText | `GetDlgCtrlID`, `GetDlgItem`, `SendDlgItemMessageW`, `SetDlgItemTextW`, `GetDlgItemTextW` |
| `gui/drop_targets.inc` | OLE drag-and-drop: RegisterDragDrop, RevokeDragDrop, IDropTarget vtable plumbing | `RegisterDragDrop`, `RevokeDragDrop`, `pe_drop_release`, `pe_drop_window_destroy` |
| `gui/enum_fonts.inc` | EnumFontFamiliesExW — calls host font-enumeration protocol and invokes FONTENUMPROC callback with real font names | `EnumFontFamiliesExW`, `pe_enum_font_name` |
| `gui/font_metrics.inc` | GetTextMetrics*, GetTextExtentPoint*, GetTextExtentExPoint* — queries DC current font via host and returns TEXTMETRIC and cumulative character extents | `GetTextMetricsW/A`, `GetTextExtentPoint32W/A`, `GetTextExtentPointW/A`, `GetTextExtentExPointW/A`, `pe_font_snapshot` |
| `gui/host.c` | Native X11 helper process: Xlib window creation, painting, font enumeration via Fontconfig/FreeType, protocol loop | `main`, `lookup`, `by_window`, `native_pixel`, `resize_surface`, `fill`, `on_error` |
| `gui/host_font_enum.inc` | Host-side Fontconfig font enumeration; fills pg_packet reply for bridge | `native_font_enumerate`, `native_enum_name`, `native_enum_string` |
| `gui/host_menus.inc` | Host-side native X11 menu drawing and event handling | `native_menu_close`, `native_menu_clear`, `native_menu_item`, `menu_label` |
| `gui/host_metrics.inc` | Host-side font query (size, ascent, etc.) via FreeType | `native_font_query` |
| `gui/imagelist.inc` | ImageList_* shims: create/destroy, add bitmaps, draw, colour table | `ImageList_Create`, `ImageList_Destroy`, `ImageList_Add`, `ImageList_GetImageCount`, `pe_iml`, `pe_iml_room` |
| `gui/image_pixels.inc` | Off-screen raster surfaces; DIB pixel read/write helpers | `pe_raster_new`, `pe_raster_free`, `pe_raster_get`, `pe_raster_set`, `pe_dib_component` |
| `gui/menus.inc` | CreateMenu/CreatePopupMenu, InsertMenuItem, GetMenuItemInfo, TrackPopupMenu | `CreateMenu`, `CreatePopupMenu`, `pe_menu_lookup`, `pe_menu_find`, `InsertMenuItemW`, `TrackPopupMenuEx` |
| `gui/properties.inc` | Window property list: SetProp/GetProp/RemoveProp (W and A) | `SetPropW/A`, `GetPropW`, `RemovePropW`, `pe_property_matches` |
| `gui/scaled_icon.inc` | LoadIconWithScaleDown — loads icon resource at requested size | `LoadIconWithScaleDown`, `pe_icon_failure` |
| `gui/scroll_state.inc` | Per-window scroll-bar state: SetScrollInfo, GetScrollInfo, SetScrollPos, ShowScrollBar | `pe_scroll_clear`, `pe_scroll_get`, `pe_scroll_clamp`, `pe_scroll_visible` |
| `gui/send.inc` | Synchronous cross-thread SendMessage, PostMessage, reply/cancel machinery | `pe_ui_send`, `pe_ui_deliver`, `pe_call_complete`, `pe_ui_cancel_window` |
| `gui/subclass.inc` | SetWindowSubclass / GetWindowSubclass / RemoveWindowSubclass — subclass chain dispatch | `SetWindowSubclass`, `GetWindowSubclass`, `RemoveWindowSubclass`, `pe_subclass_dispatch`, `pe_subclass_next` |
| `gui/theme.inc` | UxTheme stubs: SetWindowTheme, OpenThemeData, GetThemeColor, DrawThemeBackground | `SetWindowTheme`, `OpenThemeData`, `OpenThemeDataEx`, `GetWindowTheme`, `CloseThemeData` |
| `gui/window_state.inc` | SetWindowPos, MoveWindow, IsWindowEnabled, GetFocus, ShowWindow, IsWindowVisible, GetWindowRect | `SetWindowPos`, `MoveWindow`, `IsWindowEnabled`, `GetFocus`, `ShowWindow`, `GetWindowRect` |
| `tests/calendar.c` | Tests GetDateFormatW calendar/date edge cases | `mainCRTStartup` |
| `tests/caret.c` | Tests CreateCaret/ShowCaret/HideCaret from two threads | `mainCRTStartup`, `other_thread` |
| `tests/colors.c` | Tests DWM/HLS/RGB colour conversion shims | `mainCRTStartup` |
| `tests/com.c` | Tests CoInitialize, CoTaskMem*, CoCreateInstance | `mainCRTStartup`, `other` |
| `tests/commctl_ordinals.c` | Tests COMCTL32 ordinal imports (InitCommonControls, LoadIconWithScaleDown, SetWindowSubclass) | `mainCRTStartup` |
| `tests/controls.c` | Tests built-in window class message handling | `mainCRTStartup`, `callback`, `worker` |
| `tests/crypto.c` | Tests CryptCreateHash/CryptHashData/CryptGetHashParam SHA-256 | `mainCRTStartup`, `digest_is` |
| `tests/date_format.c` | Tests GetDateFormatW/A and GetTimeFormatW/A | `mainCRTStartup`, `equal`, `equala` |
| `tests/dc_state.c` | Tests DC text-attribute getters/setters | `mainCRTStartup` |
| `tests/dc_saved.c` | Tests SaveDC/RestoreDC, ViewportOrg, WindowOrg, MapMode, ClipBox, ClipRect, Regions | `mainCRTStartup`, `failed` |
| `tests/dialogs.c` | Resource and in-memory modeless dialogs, protected-page truncation and corrupt-template checks | `mainCRTStartup`, `proc` |
| `tests/dialog_items.c` | Tests GetDlgItem/SetDlgItemTextW | `mainCRTStartup` |
| `tests/directories.c` | Tests SetCurrentDirectory, CreateDirectory, GetFullPathName | `mainCRTStartup`, `same` |
| `tests/drop_targets.c` | Tests RegisterDragDrop/RevokeDragDrop with a dummy IDropTarget | `mainCRTStartup`, `allocator`, `query`, `add_ref` |
| `tests/enum_fonts.c` | Tests EnumFontFamiliesExW callback invocation | `mainCRTStartup`, `enumerate`, `stop`, `allocate` |
| `tests/exception_diag.c` | Tests crash-handler output format (synthetic metadata, intentional exit 127) | `mainCRTStartup` |
| `tests/exit_process_thread.c` | Tests ExitProcess terminates all threads | `mainCRTStartup`, `child` |
| `tests/files.c` | Tests CreateFile, ReadFile, WriteFile, SetFilePointer | `mainCRTStartup` |
| `tests/font_metrics.c` | Tests GetTextMetricsW and GetTextExtentPoint32W | `mainCRTStartup`, `failed` |
| `tests/text_extents.c` | Tests GetTextExtentExPointW and GetTextExtentExPointA with cumulative partial widths, max extent and fit calculations | `mainCRTStartup`, `failed` |
| `tests/format.c` | Tests wsprintfW/A and wvsprintfW/A format strings | `mainCRTStartup`, `equalw`, `equala`, `via_w` |
| `tests/bitmaps.c` | Tests CreateCompatibleBitmap, GetDIBits, SetDIBits, BitBlt, StretchBlt, PatBlt, GetPixel/SetPixel on memory DCs | `mainCRTStartup`, `failed` |
| `tests/gdi_objects.c` | Tests GDI object creation (pens, brushes, bitmaps) in a window | `mainCRTStartup`, `proc` |
| `tests/gui_failure.c` | Tests bridge behaviour when GUI host is absent | `mainCRTStartup` |
| `tests/heap.c` | Tests HeapAlloc/HeapReAlloc/HeapFree | `mainCRTStartup` |
| `tests/hello.c` | Minimal "Hello from PE!" smoke test (GetStdHandle, WriteFile, ExitProcess) | `mainCRTStartup` |
| `tests/imagelist.c` | Tests ImageList_Create, ImageList_Add, ImageList_GetImageCount, ImageList_Destroy | `mainCRTStartup`, `failed` |
| `tests/init_once.c` | Tests InitOnceExecuteOnce from multiple threads | `mainCRTStartup`, `initialize`, `worker` |
| `tests/paths.c` | Tests GetFullPathNameW and path-normalisation edge cases | `mainCRTStartup`, `equal` |
| `tests/queues.c` | Tests PostMessage/GetMessage queue between threads | `mainCRTStartup`, `child` |
| `tests/registry.c` | Tests RegOpenKeyExW, RegQueryValueExW, RegCloseKey stubs | `mainCRTStartup`, `worker` |
| `tests/resources.c` | Tests FindResourceW, LoadResource, LoadStringW, EnumResourceNames | `mainCRTStartup`, `enumerate`, `proc` |
| `tests/scroll_state.c` | Tests SetScrollInfo/GetScrollInfo | `mainCRTStartup` |
| `tests/security.c` | Tests SID allocation, equality and token open | `mainCRTStartup` |
| `tests/sync.c` | Tests TLS/FLS, critical sections, semaphores, events | `mainCRTStartup`, `fls_cleanup` |
| `tests/theme.c` | Tests OpenThemeData / CloseThemeData in a window | `mainCRTStartup`, `proc` |
| `tests/thread_fault.c` | Tests per-thread crash handler (naked faulting function) | `mainCRTStartup`, `fault` |
| `tests/threads.c` | Stress tests TLS, InterlockedIncrement, thread creation (20 threads) | `mainCRTStartup`, `locals`, `check` |
| `tests/thread_smoke.c` | Minimal CreateThread / WaitForSingleObject smoke test | `mainCRTStartup`, `child` |
| `tests/tls.c` | Tests static TLS (`.tls` section, IMAGE_TLS_DIRECTORY64) and callbacks | `mainCRTStartup`, `tls_data`, `callback` |
| `tests/trust.c` | Tests WinVerifyTrust returns expected error | `mainCRTStartup` |
| `tests/utf.c` | Tests MultiByteToWideChar / WideCharToMultiByte UTF-8/UTF-16 | `mainCRTStartup` |
| `tests/window_demo.c` | Simple window creation/message-loop demo (GUI display test) | `mainCRTStartup`, `window_proc` |
| `tests/windows.c` | Tests CreateWindow, subclassing, hidden windows, SetProp/GetProp | `mainCRTStartup`, `test_hidden_windows`, `test_subclass`, `proc` |
