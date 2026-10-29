#include <windows.h>
#include <shellapi.h>
void mainCRTStartup(void)
{
    WCHAR *line=GetCommandLineW(); char *ansi=GetCommandLineA(); int argc=0;
    if(!line || !ansi || line!=GetCommandLineW() || ansi!=GetCommandLineA()) ExitProcess(1);
    WCHAR **argv=CommandLineToArgvW(line,&argc);
    if(!argv || argc!=6 || lstrcmpW(argv[1],L"-noPlugin") || lstrcmpW(argv[2],L"two words") ||
       lstrcmpW(argv[3],L"a\"b") || lstrcmpW(argv[4],L"back\\slash\\") || lstrcmpW(argv[5],L"")) ExitProcess(2);
    static WCHAR image[4096]; DWORD n=GetModuleFileNameW(NULL,image,4096);
    if(!n || lstrcmpW(argv[0],image) || LocalFree(argv)) ExitProcess(3);
    DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"cmdline ok\n",11,&written,NULL);ExitProcess(0);
}
