/* Edition matches the Windows 10 Pro version/registry profile. */
__attribute__((ms_abi)) BOOL GetProductInfo(DWORD major, DWORD minor,
					    DWORD sp_major, DWORD sp_minor,
					    DWORD *product)
{
	(void)sp_major;
	(void)sp_minor;
	if (!product || major < 6) {
		SetLastError(87);
		return 0;
	}
	*product = major == 6 && minor == 0 ? 6 :
					      0x30; /* PRODUCT_PROFESSIONAL */
	return 1;
}
