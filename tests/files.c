#define UNICODE
#include <windows.h>
void mainCRTStartup(void)
{
    static WCHAR path[256]; const WCHAR *prefix = L"Z:\\tmp\\binfmt-pe-files-";
    int n = 0, k = 0, ok = 1; WCHAR digits[16]; DWORD pid = GetCurrentProcessId(), written, count;
    while (prefix[n]) { path[n] = prefix[n]; n++; }
    do { digits[k++] = '0'+pid%10; pid /= 10; } while (pid);
    while (k) path[n++] = digits[--k]; path[n] = 0;
    static const char text[] = "threaded file IO\n"; char buffer[64];
    HANDLE h = CreateFileW(path, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    ok &= h != INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER position, size; position.QuadPart = 0;
        ok &= GetFileType(h) == FILE_TYPE_DISK;
        ok &= WriteFile(h, text, sizeof(text)-1, &written, 0) && written == sizeof(text)-1;
        ok &= GetFileSizeEx(h, &size) && size.QuadPart == sizeof(text)-1;
        ok &= SetFilePointerEx(h, position, &position, FILE_BEGIN) && position.QuadPart == 0;
        ok &= ReadFile(h, buffer, sizeof(buffer), &count, 0) && count == sizeof(text)-1;
        for (unsigned i = 0; i < count; i++) ok &= buffer[i] == text[i];
        WIN32_FILE_ATTRIBUTE_DATA data;
        ok &= GetFileAttributesExW(path, GetFileExInfoStandard, &data) && data.nFileSizeLow == sizeof(text)-1 && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        ok &= CloseHandle(h);
        ok &= !ReadFile(h, buffer, 1, &count, 0) && GetLastError() == ERROR_INVALID_HANDLE;
        ok &= DeleteFileW(path);
        ok &= GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    ok &= (GetFileAttributesW(L"Z:\\tmp") & FILE_ATTRIBUTE_DIRECTORY) != 0;
    ok &= GetFileAttributesW(L"C:\\no-such-file") == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_INVALID_DRIVE;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "files ok\n" : "files FAIL\n", ok ? 9 : 11, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
