#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <ole2.h>
static DWORD WINAPI other(void *unused)
{
	(void)unused;
	int ok = CoInitializeEx(0, COINIT_MULTITHREADED) == S_OK;
	ok &= OleInitialize(0) == RPC_E_CHANGED_MODE;
	ok &= CoInitializeEx(0, COINIT_MULTITHREADED) == S_FALSE;
	CoUninitialize();
	CoUninitialize();
	ok &= OleInitialize(0) == S_OK;
	OleUninitialize();
	return ok ? 0 : 1;
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written, code;
	static const GUID unknown = {
		0x12345678, 0x1234, 0x5678, { 1, 2, 3, 4, 5, 6, 7, 8 }
	};
	static const GUID iid = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
	void *object = (void *)1;
	ok &= CoCreateInstance(&unknown, 0, CLSCTX_INPROC_SERVER, &iid,
			       &object) == CO_E_NOTINITIALIZED &&
	      !object;
	ok &= CoInitializeEx((void *)1, 0) == E_INVALIDARG;
	ok &= CoInitializeEx(0, COINIT_APARTMENTTHREADED) == S_OK;
	ok &= CoInitialize(0) == S_FALSE;
	ok &= CoInitializeEx(0, COINIT_MULTITHREADED) == RPC_E_CHANGED_MODE;
	ok &= OleInitialize(0) == S_OK && OleInitialize(0) == S_FALSE;
	ok &= CoCreateInstance(&unknown, 0, CLSCTX_INPROC_SERVER, &iid,
			       &object) == REGDB_E_CLASSNOTREG &&
	      !object;
	HANDLE t = CreateThread(0, 0, other, 0, 0, 0);
	ok &= t && WaitForSingleObject(t, 5000) == WAIT_OBJECT_0 &&
	      GetExitCodeThread(t, &code) && code == 0;
	CloseHandle(t);
	OleUninitialize();
	OleUninitialize();
	CoUninitialize();
	CoUninitialize();
	ok &= CoInitializeEx(0, COINIT_MULTITHREADED) == S_OK;
	CoUninitialize();
	unsigned char *p = CoTaskMemAlloc(32);
	ok &= p != 0;
	if (p) {
		p[0] = 41;
		p[31] = 42;
		unsigned char *q = CoTaskMemRealloc(p, 128);
		ok &= q != 0;
		if (q) {
			p = q;
			ok &= p[0] == 41 && p[31] == 42;
		}
		CoTaskMemFree(p);
	}
	p = CoTaskMemRealloc(0, 12);
	ok &= p != 0;
	if (p)
		ok &= CoTaskMemRealloc(p, 0) == 0;
	CoTaskMemFree(0);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "com ok\n" : "com FAIL\n", ok ? 7 : 9, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
