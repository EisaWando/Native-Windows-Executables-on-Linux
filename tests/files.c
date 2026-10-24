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
        ok &= FlushFileBuffers(h);
        BY_HANDLE_FILE_INFORMATION info;
        ok &= GetFileInformationByHandle(h, &info) && info.nFileSizeLow == sizeof(text)-1;
        static WCHAR copy_path[256]; static char path_a[512], copy_a[512];
        lstrcpyW(copy_path, path); lstrcatW(copy_path, L"_copy");
        ok &= CopyFileW(path, copy_path, TRUE);
        ok &= !CopyFileW(path, copy_path, TRUE) && GetLastError() == ERROR_FILE_EXISTS;
        ok &= !CopyFileW(path, path, FALSE);
        ok &= WideCharToMultiByte(CP_UTF8, 0, path, -1, path_a, 512, 0, 0) > 0;
        ok &= WideCharToMultiByte(CP_UTF8, 0, copy_path, -1, copy_a, 512, 0, 0) > 0;
        ok &= CopyFileA(path_a, copy_a, FALSE);
        HANDLE copy = CreateFileW(copy_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
        ok &= copy != INVALID_HANDLE_VALUE;
        if (copy != INVALID_HANDLE_VALUE) {
            ok &= ReadFile(copy, buffer, sizeof(buffer), &count, 0) && count == sizeof(text)-1;
            for (unsigned i = 0; i < count; i++) ok &= buffer[i] == text[i];
            ok &= CloseHandle(copy);
        }
        static WIN32_FIND_DATAW data_w; static WIN32_FIND_DATAA data_a;
        static WCHAR pattern[256]; lstrcpyW(pattern, path); lstrcatW(pattern, L"*");
        HANDLE search = FindFirstFileExW(pattern, FindExInfoBasic, &data_w, FindExSearchNameMatch, 0, 0);
        int matches = 0;
        if (search != INVALID_HANDLE_VALUE) {
            do { matches++; ok &= data_w.nFileSizeLow == sizeof(text)-1; } while (FindNextFileW(search, &data_w));
            ok &= matches == 2 && GetLastError() == ERROR_NO_MORE_FILES && FindClose(search);
            ok &= !FindNextFileW(search, &data_w) && GetLastError() == ERROR_INVALID_HANDLE;
        } else ok = 0;
        search = FindFirstFileA(copy_a, &data_a);
        ok &= search != INVALID_HANDLE_VALUE && data_a.nFileSizeLow == sizeof(text)-1;
        if (search != INVALID_HANDLE_VALUE) ok &= !FindNextFileA(search, &data_a) && GetLastError() == ERROR_NO_MORE_FILES && FindClose(search);
        ok &= DeleteFileA(copy_a);
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
