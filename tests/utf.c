#include <windows.h>
void mainCRTStartup(void)
{
    static const char utf[] = "A\xc3\xa9\xf0\x9f\x98\x80";
    WCHAR w[8]; char bytes[16]; DWORD written; int ok = 1;
    ok &= MultiByteToWideChar(CP_UTF8, 8, utf, -1, 0, 0) == 5;
    ok &= MultiByteToWideChar(CP_UTF8, 8, utf, -1, w, 8) == 5;
    ok &= w[0] == 'A' && w[1] == 0xe9 && w[2] == 0xd83d && w[3] == 0xde00 && !w[4];
    ok &= WideCharToMultiByte(CP_UTF8, 0x80, w, -1, 0, 0, 0, 0) == 8;
    ok &= WideCharToMultiByte(CP_UTF8, 0x80, w, -1, bytes, 16, 0, 0) == 8;
    for (int i = 0; i < 8; i++) ok &= bytes[i] == utf[i];
    ok &= !MultiByteToWideChar(CP_UTF8, 8, utf, -1, w, 2) && GetLastError() == 122;
    ok &= !MultiByteToWideChar(CP_UTF8, 8, "\xc0", 1, w, 8) && GetLastError() == 1113;
    ok &= MultiByteToWideChar(CP_UTF8, 0, "\xc0", 1, w, 8) == 1 && w[0] == 0xfffd;
    w[0] = 0xd800;
    ok &= !WideCharToMultiByte(CP_UTF8, 0x80, w, 1, bytes, 16, 0, 0) && GetLastError() == 1113;
    static WCHAR path[4096], cwd[4096], tiny[1]; static char path_a[4096];
    DWORD plen = GetModuleFileNameW(0, path, 4096);
    ok &= plen > 3 && path[0] == 'Z' && path[1] == ':' && path[2] == '\\' && !path[plen];
    ok &= GetModuleFileNameA(0, path_a, 4096) > 3 && path_a[0] == 'Z' && path_a[2] == '\\';
    ok &= GetModuleFileNameW(0, tiny, 1) == 1 && !tiny[0] && GetLastError() == 122;
    DWORD need = GetCurrentDirectoryW(0, 0);
    ok &= need > 3 && GetCurrentDirectoryW(4096, cwd) == need-1 && cwd[0] == 'Z';
    ok &= !GetEnvironmentVariableW(L"MISSING", cwd, 4096) && GetLastError() == 203;
    ok &= ExpandEnvironmentStringsW(L"%MISSING%", cwd, 4096) == 10 && cwd[0] == '%';
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    ok &= (ULONG_PTR)h == 0x44 && GetFileType(h) == FILE_TYPE_DISK;
    ok &= !WriteFile((HANDLE)1, "x", 1, &written, 0) && GetLastError() == 6;
    WriteFile(h, ok ? "utf ok\n" : "utf FAIL\n", ok ? 7 : 9, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
