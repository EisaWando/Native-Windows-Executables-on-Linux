#define UNICODE
#include <windows.h>
#include <shlwapi.h>
static int equal(const WCHAR *a, const WCHAR *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
void mainCRTStartup(void)
{
    static WCHAR p[260] = L"Z:\\one\\two.exe", root[] = L"Z:\\", leaf[] = L"name.ext";
    int ok = equal(PathFindFileNameW(p), L"two.exe") && equal(PathFindExtensionW(p), L".exe"); DWORD written;
    ok &= PathRemoveFileSpecW(p) && equal(p, L"Z:\\one");
    ok &= PathAddBackslashW(p) && equal(p, L"Z:\\one\\");
    ok &= PathRemoveBackslashW(p) && equal(p, L"Z:\\one");
    ok &= PathRemoveFileSpecW(p) && equal(p, root) && !PathRemoveFileSpecW(p);
    ok &= PathIsRootW(root) && PathRemoveBackslashW(root) == root+3;
    ok &= PathIsRelativeW(leaf) && !PathIsRelativeW(p) && PathGetDriveNumberW(p) == 25;
    ok &= PathIsUNCW(L"\\\\host\\share") && PathIsRootW(L"\\\\host\\share");
    PathRemoveExtensionW(leaf); ok &= equal(leaf, L"name");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "paths ok\n" : "paths FAIL\n", ok ? 9 : 11, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
