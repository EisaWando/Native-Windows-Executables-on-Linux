/* Child lookup and dialog-item operations; RT_DIALOG construction is separate. */
__attribute__((ms_abi)) int GetDlgCtrlID(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	return w ? (int)(uintptr_t)w->menu : 0;
}
__attribute__((ms_abi)) HANDLE GetDlgItem(HANDLE parent, int id)
{
	PE_STATE_GUARD;
	if (!pe_window_lookup(parent, false))
		return NULL;
	for (struct pe_window *w = pe_windows; w; w = w->next)
		if (!w->dead && (w->style & 0x40000000u) &&
		    w->parent == parent && (int)(uintptr_t)w->menu == id)
			return w->handle;
	return NULL;
}
__attribute__((ms_abi)) intptr_t SendDlgItemMessageW(HANDLE parent, int id,
						     DWORD msg, uintptr_t wp,
						     intptr_t lp)
{
	HANDLE child = GetDlgItem(parent, id);
	return child ? SendMessageW(child, msg, wp, lp) : 0;
}
__attribute__((ms_abi)) BOOL SetDlgItemTextW(HANDLE parent, int id,
					     const uint16_t *text)
{
	HANDLE child = GetDlgItem(parent, id);
	return child ? SendMessageW(child, 0xc, 0, (intptr_t)text) != 0 : 0;
}
__attribute__((ms_abi)) unsigned GetDlgItemTextW(HANDLE parent, int id,
						 uint16_t *text, int cap)
{
	if (!text || cap <= 0) {
		SetLastError(87);
		return 0;
	}
	text[0] = 0;
	HANDLE child = GetDlgItem(parent, id);
	return child ? SendMessageW(child, 0xd, cap, (intptr_t)text) : 0;
}
__attribute__((ms_abi)) BOOL SetDlgItemTextA(HANDLE parent, int id,
					     const char *text)
{
	if (!text)
		return SetDlgItemTextW(parent, id, NULL);
	int n = MultiByteToWideChar(65001, 8, text, -1, NULL, 0);
	if (!n)
		return 0;
	uint16_t *wide = pe_raw_map((size_t)n * 2);
	if (!wide) {
		SetLastError(8);
		return 0;
	}
	MultiByteToWideChar(65001, 8, text, -1, wide, n);
	BOOL result = SetDlgItemTextW(parent, id, wide);
	raw_syscall3(SYS_munmap, (long)wide, (size_t)n * 2, 0);
	return result;
}
__attribute__((ms_abi)) unsigned GetDlgItemTextA(HANDLE parent, int id,
						 char *text, int cap)
{
	if (!text || cap <= 0) {
		SetLastError(87);
		return 0;
	}
	text[0] = 0;
	HANDLE child = GetDlgItem(parent, id);
	if (!child)
		return 0;
	intptr_t len = SendMessageW(child, 0xe, 0, 0);
	if (len < 0 || len > 1048576) {
		SetLastError(87);
		return 0;
	}
	size_t bytes = ((size_t)len + 1) * 2;
	uint16_t *wide = pe_raw_map(bytes);
	if (!wide) {
		SetLastError(8);
		return 0;
	}
	unsigned n = SendMessageW(child, 0xd, len + 1, (intptr_t)wide);
	if (n > (unsigned)len)
		n = len;
	unsigned used = 0;
	for (unsigned i = 0; i < n;) {
		unsigned units = (wide[i] >= 0xd800 && wide[i] <= 0xdbff &&
				  i + 1 < n && wide[i + 1] >= 0xdc00 &&
				  wide[i + 1] <= 0xdfff) ?
					 2 :
					 1;
		char out[4];
		int count = WideCharToMultiByte(65001, 0, wide + i, units, out,
						4, NULL, NULL);
		if (!count || count >= cap - (int)used)
			break;
		memcpy(text + used, out, count);
		used += count;
		i += units;
	}
	text[used] = 0;
	raw_syscall3(SYS_munmap, (long)wide, bytes, 0);
	return used;
}
__attribute__((ms_abi)) BOOL SetDlgItemInt(HANDLE parent, int id,
					   unsigned value, BOOL sign)
{
	uint16_t text[16], reverse[12];
	unsigned n = 0, p = 0;
	bool negative = sign && (int32_t)value < 0;
	if (negative) {
		text[p++] = '-';
		value = 0u - value;
	}
	do {
		reverse[n++] = '0' + value % 10;
		value /= 10;
	} while (value);
	while (n)
		text[p++] = reverse[--n];
	text[p] = 0;
	return SetDlgItemTextW(parent, id, text);
}
__attribute__((ms_abi)) unsigned GetDlgItemInt(HANDLE parent, int id,
					       BOOL *translated, BOOL sign)
{
	if (translated)
		*translated = 0;
	uint16_t text[64];
	HANDLE child = GetDlgItem(parent, id);
	if (!child || SendMessageW(child, 0xe, 0, 0) >= 64 ||
	    !GetDlgItemTextW(parent, id, text, 64))
		return 0;
	unsigned i = 0;
	while (text[i] == ' ' || (text[i] >= 9 && text[i] <= 13))
		i++;
	bool negative = text[i] == '-';
	if (negative || text[i] == '+')
		i++;
	if (negative && !sign)
		return 0;
	if (text[i] < '0' || text[i] > '9')
		return 0;
	uint64_t result = 0,
		 limit = sign ? (negative ? 2147483648ULL : 2147483647ULL) :
				4294967295ULL;
	while (text[i] >= '0' && text[i] <= '9') {
		result = result * 10 + text[i++] - '0';
		if (result > limit)
			return 0;
	}
	if (translated)
		*translated = 1;
	return negative ? 0u - (unsigned)result : result;
}
__attribute__((ms_abi)) BOOL CheckDlgButton(HANDLE parent, int id,
					    unsigned check)
{
	HANDLE child = GetDlgItem(parent, id);
	if (!child)
		return 0;
	SendMessageW(child, 0xf1, check, 0);
	return 1;
}
__attribute__((ms_abi)) unsigned IsDlgButtonChecked(HANDLE parent, int id)
{
	return SendDlgItemMessageW(parent, id, 0xf0, 0, 0);
}
__attribute__((ms_abi)) BOOL CheckRadioButton(HANDLE parent, int first,
					      int last, int checked)
{
	if (first > last || !pe_window_lookup(parent, false)) {
		SetLastError(87);
		return 0;
	}
	struct pe_window *head;
	{
		PE_STATE_GUARD;
		head = pe_windows;
	}
	for (struct pe_window *w = head; w; w = w->next) {
		HANDLE target = NULL;
		int id;
		{
			PE_STATE_GUARD;
			id = (int)(uintptr_t)w->menu;
			if (!w->dead && w->parent == parent &&
			    (w->style & 0x40000000u) && id >= first &&
			    id <= last)
				target = w->handle;
		}
		if (target)
			SendMessageW(target, 0xf1, id == checked, 0);
	}
	return 1;
}
__attribute__((ms_abi)) BOOL IsChild(HANDLE parent, HANDLE child)
{
	PE_STATE_GUARD;
	if (!pe_window_lookup(parent, false))
		return 0;
	struct pe_window *w = pe_window_lookup(child, false);
	for (unsigned depth = 0; w && depth < 128 && (w->style & 0x40000000u);
	     depth++) {
		if (w->parent == parent)
			return 1;
		w = pe_window_lookup(w->parent, false);
	}
	return 0;
}
