#include <windows.h>
#include <winver.h>
void mainCRTStartup(void) {
 DWORD handle=123, n=GetFileVersionInfoSizeW(L"tests/version.exe",&handle);
 if(!n || handle || n>65536) ExitProcess(1);
 BYTE *p=HeapAlloc(GetProcessHeap(),0,n); if(!p) ExitProcess(2);
 SetLastError(0); if(GetFileVersionInfoW(L"tests/version.exe",0,n-1,p) || GetLastError()!=122) ExitProcess(3);
 if(!GetFileVersionInfoW(L"tests/version.exe",0,n,p) || *(WORD*)p!=n) ExitProcess(4);
 if(GetFileVersionInfoSizeA("tests/version.exe",&handle)!=n || !GetFileVersionInfoA("tests/version.exe",0,n,p)) ExitProcess(5);
 VS_FIXEDFILEINFO *fixed=NULL; UINT len=0;
 if(!VerQueryValueW(p,L"\\",(void**)&fixed,&len) || len!=sizeof(*fixed) || fixed->dwSignature!=0xFEEF04BD || fixed->dwFileVersionMS!=0x00010002) ExitProcess(7);
 WCHAR *name=NULL; if(!VerQueryValueW(p,L"\\StringFileInfo\\040904b0\\ProductName",(void**)&name,&len) || len!=16 || lstrcmpW(name,L"Version fixture")) ExitProcess(8);
 void *translation=NULL; if(!VerQueryValueW(p,L"\\VarFileInfo\\Translation",&translation,&len) || len!=4 || *(DWORD*)translation!=0x04b00409) ExitProcess(9);
 if(VerQueryValueW(p,L"\\missing",&translation,&len)) ExitProcess(10);
 SetLastError(0); if(GetFileVersionInfoSizeW(L"tests/no-such-file.exe",NULL) || GetLastError()!=2) ExitProcess(6);
 DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"version ok\n",11,&written,NULL); ExitProcess(0);
}
