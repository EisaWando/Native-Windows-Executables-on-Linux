/* No Authenticode trust provider is installed in this runtime. Never report
 * successful verification; callers must choose their own failure/fallback. */
struct pe_wintrust_data {
	DWORD size;
	void *policy, *sip;
	DWORD ui, revocation, choice;
	void *subject;
	DWORD action;
	HANDLE state;
	const uint16_t *url;
	DWORD flags, context;
};
_Static_assert(sizeof(struct pe_wintrust_data) == 80,
	       "WINTRUST_DATA legacy ABI");
__attribute__((ms_abi)) int32_t WinVerifyTrust(HANDLE hwnd, const void *action,
					       struct pe_wintrust_data *data)
{
	(void)hwnd;
	static const unsigned char generic_verify[16] = {
		0x6b, 0xc5, 0xaa, 0,	0x44, 0xcd, 0xd0, 0x11,
		0x8c, 0xc2, 0,	  0xc0, 0x4f, 0xc2, 0x95, 0xee
	};
	if (!action || !data || data->size < 80)
		return (int32_t)0x80070057;
	if (memcmp(action, generic_verify, 16))
		return (int32_t)0x800b0002;
	if (data->action == 2)
		return data->state ? (int32_t)0x80070057 : 0;
	if (data->action > 1)
		return (int32_t)0x80070057;
	raw_puts(
		"[BRIDGE] WinVerifyTrust: no trust provider; verification failed");
	if (data->choice == 1 && data->subject) {
		const uint16_t *path = NULL;
		if (diagnostic_read((uintptr_t)data->subject + 8, &path, 8)) {
			raw_puts(" file=");
			print_utf16_preview((uintptr_t)path);
		}
	}
	raw_puts("\n");
	return (int32_t)0x800b0001;
}
