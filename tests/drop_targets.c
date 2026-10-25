#define COBJMACROS
#define UNICODE
#include <windows.h>
#include <ole2.h>
static LONG refs = 1, errors;
static DWORD WINAPI allocator(void *p)
{
	(void)p;
	void *m = HeapAlloc(GetProcessHeap(), 0, 32);
	if (!m)
		return 1;
	HeapFree(GetProcessHeap(), 0, m);
	return 0;
}
static HRESULT STDMETHODCALLTYPE query(IDropTarget *t, REFIID id, void **out)
{
	(void)t;
	(void)id;
	*out = 0;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE add_ref(IDropTarget *t)
{
	(void)t;
	HANDLE h = CreateThread(0, 0, allocator, 0, 0, 0);
	DWORD code;
	if (!h || WaitForSingleObject(h, 2000) != WAIT_OBJECT_0 ||
	    !GetExitCodeThread(h, &code) || code)
		errors++;
	if (h)
		CloseHandle(h);
	return ++refs;
}
static ULONG STDMETHODCALLTYPE release(IDropTarget *t)
{
	(void)t;
	return --refs;
}
static HRESULT STDMETHODCALLTYPE enter(IDropTarget *t, IDataObject *o, DWORD k,
				       POINTL p, DWORD *e)
{
	(void)t;
	(void)o;
	(void)k;
	(void)p;
	(void)e;
	return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE over(IDropTarget *t, DWORD k, POINTL p,
				      DWORD *e)
{
	(void)t;
	(void)k;
	(void)p;
	(void)e;
	return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE leave(IDropTarget *t)
{
	(void)t;
	return E_NOTIMPL;
}
static IDropTargetVtbl table = { query, add_ref, release, enter,
				 over,	leave,	 enter };
static IDropTarget target = { &table };
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	HWND w = CreateWindowExW(0, L"Static", L"hidden", 0, 0, 0, 0, 0, 0, 0,
				 GetModuleHandleW(0), 0);
	ok &= w != 0;
	ok &= RegisterDragDrop(w, &target) == E_OUTOFMEMORY && refs == 1;
	ok &= OleInitialize(0) == S_OK;
	ok &= RegisterDragDrop(w, &target) == S_OK && refs == 2;
	ok &= RegisterDragDrop(w, &target) == DRAGDROP_E_ALREADYREGISTERED &&
	      refs == 2;
	ok &= RevokeDragDrop(w) == S_OK && refs == 1;
	ok &= RevokeDragDrop(w) == DRAGDROP_E_NOTREGISTERED;
	ok &= RegisterDragDrop(w, 0) == E_INVALIDARG;
	ok &= RegisterDragDrop(w, &target) == S_OK && refs == 2;
	ok &= DestroyWindow(w) && refs == 1;
	ok &= RevokeDragDrop(w) == DRAGDROP_E_INVALIDHWND && !errors;
	OleUninitialize();
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "drop targets ok\n" : "drop targets FAIL\n",
		  ok ? 16 : 18, &written, 0);
	ExitProcess(ok ? 0 : 1);
}
