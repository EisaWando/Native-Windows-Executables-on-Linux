/* Cross-thread sends execute on the receiving GUI thread. Waiting senders also
 * service incoming sends, allowing reciprocal calls without a queue deadlock. */
struct pe_ui_call {
	struct pe_ui_call *next, *active_next, *outgoing_next;
	struct pe_ui *receiver;
	HANDLE hwnd;
	DWORD msg;
	uintptr_t wp;
	intptr_t lp, result;
	DWORD error;
	unsigned refs;
	uint32_t done;
};
static uint32_t pe_ui_sequence;
static void pe_call_drop(struct pe_ui_call *c)
{
	if (!--c->refs)
		raw_syscall3(SYS_munmap, (long)c, sizeof(*c), 0);
}
static void pe_call_complete(struct pe_ui_call *c, intptr_t result, DWORD error)
{
	c->result = result;
	c->error = error;
	__atomic_store_n(&c->done, 1, __ATOMIC_RELEASE);
	__atomic_add_fetch(&pe_ui_sequence, 1, __ATOMIC_RELEASE);
	raw_syscall6(SYS_futex, (long)&pe_ui_sequence, 129, INT32_MAX, 0, 0, 0);
	pe_call_drop(c);
}
static void pe_ui_deliver(struct pe_ui *u)
{
	for (;;) {
		struct pe_ui_call *c;
		{
			PE_STATE_GUARD;
			c = u->calls;
			if (!c)
				return;
			u->calls = c->next;
			c->active_next = u->active_calls;
			u->active_calls = c;
		}
		struct pe_window *w = pe_window_lookup(c->hwnd, true);
		intptr_t result = w ? w->proc(c->hwnd, c->msg, c->wp, c->lp) :
				      0;
		{
			PE_STATE_GUARD;
			struct pe_ui_call **p = &u->active_calls;
			while (*p && *p != c)
				p = &(*p)->active_next;
			if (*p)
				*p = c->active_next;
			pe_call_complete(c, result, w ? 0 : 1400);
		}
	}
}
static intptr_t pe_ui_send(HANDLE hwnd, DWORD msg, uintptr_t wp, intptr_t lp)
{
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	if (w->ui->tid == GetCurrentThreadId())
		return w->proc(hwnd, msg, wp, lp);
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return 0;
	struct pe_ui_call *c = pe_raw_map(sizeof(*c));
	if (!c) {
		SetLastError(8);
		return 0;
	}
	*c = (struct pe_ui_call){ .receiver = w->ui,
				  .hwnd = hwnd,
				  .msg = msg,
				  .wp = wp,
				  .lp = lp,
				  .refs = 2 };
	{
		PE_STATE_GUARD;
		if (w->dead || !w->ui->tid) {
			raw_syscall3(SYS_munmap, (long)c, sizeof(*c), 0);
			SetLastError(1400);
			return 0;
		}
		struct pe_ui_call **tail = &w->ui->calls;
		while (*tail)
			tail = &(*tail)->next;
		*tail = c;
		c->outgoing_next = u->outgoing;
		u->outgoing = c;
		__atomic_add_fetch(&pe_ui_sequence, 1, __ATOMIC_RELEASE);
		raw_syscall6(SYS_futex, (long)&pe_ui_sequence, 129, INT32_MAX,
			     0, 0, 0);
	}
	while (!__atomic_load_n(&c->done, __ATOMIC_ACQUIRE)) {
		uint32_t seq =
			__atomic_load_n(&pe_ui_sequence, __ATOMIC_ACQUIRE);
		pe_ui_deliver(u);
		if (!__atomic_load_n(&c->done, __ATOMIC_ACQUIRE)) {
			struct pe_timespec timeout = { 0, 16000000 };
			raw_syscall6(SYS_futex, (long)&pe_ui_sequence, 128, seq,
				     (long)&timeout, 0, 0);
		}
	}
	intptr_t result = c->result;
	DWORD error = c->error;
	{
		PE_STATE_GUARD;
		struct pe_ui_call **p = &u->outgoing;
		while (*p && *p != c)
			p = &(*p)->outgoing_next;
		if (*p)
			*p = c->outgoing_next;
		pe_call_drop(c);
	}
	if (error)
		SetLastError(error);
	return result;
}
static void pe_ui_cancel_window(struct pe_window *w)
{
	PE_STATE_GUARD;
	struct pe_ui_call **p = &w->ui->calls;
	while (*p) {
		struct pe_ui_call *c = *p;
		if (c->hwnd == w->handle) {
			*p = c->next;
			pe_call_complete(c, 0, 1400);
		} else
			p = &c->next;
	}
}
static void pe_ui_cancel(struct pe_ui *u)
{
	PE_STATE_GUARD;
	while (u->calls) {
		struct pe_ui_call *c = u->calls;
		u->calls = c->next;
		pe_call_complete(c, 0, 1400);
	}
	while (u->active_calls) {
		struct pe_ui_call *c = u->active_calls;
		u->active_calls = c->active_next;
		pe_call_complete(c, 0, 1400);
	}
	while (u->outgoing) {
		struct pe_ui_call *c = u->outgoing;
		u->outgoing = c->outgoing_next;
		pe_call_drop(c);
	}
}
