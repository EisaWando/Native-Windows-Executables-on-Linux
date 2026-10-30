#include <windows.h>
#include <shlwapi.h>
void mainCRTStartup(void)
{
    if(!PathMatchSpecW(L"estonian.xml",L"*.xml") || !PathMatchSpecW(L"README",L"*.*"))ExitProcess(1);
    if(!PathMatchSpecA("File.TXT","*.xml;  *.txt") || PathMatchSpecA("name.xml","*.txt"))ExitProcess(2);
    if(!PathMatchSpecW(L"abc.xml",L"a?c.*") || PathMatchSpecW(L"ac.xml",L"a?c.*"))ExitProcess(3);
    if(PathMatchSpecExW(L"x.txt",L"*.xml; *.txt",PMSF_MULTIPLE)!=S_OK)ExitProcess(4);
    if(PathMatchSpecExW(L"x.txt",L"*.xml; *.txt",PMSF_MULTIPLE|PMSF_DONT_STRIP_SPACES)!=S_FALSE)ExitProcess(5);
    if(PathMatchSpecExA("x.txt","*.xml",PMSF_NORMAL)!=S_FALSE || PathMatchSpecExA(NULL,"*",0)!=E_INVALIDARG)ExitProcess(6);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"path match ok\n",14,&n,NULL);ExitProcess(0);
}
