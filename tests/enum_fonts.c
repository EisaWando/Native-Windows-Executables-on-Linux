#define UNICODE
#include <windows.h>
static int errors, count, nested;
static WCHAR chosen[32];
static BYTE charset;
static HDC dc;
static DWORD WINAPI allocate(void *p)
{
	(void)p;
	void *v = HeapAlloc(GetProcessHeap(), 0, 37);
	if (!v)
		return 1;
	HeapFree(GetProcessHeap(), 0, v);
	return 0;
}
static int CALLBACK stop(const LOGFONTW *f, const TEXTMETRICW *m, DWORD type,
			 LPARAM param)
{
	(void)f;
	(void)m;
	(void)type;
	(*(int *)param)++;
	return 0;
}
static int CALLBACK enumerate(const LOGFONTW *lf, const TEXTMETRICW *tm,
			      DWORD type, LPARAM parameter)
{
	const ENUMLOGFONTEXW *f = (const void *)lf;
	const NEWTEXTMETRICEXW *m = (const void *)tm;
	LOGFONTW *filter = (void *)parameter;
	count++;
	if (!f->elfLogFont.lfFaceName[0] || m->ntmTm.tmHeight <= 0 ||
	    m->ntmTm.tmHeight != m->ntmTm.tmAscent + m->ntmTm.tmDescent ||
	    !(type & (RASTER_FONTTYPE | TRUETYPE_FONTTYPE)))
		errors++;
	if (filter->lfCharSet != DEFAULT_CHARSET &&
	    filter->lfCharSet != lf->lfCharSet)
		errors++;
	if (filter->lfFaceName[0] &&
	    lstrcmpiW(filter->lfFaceName, lf->lfFaceName))
		errors++;
	if (!chosen[0]) {
		for (int i = 0; i < 32; i++)
			chosen[i] = lf->lfFaceName[i];
		charset = lf->lfCharSet;
	}
	if (!nested) {
		nested = 1;
		LOGFONTW local = { 0 };
		local.lfCharSet = DEFAULT_CHARSET;
		int calls = 0;
		if (EnumFontFamiliesExW(dc, &local, stop, (LPARAM)&calls, 0) !=
			    0 ||
		    calls != 1)
			errors++;
		HANDLE t = CreateThread(0, 0, allocate, 0, 0, 0);
		DWORD code = 1;
		if (!t || WaitForSingleObject(t, 3000) != WAIT_OBJECT_0 ||
		    !GetExitCodeThread(t, &code) || code)
			errors++;
		if (t)
			CloseHandle(t);
	}
	return 7;
}
void mainCRTStartup(void)
{
	int ok = 1;
	DWORD written;
	dc = CreateCompatibleDC(0);
	LOGFONTW filter = { 0 };
	filter.lfCharSet = DEFAULT_CHARSET;
	ok &= EnumFontFamiliesExW(dc, &filter, enumerate, (LPARAM)&filter, 0) ==
		      7 &&
	      count > 0 && chosen[0] && !errors;
	for (int i = 0; i < 32; i++)
		filter.lfFaceName[i] = chosen[i];
	filter.lfCharSet = charset;
	count = 0;
	ok &= EnumFontFamiliesExW(dc, &filter, enumerate, (LPARAM)&filter, 0) ==
		      7 &&
	      count > 0 && !errors;
	filter.lfFaceName[0] = 0;
	filter.lfCharSet = 254;
	count = 0;
	ok &= EnumFontFamiliesExW(dc, &filter, enumerate, (LPARAM)&filter, 0) ==
		      0 &&
	      count == 0;
	filter.lfCharSet = DEFAULT_CHARSET;
	lstrcpyW(filter.lfFaceName, L"__binfmt_no_such_family__");
	count = 0;
	ok &= EnumFontFamiliesExW(dc, &filter, enumerate, (LPARAM)&filter, 0) ==
		      0 &&
	      !count;
	ok &= !EnumFontFamiliesExW(dc, 0, enumerate, 0, 0) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= !EnumFontFamiliesExW((HDC)4, &filter, enumerate, 0, 0) &&
	      GetLastError() == ERROR_INVALID_HANDLE;
	ok &= !EnumFontFamiliesExW(dc, &filter, 0, 0, 0) &&
	      GetLastError() == ERROR_INVALID_PARAMETER;
	ok &= DeleteDC(dc);
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		  ok ? "enum fonts ok\n" : "enum fonts FAIL\n", ok ? 14 : 16,
		  &written, 0);
	ExitProcess(ok ? 0 : 1);
}
