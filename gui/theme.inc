/* Classic profile: persist window associations, but no Windows theme engine. */
__attribute__((ms_abi)) int32_t SetWindowTheme(HANDLE hwnd, const uint16_t *app,
					       const uint16_t *ids)
{
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return (int32_t)0x80070578u;
	uint16_t *a = app ? pe_control_copy(app) : NULL;
	uint16_t *b = ids ? pe_control_copy(ids) : NULL;
	if ((app && !a) || (ids && !b)) {
		if (a)
			raw_syscall3(SYS_munmap, (long)a, (lstrlenW(a) + 1) * 2,
				     0);
		if (b)
			raw_syscall3(SYS_munmap, (long)b, (lstrlenW(b) + 1) * 2,
				     0);
		return (int32_t)0x8007000eu;
	}
	{
		PE_STATE_GUARD;
		if (w->dead) {
			if (a)
				raw_syscall3(SYS_munmap, (long)a,
					     (lstrlenW(a) + 1) * 2, 0);
			if (b)
				raw_syscall3(SYS_munmap, (long)b,
					     (lstrlenW(b) + 1) * 2, 0);
			return (int32_t)0x80070578u;
		}
		if (w->theme_app)
			raw_syscall3(SYS_munmap, (long)w->theme_app,
				     (lstrlenW(w->theme_app) + 1) * 2, 0);
		if (w->theme_ids)
			raw_syscall3(SYS_munmap, (long)w->theme_ids,
				     (lstrlenW(w->theme_ids) + 1) * 2, 0);
		w->theme_app = a;
		w->theme_ids = b;
	}
	SendMessageW(hwnd, 0x31a, 0, 0);
	return 0;
}
__attribute__((ms_abi)) HANDLE OpenThemeData(HANDLE hwnd,
					     const uint16_t *classes)
{
	if ((hwnd && !pe_window_lookup(hwnd, false)) || !classes) {
		SetLastError(87);
		return NULL;
	}
	SetLastError(50);
	return NULL;
}
__attribute__((ms_abi)) HANDLE OpenThemeDataEx(HANDLE hwnd,
					       const uint16_t *classes,
					       DWORD flags)
{
	(void)flags;
	return OpenThemeData(hwnd, classes);
}
__attribute__((ms_abi)) HANDLE OpenThemeDataForDpi(HANDLE hwnd,
						   const uint16_t *classes,
						   unsigned dpi)
{
	(void)dpi;
	return OpenThemeData(hwnd, classes);
}
__attribute__((ms_abi)) HANDLE GetWindowTheme(HANDLE hwnd)
{
	if (!pe_window_lookup(hwnd, false))
		return NULL;
	return NULL;
}
__attribute__((ms_abi)) int32_t CloseThemeData(HANDLE theme)
{
	(void)theme;
	return (int32_t)0x80070006u;
}
/* Classic-theme profile: retain dialog texture selection without themed artwork. */
__attribute__((ms_abi)) int32_t EnableThemeDialogTexture(HANDLE hwnd,DWORD flags)
{
    if(!flags || (flags&~15u) || ((flags&1) && (flags&2))) return (int32_t)0x80070057u;
    PE_STATE_GUARD; struct pe_window *w=pe_window_lookup(hwnd,false);
    if(!w) return (int32_t)0x80070578u;
    w->dialog_texture_flags=(flags&1)?0:flags;
    return 0;
}
__attribute__((ms_abi)) BOOL IsThemeDialogTextureEnabled(HANDLE hwnd)
{
    PE_STATE_GUARD; struct pe_window *w=pe_window_lookup(hwnd,false);
    return w && !!(w->dialog_texture_flags&2);
}
