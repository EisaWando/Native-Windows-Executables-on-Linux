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
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    ok &= (ULONG_PTR)h == 0x44 && GetFileType(h) == FILE_TYPE_DISK;
    ok &= !WriteFile((HANDLE)1, "x", 1, &written, 0) && GetLastError() == 6;
    WriteFile(h, ok ? "utf ok\n" : "utf FAIL\n", ok ? 7 : 9, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
