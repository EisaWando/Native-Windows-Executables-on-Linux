/* Text/DC attribute state. Alignment-aware glyph placement is subsequent work. */
__attribute__((ms_abi)) unsigned SetTextAlign(HANDLE dc, unsigned align)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return UINT32_MAX;
	if ((align & ~0x11fu) || (align & 6) == 4 || (align & 24) == 16) {
		SetLastError(87);
		return UINT32_MAX;
	}
	unsigned old = d->u.dc.text_align;
	d->u.dc.text_align = align;
	return old;
}
__attribute__((ms_abi)) unsigned GetTextAlign(HANDLE dc)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	return d ? d->u.dc.text_align : UINT32_MAX;
}
__attribute__((ms_abi)) DWORD GetTextColor(HANDLE dc)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	return d ? d->u.dc.text_color : UINT32_MAX;
}
__attribute__((ms_abi)) DWORD GetBkColor(HANDLE dc)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	return d ? d->u.dc.background : UINT32_MAX;
}
__attribute__((ms_abi)) int GetBkMode(HANDLE dc)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	return d ? d->u.dc.background_mode : 0;
}
__attribute__((ms_abi)) int SetTextCharacterExtra(HANDLE dc, int extra)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return (int32_t)0x80000000u;
	int old = d->u.dc.character_extra;
	d->u.dc.character_extra = extra;
	return old;
}
__attribute__((ms_abi)) int GetTextCharacterExtra(HANDLE dc)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	return d ? d->u.dc.character_extra : (int32_t)0x80000000u;
}
