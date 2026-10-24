#define UNICODE
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
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
    char a[16]; WCHAR w[16];
    ok &= lstrcpynA(a, "abcd", 3) == a && !lstrcmpA(a, "ab");
    ok &= lstrcpyA(a, "Ab") == a && lstrcatA(a, "Cd") == a && !lstrcmpiA(a, "aBcD");
    ok &= lstrcmpA("a", "b") < 0 && lstrcmpiA("A", "b") < 0;
    ok &= lstrcpynW(w, L"abcd", 3) == w && !lstrcmpW(w, L"ab");
    ok &= lstrcpyW(w, L"Ab") == w && lstrcatW(w, L"Cd") == w && !lstrcmpiW(w, L"aBcD");
    static WCHAR folder[MAX_PATH], temp[MAX_PATH];
    ok &= SHGetFolderPathW(0, CSIDL_PROGRAM_FILES, 0, 0, folder) == S_OK && !lstrcmpW(folder, L"Z:\\opt");
    ok &= GetSystemDirectoryW(folder, MAX_PATH) == 10 && !lstrcmpW(folder, L"Z:\\usr\\lib");
    DWORD temp_length = GetTempPathW(MAX_PATH, temp);
    ok &= temp_length > 3 && temp[0] == 'Z' && temp[temp_length-1] == '\\';
    PathRemoveExtensionW(leaf); ok &= equal(leaf, L"name");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "paths ok\n" : "paths FAIL\n", ok ? 9 : 11, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
