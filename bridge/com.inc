/* Apartment bookkeeping and task allocation only: no COM servers, marshaling,
 * clipboard, embedding or drag/drop backend. Unregistered classes fail explicitly.
 * Public contracts: Microsoft Learn CoInitializeEx / OleInitialize. */
__attribute__((ms_abi)) uint32_t CoInitializeEx(void *reserved, DWORD flags)
{
	if (reserved || (flags & ~14u))
		return 0x80070057u;
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return 0x8007000eu;
	unsigned mode = flags & 2;
	if (u->com_refs && u->com_mode != mode)
		return 0x80010106u;
	if (u->com_refs == UINT32_MAX)
		return 0x8000ffffu;
	uint32_t result = u->com_refs ? 1 : 0;
	u->com_mode = mode;
	u->com_refs++;
	return result;
}
__attribute__((ms_abi)) uint32_t CoInitialize(void *reserved)
{
	return CoInitializeEx(reserved, 2);
}
__attribute__((ms_abi)) void CoUninitialize(void)
{
	struct pe_ui *u = pe_ui_current();
	if (u && u->com_refs && !--u->com_refs)
		u->com_mode = 0;
}
__attribute__((ms_abi)) uint32_t OleInitialize(void *reserved)
{
	uint32_t result = CoInitializeEx(reserved, 2);
	if ((int32_t)result < 0)
		return result;
	struct pe_ui *u = pe_ui_current();
	uint32_t ole_result = u->ole_refs ? 1 : 0;
	u->ole_refs++;
	return ole_result;
}
__attribute__((ms_abi)) void OleUninitialize(void)
{
	struct pe_ui *u = pe_ui_current();
	if (u && u->ole_refs) {
		u->ole_refs--;
		CoUninitialize();
	}
}
__attribute__((ms_abi)) void *CoTaskMemAlloc(size_t size)
{
	return pe_heap_alloc(size, 0);
}
__attribute__((ms_abi)) void CoTaskMemFree(void *memory)
{
	pe_heap_free(memory);
}
__attribute__((ms_abi)) void *CoTaskMemRealloc(void *memory, size_t size)
{
	if (!memory)
		return pe_heap_alloc(size, 0);
	if (!size) {
		pe_heap_free(memory);
		return NULL;
	}
	return HeapReAlloc(GetProcessHeap(), 0, memory, size);
}
__attribute__((ms_abi)) uint32_t CoGetClassObject(const void *clsid,
						  DWORD flags, void *server,
						  const void *iid, void **out)
{
	(void)flags;
	if (!out)
		return 0x80004003u;
	*out = NULL;
	if (!clsid || !iid)
		return 0x80070057u;
	struct pe_ui *u = pe_ui_current();
	if (!u || !u->com_refs)
		return 0x800401f0u;
	if (server)
		return 0x80004001u;
	return 0x80040154u; /* REGDB_E_CLASSNOTREG: no installed COM server registry */
}
__attribute__((ms_abi)) uint32_t CoCreateInstance(const void *clsid,
						  void *outer, DWORD flags,
						  const void *iid, void **out)
{
	if (!out)
		return 0x80004003u;
	*out = NULL;
	if (outer)
		return 0x80040110u; /* CLASS_E_NOAGGREGATION */
	return CoGetClassObject(clsid, flags, NULL, iid, out);
}
