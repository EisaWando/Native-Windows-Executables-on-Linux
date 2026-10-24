#include <windows.h>
static int failures;
#define CHECK(x) do {if(!(x)) failures++;}while(0)
static DWORD WINAPI worker(void *arg)
{
    HKEY h; DWORD d; (void)arg;
    for(int i=0;i<100;i++) {
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\BridgeConcurrent",0,0,0,KEY_ALL_ACCESS,0,&h,&d))return 1;
        DWORD value=GetCurrentThreadId();
        if(RegSetValueExW(h,L"worker",0,REG_DWORD,(BYTE *)&value,4)||RegCloseKey(h))return 2;
    }
    return 0;
}
void mainCRTStartup(void)
{
    HKEY h,k,alias; DWORD d,type,n,v=0x12345678,sub,vals,maxn,maxd; BYTE buf[256]; char name[128]; WCHAR wide[128];
    SetLastError(0x76543210);
    CHECK(RegOpenKeyExA(HKEY_LOCAL_MACHINE,"software\\MICROSOFT\\Cryptography",0,KEY_READ,&h)==0);
    n=0; CHECK(RegQueryValueExA(h,"MachineGuid",0,&type,0,&n)==0&&type==REG_SZ&&n==37);
    n=2; CHECK(RegQueryValueExA(h,"MachineGuid",0,0,buf,&n)==234&&n==37);
    n=sizeof(buf); CHECK(RegQueryValueExA(h,"MachineGuid",0,0,buf,&n)==0&&buf[36]==0&&buf[8]=='-');
    CHECK(RegSetValueExA(h,"no",0,REG_DWORD,(BYTE *)&v,4)==5);
    CHECK(RegCloseKey(h)==0&&RegCloseKey(h)==6);
    CHECK(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",0,KEY_READ,&h)==0);
    n=4;d=0;CHECK(RegQueryValueExW(h,L"CurrentMajorVersionNumber",0,&type,(BYTE *)&d,&n)==0&&d==10&&type==REG_DWORD);
    n=sizeof(buf);CHECK(RegQueryValueExA(h,"CurrentBuild",0,0,buf,&n)==0&&n==6&&buf[0]=='1'&&buf[4]=='5');RegCloseKey(h);
    CHECK(RegOpenKeyExA(HKEY_LOCAL_MACHINE,"missing",0,KEY_READ,&h)==2);
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\BridgeRegistry",0,0,0,KEY_ALL_ACCESS,0,&h,&d)==0&&d==1);
    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER,"software\\bridgeregistry",0,0,0,KEY_ALL_ACCESS,0,&k,&d)==0&&d==2);RegCloseKey(k);
    CHECK(RegOpenKeyExW(h,0,0,KEY_READ,&alias)==0&&alias!=h);RegCloseKey(alias);
    CHECK(RegSetValueExA(h,"caf\xc3\xa9",0,REG_SZ,(BYTE *)"h\xc3\xa9",3)==0);
    n=sizeof(wide);CHECK(RegQueryValueExW(h,L"CAF\x00c9",0,&type,(BYTE *)wide,&n)==0&&n==6&&wide[1]==0xe9&&wide[2]==0);
    CHECK(RegSetValueExW(h,0,0,REG_DWORD,(BYTE *)&v,4)==0);
    CHECK(RegSetValueExW(h,L"multi",0,REG_MULTI_SZ,(BYTE *)L"one\0two\0",18)==0);
    n=sizeof(buf); CHECK(RegQueryValueExA(h,"multi",0,0,buf,&n)==0&&n==9&&buf[8]==0);
    CHECK(RegSetValueExW(h,L"binary",0,REG_BINARY,(BYTE *)&v,4)==0);
    ULONGLONG q=0x123456789abcdef0ULL;CHECK(RegSetValueExW(h,L"qword",0,REG_QWORD,(BYTE *)&q,8)==0);
    CHECK(RegSetValueExA(h,"expand",0,REG_EXPAND_SZ,(BYTE *)"%UNKNOWN%",10)==0);
    n=sizeof(buf);CHECK(RegGetValueA(h,0,"expand",RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,&type,buf,&n)==0&&type==REG_SZ);
    n=sizeof(buf);CHECK(RegGetValueA(h,0,"qword",RRF_RT_REG_DWORD|RRF_ZEROONFAILURE,0,buf,&n)==1630&&buf[0]==0);
    CHECK(RegQueryInfoKeyA(h,0,0,0,&sub,0,0,&vals,&maxn,&maxd,0,0)==0&&sub==0&&vals==6&&maxd>=10);
    for(DWORD i=0;i<vals;i++){n=128;DWORD z=sizeof(buf);CHECK(RegEnumValueA(h,i,name,&n,0,&type,buf,&z)==0);}
    n=128;CHECK(RegEnumValueW(h,vals,wide,&n,0,0,0,0)==259);
    n=1;CHECK(RegEnumValueA(h,0,name,&n,0,0,0,0)==234);
    CHECK(RegCreateKeyExA(h,"child\\leaf",0,0,0,KEY_ALL_ACCESS,0,&k,&d)==0&&d==1);RegCloseKey(k);
    n=128;CHECK(RegEnumKeyExA(h,0,name,&n,0,0,0,0)==0&&n==5);
    CHECK(RegDeleteKeyA(h,"child")==5);
    CHECK(RegOpenKeyExA(h,"child\\leaf",0,KEY_READ,&k)==0);
    CHECK(RegDeleteKeyW(h,L"child\\leaf")==0&&RegQueryInfoKeyW(k,0,0,0,0,0,0,0,0,0,0,0)==6&&RegCloseKey(k)==0);
    CHECK(RegDeleteKeyA(h,"child")==0);
    CHECK(RegDeleteValueW(h,L"binary")==0&&RegDeleteValueA(h,"binary")==2);
    CHECK(RegFlushKey(h)==0&&RegFlushKey((HKEY)0x1234)==6);
    CHECK(RegCloseKey(h)==0&&RegDeleteKeyA(HKEY_CURRENT_USER,"Software\\BridgeRegistry")==0);
    CHECK(RegCloseKey(HKEY_CLASSES_ROOT)==0&&RegCloseKey(HKEY_USERS)==0&&RegCloseKey(HKEY_CURRENT_CONFIG)==0);
    CHECK(GetLastError()==0x76543210);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER,0,0,KEY_READ,&h)==0);
    CHECK(WaitForSingleObject(h,0)==WAIT_FAILED&&GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(RegCloseKey(h)==0);
    HANDLE threads[4];for(int i=0;i<4;i++)threads[i]=CreateThread(0,0,worker,0,0,0);
    CHECK(WaitForMultipleObjects(4,threads,TRUE,10000)==0);
    for(int i=0;i<4;i++){GetExitCodeThread(threads[i],&d);CHECK(d==0);CloseHandle(threads[i]);}
    CHECK(RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\BridgeConcurrent")==0);
    DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),failures?"registry FAIL\n":"registry ok\n",failures?14:12,&written,0);ExitProcess(failures?1:0);
}
