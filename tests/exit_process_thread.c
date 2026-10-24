#include <windows.h>
static DWORD WINAPI child(void *p)
{
    (void)p; DWORD written;
    static const char text[] = "exit group ok\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, sizeof(text)-1, &written, 0);
    ExitProcess(23);
}
void mainCRTStartup(void)
{
    HANDLE h = CreateThread(0, 0, child, 0, 0, 0);
    if (!h) ExitProcess(1);
    WaitForSingleObject(h, INFINITE); ExitProcess(2);
}
