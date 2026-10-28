#include <windows.h>
#include <stdint.h>
#include <psapi.h>
void mainCRTStartup(void) {
 HANDLE m=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,65536,L"pe-test-section");
 if(!m) ExitProcess(1); HANDLE again=OpenFileMappingW(FILE_MAP_READ,FALSE,L"pe-test-section"); if(!again) ExitProcess(2);
 BYTE *a=MapViewOfFile(m,FILE_MAP_WRITE,0,0,0), *b=MapViewOfFile(again,FILE_MAP_READ,0,0,0);
 if(!a || !b) ExitProcess(3); a[0]=0x65; if(b[0]!=0x65) ExitProcess(4);
 MEMORY_BASIC_INFORMATION mbi; if(!VirtualQuery(b,&mbi,sizeof(mbi)) || mbi.Type!=MEM_MAPPED) ExitProcess(11);
 BYTE *copy=MapViewOfFile(m,FILE_MAP_COPY,0,0,0); if(!copy) ExitProcess(12);
 copy[0]=0x22; if(a[0]!=0x65 || !UnmapViewOfFile(copy)) ExitProcess(13);
 SetLastError(0); if(MapViewOfFile(m,FILE_MAP_READ,0,1,16) || GetLastError()!=ERROR_MAPPED_ALIGNMENT) ExitProcess(14);
 if(!CloseHandle(m) || !CloseHandle(again) || !UnmapViewOfFile(a) || !UnmapViewOfFile(b)) ExitProcess(5);
 if(UnmapViewOfFile(b)) ExitProcess(6);
 HANDLE temp=CreateFileW(L"Z:\\tmp\\pe-file-mapping-test.bin",GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
 if(temp==INVALID_HANDLE_VALUE) ExitProcess(17); DWORD written; char initial[4]={'a','b','c','d'};
 if(!WriteFile(temp,initial,4,&written,NULL) || written!=4) ExitProcess(18);
 HANDLE disk=CreateFileMappingW(temp,NULL,PAGE_READWRITE,0,0,NULL); if(!disk) ExitProcess(19);
 BYTE *fileview=MapViewOfFile(disk,FILE_MAP_WRITE,0,0,0); if(!fileview || fileview[0]!='a') ExitProcess(20);
 fileview[1]='Z'; if(!FlushViewOfFile(fileview,0) || !SetFilePointerEx(temp,(LARGE_INTEGER){.QuadPart=0},NULL,FILE_BEGIN)) ExitProcess(21);
 char result[4]; DWORD read; if(!ReadFile(temp,result,4,&read,NULL) || read!=4 || result[1]!='Z') ExitProcess(22);
 if(CreateFileMappingW(temp,NULL,PAGE_READONLY|SEC_IMAGE,0,0,NULL) || GetLastError()!=ERROR_BAD_EXE_FORMAT) ExitProcess(23);
 if(!UnmapViewOfFile(fileview) || !CloseHandle(disk) || !CloseHandle(temp) || !DeleteFileW(L"Z:\\tmp\\pe-file-mapping-test.bin")) ExitProcess(24);
 HANDLE f=CreateFileW(L"tests/file_mapping.exe",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(f==INVALID_HANDLE_VALUE) ExitProcess(7);
 HANDLE image=CreateFileMappingW(f,NULL,PAGE_READONLY|SEC_IMAGE,0,0,NULL);
 if(!image) ExitProcess(8); BYTE *p=MapViewOfFile(image,FILE_MAP_READ,0,0,0);
 if(!p || *(WORD*)p!=0x5a4d || *(DWORD*)(p+*(DWORD*)(p+0x3c))!=0x4550) ExitProcess(9);
 if(!VirtualQuery(p,&mbi,sizeof(mbi)) || mbi.Type!=MEM_IMAGE || mbi.Protect!=PAGE_READONLY) ExitProcess(15);
 IMAGE_NT_HEADERS64 *nt=(void*)(p+*(DWORD*)(p+0x3c)); IMAGE_SECTION_HEADER *sec=IMAGE_FIRST_SECTION(nt);
 if(!VirtualQuery(p+sec->VirtualAddress,&mbi,sizeof(mbi)) || mbi.Type!=MEM_IMAGE || !(mbi.Protect & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))) ExitProcess(25);
 static WCHAR path[4096]; if(!GetMappedFileNameW(GetCurrentProcess(),p,path,4096)) ExitProcess(16);
 if(!CloseHandle(f) || !CloseHandle(image) || !UnmapViewOfFile(p)) ExitProcess(10);
 WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"file mapping ok\n",16,&written,NULL);ExitProcess(0);
}
