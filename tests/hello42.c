typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;

__declspec(dllimport) HANDLE GetStdHandle(DWORD n);
__declspec(dllimport) BOOL WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *written, void *ov);
__declspec(dllimport) void ExitProcess(unsigned int code);

static const char msg[] = "Hello from a native PE on Linux!\n";

void mainCRTStartup(void)
{
    DWORD written;
    WriteFile(GetStdHandle((DWORD)-11), msg, sizeof(msg) - 1, &written, 0);
    ExitProcess(42);
}
