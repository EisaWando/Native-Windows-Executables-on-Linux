/* Original INIT_ONCE implementation: word states 0 idle, 1 synchronous,
 * 3 asynchronous, aligned-context|2 complete. Release/acquire publishes data.
 * Contracts: Microsoft Learn InitOnceBeginInitialize / InitOnceComplete. */
__attribute__((ms_abi)) void InitOnceInitialize(uintptr_t *once)
{
	if (once)
		__atomic_store_n(once, 0, __ATOMIC_RELEASE);
}
__attribute__((ms_abi)) BOOL InitOnceBeginInitialize(uintptr_t *once,
						     DWORD flags, BOOL *pending,
						     void **context)
{
	if (!once || !pending || (flags & ~3u) || flags == 3) {
		SetLastError(87);
		return 0;
	}
	for (;;) {
		uintptr_t state = __atomic_load_n(once, __ATOMIC_ACQUIRE);
		if ((state & 3) == 2) {
			*pending = 0;
			if (context)
				*context = (void *)(state & ~(uintptr_t)3);
			return 1;
		}
		*pending = 1;
		if (context)
			*context = NULL;
		if (flags & 1) {
			SetLastError(997);
			return 0;
		}
		if (!state) {
			uintptr_t wanted = flags & 2 ? 3 : 1;
			if (__atomic_compare_exchange_n(once, &state, wanted,
							false, __ATOMIC_ACQ_REL,
							__ATOMIC_ACQUIRE))
				return 1;
			continue;
		}
		if (state != (flags & 2 ? 3u : 1u)) {
			SetLastError(87);
			return 0;
		}
		if (flags & 2)
			return 1;
		if (raw_syscall6(SYS_futex, (long)once, 128, 1, 0, 0, 0) < 0)
			raw_syscall3(SYS_sched_yield, 0, 0, 0);
	}
}
__attribute__((ms_abi)) BOOL InitOnceComplete(uintptr_t *once, DWORD flags,
					      void *context)
{
	if (!once || (flags & ~6u) || flags == 6 || ((uintptr_t)context & 3) ||
	    ((flags & 4) && context)) {
		SetLastError(87);
		return 0;
	}
	uintptr_t expected = flags & 2 ? 3 : 1;
	uintptr_t value = flags & 4 ? 0 : (uintptr_t)context | 2;
	if (!__atomic_compare_exchange_n(once, &expected, value, false,
					 __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
		SetLastError(87);
		return 0;
	}
	raw_syscall6(SYS_futex, (long)once, 129, INT32_MAX, 0, 0, 0);
	return 1;
}
typedef BOOL(__attribute__((ms_abi)) * pe_once_callback)(uintptr_t *, void *,
							 void **);
__attribute__((ms_abi)) BOOL InitOnceExecuteOnce(uintptr_t *once,
						 pe_once_callback callback,
						 void *parameter,
						 void **context)
{
	if (!callback) {
		SetLastError(87);
		return 0;
	}
	BOOL pending;
	if (!InitOnceBeginInitialize(once, 0, &pending, context))
		return 0;
	if (!pending)
		return 1;
	void *value = NULL;
	BOOL success = callback(once, parameter, &value);
	if (!success) {
		DWORD error = GetLastError();
		InitOnceComplete(once, 4, NULL);
		SetLastError(error);
		return 0;
	}
	if (!InitOnceComplete(once, 0, value)) {
		DWORD error = GetLastError();
		InitOnceComplete(once, 4, NULL);
		SetLastError(error);
		return 0;
	}
	if (context)
		*context = value;
	return 1;
}
