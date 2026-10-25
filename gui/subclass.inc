/* Original subclass chain. Nodes are stable through recursive removal/destruction. */
typedef intptr_t(__attribute__((ms_abi)) *
		 pe_subclass_proc)(HANDLE, DWORD, uintptr_t, intptr_t,
				   uintptr_t, uintptr_t);
struct pe_subclass {
	struct pe_subclass *next;
	pe_subclass_proc proc;
	uintptr_t id, data;
	bool active;
};
struct pe_subclass_frame {
	struct pe_subclass_frame *parent;
	struct pe_window *window;
	struct pe_subclass *next;
	pe_wndproc base;
};
static intptr_t pe_subclass_next(struct pe_subclass_frame *frame, HANDLE hwnd,
				 DWORD msg, uintptr_t wp, intptr_t lp)
{
	while (frame->next) {
		struct pe_subclass *s = frame->next;
		frame->next = s->next;
		if (s->active)
			return s->proc(hwnd, msg, wp, lp, s->id, s->data);
	}
	return frame->base ? frame->base(hwnd, msg, wp, lp) :
			     DefWindowProcW(hwnd, msg, wp, lp);
}
static __attribute__((ms_abi)) intptr_t pe_subclass_dispatch(HANDLE hwnd,
							     DWORD msg,
							     uintptr_t wp,
							     intptr_t lp)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	struct pe_ui *u = w->ui;
	struct pe_subclass_frame frame = { u->subclass_frame, w, w->subclasses,
					   w->subclass_base };
	u->subclass_frame = &frame;
	intptr_t result = pe_subclass_next(&frame, hwnd, msg, wp, lp);
	u->subclass_frame = frame.parent;
	return result;
}
__attribute__((ms_abi)) BOOL SetWindowSubclass(HANDLE hwnd,
					       pe_subclass_proc proc,
					       uintptr_t id, uintptr_t data)
{
	PE_STATE_GUARD;
	if (!proc) {
		SetLastError(87);
		return 0;
	}
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	struct pe_subclass *s = w->subclasses;
	while (s && (s->proc != proc || s->id != id))
		s = s->next;
	if (!s) {
		s = pe_raw_map(sizeof(*s));
		if (!s) {
			SetLastError(8);
			return 0;
		}
		s->proc = proc;
		s->id = id;
		s->next = w->subclasses;
		w->subclasses = s;
	}
	s->data = data;
	s->active = true;
	if (w->proc != pe_subclass_dispatch) {
		w->subclass_base = w->proc;
		w->proc = pe_subclass_dispatch;
	}
	return 1;
}
__attribute__((ms_abi)) BOOL GetWindowSubclass(HANDLE hwnd,
					       pe_subclass_proc proc,
					       uintptr_t id, uintptr_t *data)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	for (struct pe_subclass *s = w->subclasses; s; s = s->next)
		if (s->active && s->proc == proc && s->id == id) {
			if (data)
				*data = s->data;
			return 1;
		}
	return 0;
}
__attribute__((ms_abi)) BOOL RemoveWindowSubclass(HANDLE hwnd,
						  pe_subclass_proc proc,
						  uintptr_t id)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	struct pe_subclass *found = NULL;
	for (struct pe_subclass *s = w->subclasses; s; s = s->next)
		if (s->active && s->proc == proc && s->id == id) {
			found = s;
			break;
		}
	if (!found)
		return 0;
	found->active = false;
	for (struct pe_subclass *s = w->subclasses; s; s = s->next)
		if (s->active)
			return 1;
	if (w->proc == pe_subclass_dispatch)
		w->proc = w->subclass_base;
	return 1;
}
__attribute__((ms_abi)) intptr_t DefSubclassProc(HANDLE hwnd, DWORD msg,
						 uintptr_t wp, intptr_t lp)
{
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return 0;
	struct pe_subclass_frame *f = u->subclass_frame;
	if (f && f->window->handle == hwnd)
		return pe_subclass_next(f, hwnd, msg, wp, lp);
	return DefWindowProcW(hwnd, msg, wp, lp);
}
