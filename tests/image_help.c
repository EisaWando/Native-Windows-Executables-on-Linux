#include <windows.h>
#include <dbghelp.h>
void mainCRTStartup(void) {
 HANDLE file=CreateFileW(L"tests/image_help.exe",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(file==INVALID_HANDLE_VALUE) ExitProcess(1); DWORD size=GetFileSize(file,NULL), read;
 BYTE *p=HeapAlloc(GetProcessHeap(),0,size); if(!p || !ReadFile(file,p,size,&read,NULL) || read!=size) ExitProcess(2);
 IMAGE_NT_HEADERS *nt=ImageNtHeader(p); if(!nt || nt!=(void*)(p+*(DWORD*)(p+0x3c))) ExitProcess(3);
 IMAGE_SECTION_HEADER *first=IMAGE_FIRST_SECTION(nt),*last=NULL;
 if(ImageRvaToSection(nt,p,first->VirtualAddress)!=first) ExitProcess(4);
 if(ImageRvaToVa(nt,p,first->VirtualAddress,&last)!=p+first->PointerToRawData || last!=first) ExitProcess(5);
 if(ImageNtHeader(NULL) || ImageNtHeader((void*)1) || ImageRvaToSection(nt,p,0xffffffff)) ExitProcess(6);
 CloseHandle(file); DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"image help ok\n",14,&written,NULL); ExitProcess(0);
}
