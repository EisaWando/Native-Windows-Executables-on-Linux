#include <windows.h>
static HANDLE event;
static volatile LONG ran;
static DWORD WINAPI child(void *parameter)
{
    DWORD code;
    if (!GetExitCodeThread(GetCurrentThread(), &code) || code != STILL_ACTIVE) return 1;
    InterlockedIncrement(&ran); SetEvent(event);
    return (DWORD)(ULONG_PTR)parameter;
}
void mainCRTStartup(void)
{
    DWORD written, id, code; int ok = 1;
    event = CreateEventW(0, FALSE, FALSE, 0);
    HANDLE thread = CreateThread(0, 0, child, (void *)42, CREATE_SUSPENDED, &id);
    ok &= thread != 0 && id != GetCurrentThreadId() && ran == 0;
    ok &= WaitForSingleObject(thread, 0) == WAIT_TIMEOUT;
    ok &= ResumeThread(thread) == 1;
    ok &= WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
    ok &= WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
    ok &= GetExitCodeThread(thread, &code) && code == 42 && ran == 1;
    ok &= CloseHandle(thread) && CloseHandle(event);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "thread smoke ok\n" : "thread smoke FAIL\n", ok ? 16 : 18, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
