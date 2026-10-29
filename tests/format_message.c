#include <windows.h>
void mainCRTStartup(void)
{
    WCHAR text[128]; DWORD n;
    n=FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL,ERROR_MOD_NOT_FOUND,0,text,128,NULL);
    if(n!=42 || lstrcmpW(text,L"The specified module could not be found.\r\n")) ExitProcess(1);
    SetLastError(0);
    if(FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL,0xdeadbeef,0,text,128,NULL) || GetLastError()!=ERROR_MR_MID_NOT_FOUND) ExitProcess(2);
    WCHAR *allocated=NULL;
    n=FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS|FORMAT_MESSAGE_ALLOCATE_BUFFER,
                     NULL,ERROR_INVALID_HANDLE,0,(WCHAR *)&allocated,0,NULL);
    if(!n || !allocated || lstrcmpW(allocated,L"The handle is invalid.\r\n") || LocalFree(allocated)) ExitProcess(3);
    ULONG_PTR values[1]={(ULONG_PTR)L"fixture"};
    n=FormatMessageW(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_ARGUMENT_ARRAY,
                     L"value %1%n",0,0,text,128,(va_list *)values);
    if(n!=15 || lstrcmpW(text,L"value fixture\r\n")) ExitProcess(4);
    char ansi[128];
    n=FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL,ERROR_FILE_NOT_FOUND,0,ansi,128,NULL);
    if(!n || lstrcmpA(ansi,"The system cannot find the file specified.\r\n")) ExitProcess(5);
    DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"format message ok\n",18,&written,NULL); ExitProcess(0);
}
