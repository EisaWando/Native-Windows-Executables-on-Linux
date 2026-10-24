#include <windows.h>
int mainCRTStartup(void)
{
    DWORD written;
    static const char text[] = "entry return ok\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, sizeof(text)-1, &written, 0);
    return 37;
}
