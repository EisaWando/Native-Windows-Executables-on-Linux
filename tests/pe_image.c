#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../bridge/pe_image.inc"
void mainCRTStartup(void) {
 HANDLE file=CreateFileW(L"tests/pe_image.exe",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(file==INVALID_HANDLE_VALUE) ExitProcess(1);
 DWORD size=GetFileSize(file,NULL), got=0; BYTE *p=HeapAlloc(GetProcessHeap(),0,size);
 if(!p || !ReadFile(file,p,size,&got,NULL) || got!=size) ExitProcess(2);
 struct pe_image_layout layout;
 if(!pe_image_parse(p,size,&layout) || layout.section_count<1 || !layout.image_size || !layout.header_size) ExitProcess(3);
 if(pe_image_parse(p,64,&layout)) ExitProcess(4);
 BYTE saved=p[0];p[0]=0;if(pe_image_parse(p,size,&layout)) ExitProcess(5);p[0]=saved;
 DWORD nt=*(DWORD*)(p+0x3c); WORD *sections=(WORD*)(p+nt+6); WORD old=*sections;*sections=0xffff;
 if(pe_image_parse(p,size,&layout)) ExitProcess(6); *sections=old;
 CloseHandle(file); DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"pe image ok\n",12,&written,NULL); ExitProcess(0);
}
