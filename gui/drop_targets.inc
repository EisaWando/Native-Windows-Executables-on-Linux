/* Headless OLE target registration/lifetime only. No DoDragDrop/XDND transport.
 * Public contracts: Microsoft Learn RegisterDragDrop and RevokeDragDrop. */
struct pe_drop_vtable {
	void *query_interface;
	uint32_t(__attribute__((ms_abi)) * add_ref)(void *);
	uint32_t(__attribute__((ms_abi)) * release)(void *);
};
static void pe_drop_release(void *target)
{
	const struct pe_drop_vtable *v =
		*(const struct pe_drop_vtable **)target;
	v->release(target);
}
static void pe_drop_window_destroy(struct pe_window *w)
{
	void *target;
	{
		PE_STATE_GUARD;
		target = w->drop_target;
		w->drop_target = NULL;
	}
	if (target)
		pe_drop_release(
			target); /* never invoke PE code under runtime lock */
}
__attribute__((ms_abi)) uint32_t RegisterDragDrop(HANDLE hwnd, void *target)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0x80040102u;
	if (!target)
		return 0x80070057u;
	const struct pe_drop_vtable *v =
		*(const struct pe_drop_vtable **)target;
	if (!v || !v->add_ref || !v->release)
		return 0x80070057u;
	{
		PE_STATE_GUARD;
		if (!w->ui->ole_refs)
			return 0x8007000eu;
		if (w->drop_target)
			return 0x80040101u;
	}
	v->add_ref(target);
	uint32_t error = 0;
	{
		PE_STATE_GUARD;
		if (w->dead || w->destroying)
			error = 0x80040102u;
		else if (w->drop_target)
			error = 0x80040101u;
		else
			w->drop_target = target;
	}
	if (error)
		pe_drop_release(target);
	return error;
}
__attribute__((ms_abi)) uint32_t RevokeDragDrop(HANDLE hwnd)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0x80040102u;
	void *target;
	{
		PE_STATE_GUARD;
		target = w->drop_target;
		w->drop_target = NULL;
	}
	if (!target)
		return 0x80040100u;
	pe_drop_release(target);
	return 0;
}
