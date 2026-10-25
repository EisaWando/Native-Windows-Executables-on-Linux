#define _WIN32_WINNT 0x0600
#include <windows.h>
static INIT_ONCE once;
static volatile LONG callbacks, errors;
static DWORD payload;
static HANDLE gate;
static BOOL CALLBACK initialize(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
	(void)o;
	(void)parameter;
	LONG attempt = InterlockedIncrement(&callbacks);
	Sleep(5);
	if (attempt == 1) {
		SetLastError(1234);
		return FALSE;
	}
	payload = 0x12345678;
	*context = &payload;
	return TRUE;
}
static DWORD WINAPI worker(void *unused)
{
	(void)unused;
	WaitForSingleObject(gate, 5000);
	void *context = 0;
	if (!InitOnceExecuteOnce(&once, initialize, 0, &context)) {
		if (GetLastError() != 1234)
			InterlockedIncrement(&errors);
		if (!InitOnceExecuteOnce(&once, initialize, 0, &context))
			InterlockedIncrement(&errors);
	}
	if (context != &payload || payload != 0x12345678)
		InterlockedIncrement(&errors);
	return 0;
}
void mainCRTStartup(void)
{
	int ok = 1;
	BOOL pending = FALSE;
	void *context = 0;
	InitOnceInitialize(&once);
	ok &= !InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending,
				       &context) &&
	      pending;
	gate = CreateEventW(0, TRUE, FALSE, 0);
	HANDLE threads[8];
	for (int i = 0; i < 8; i++) {
		threads[i] = CreateThread(0, 0, worker, 0, 0, 0);
		ok &= threads[i] != 0;
	}
	SetEvent(gate);
	ok &= WaitForMultipleObjects(8, threads, TRUE, 10000) == WAIT_OBJECT_0;
	for (int i = 0; i < 8; i++)
		CloseHandle(threads[i]);
	CloseHandle(gate);
	ok &= callbacks == 2 && !errors;
	ok &= InitOnceBeginInitialize(&once, INIT_ONCE_CHECK_ONLY, &pending,
				      &context) &&
	      !pending && context == &payload;
	INIT_ONCE async = INIT_ONCE_STATIC_INIT;
	ok &= InitOnceBeginInitialize(&async, INIT_ONCE_ASYNC, &pending, 0) &&
	      pending;
	ok &= InitOnceBeginInitialize(&async, INIT_ONCE_ASYNC, &pending, 0) &&
	      pending;
	ok &= !InitOnceBeginInitialize(&async, 0, &pending, 0) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !InitOnceComplete(&async, INIT_ONCE_ASYNC, (void *)3);
	ok &= !InitOnceComplete(&async, INIT_ONCE_ASYNC | INIT_ONCE_INIT_FAILED,
				0);
	ok &= InitOnceComplete(&async, INIT_ONCE_ASYNC, &payload);
	ok &= !InitOnceComplete(&async, INIT_ONCE_ASYNC, &payload);
	ok &= InitOnceBeginInitialize(&async, INIT_ONCE_CHECK_ONLY, &pending,
				      &context) &&
	      !pending && context == &payload;
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "init once ok\n" : "init once FAIL\n", ok ? 13 : 15,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
