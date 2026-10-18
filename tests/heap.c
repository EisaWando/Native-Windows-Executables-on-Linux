typedef void *HANDLE; typedef unsigned long DWORD; typedef unsigned long long SIZE_T; typedef int BOOL;
__declspec(dllimport) HANDLE GetProcessHeap(void);
__declspec(dllimport) void *HeapAlloc(HANDLE, DWORD, SIZE_T);
__declspec(dllimport) void *HeapReAlloc(HANDLE, DWORD, void *, SIZE_T);
__declspec(dllimport) BOOL HeapFree(HANDLE, DWORD, void *);
__declspec(dllimport) HANDLE GetStdHandle(DWORD);
__declspec(dllimport) BOOL WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) void ExitProcess(unsigned);

static int check(unsigned char *p, SIZE_T n, unsigned char v)
{ SIZE_T i; for (i = 0; i < n; i++) if (p[i] != v) return 0; return 1; }
static void fill(unsigned char *p, SIZE_T n, unsigned char v)
{ SIZE_T i; for (i = 0; i < n; i++) p[i] = v; }

void mainCRTStartup(void)
{
	HANDLE h = GetProcessHeap();
	unsigned char *ptrs[64];
	DWORD w; int ok = 1, i; SIZE_T n; unsigned char *q;
	for (i = 0; i < 64 && ok; i++) {
		n = 16 + i * 37;
		ptrs[i] = HeapAlloc(h, 8, n);
		if (!ptrs[i] || !check(ptrs[i], n, 0)) { ok = 0; break; }
		fill(ptrs[i], n, (unsigned char)(i + 1));
	}
	for (i = 0; i < 64 && ok; i += 2) {
		n = 16 + i * 37;
		if (!check(ptrs[i], n, (unsigned char)(i + 1))) ok = 0;
		HeapFree(h, 0, ptrs[i]);
	}
	for (i = 1; i < 64 && ok; i += 2) {
		n = 16 + i * 37;
		q = HeapReAlloc(h, 8, ptrs[i], n * 4);
		if (!q || !check(q, n, (unsigned char)(i + 1)) || !check(q + n, n * 3, 0)) ok = 0;
		else HeapFree(h, 0, q);
	}
	q = HeapAlloc(h, 8, 300000);
	if (!q || !check(q, 300000, 0)) ok = 0; else { fill(q, 300000, 7); HeapFree(h, 0, q); }
	WriteFile(GetStdHandle((DWORD)-11), ok ? "heap ok\n" : "heap FAIL\n", ok ? 8 : 10, &w, 0);
	ExitProcess(ok ? 0 : 1);
}
