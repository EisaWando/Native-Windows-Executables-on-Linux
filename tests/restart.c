#include <windows.h>
void mainCRTStartup(void)
{
    DWORD size=0,flags=0;WCHAR output[16];
    if(GetApplicationRestartSettings(GetCurrentProcess(),NULL,&size,NULL)!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))ExitProcess(1);
    if(RegisterApplicationRestart(L"-recover",RESTART_NO_REBOOT)!=S_OK)ExitProcess(2);
    if(GetApplicationRestartSettings(GetCurrentProcess(),NULL,&size,&flags)!=S_OK || size!=9 || flags!=RESTART_NO_REBOOT)ExitProcess(3);
    size=2;if(GetApplicationRestartSettings(GetCurrentProcess(),output,&size,NULL)!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) || size!=9)ExitProcess(4);
    size=16;if(GetApplicationRestartSettings(GetCurrentProcess(),output,&size,&flags)!=S_OK || lstrcmpW(output,L"-recover"))ExitProcess(5);
    if(UnregisterApplicationRestart()!=S_OK)ExitProcess(6);
    size=0;if(GetApplicationRestartSettings(GetCurrentProcess(),NULL,&size,NULL)!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))ExitProcess(7);
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"restart ok\n",11,&written,NULL);ExitProcess(0);
}
