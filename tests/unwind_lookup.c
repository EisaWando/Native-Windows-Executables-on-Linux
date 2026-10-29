#include <windows.h>
static RUNTIME_FUNCTION entries[2]={{0x100,0x120,0},{0x200,0x240,0}};
void mainCRTStartup(void)
{
    DWORD64 base=0;
    PRUNTIME_FUNCTION self=RtlLookupFunctionEntry((DWORD64)(ULONG_PTR)mainCRTStartup,&base,NULL);
    if(!self || !base || (DWORD64)(ULONG_PTR)mainCRTStartup<base+self->BeginAddress ||
       (DWORD64)(ULONG_PTR)mainCRTStartup>=base+self->EndAddress) ExitProcess(1);
    if(!RtlAddFunctionTable(entries,2,0x400000000ULL)) ExitProcess(2);
    if(RtlLookupFunctionEntry(0x400000201ULL,&base,NULL)!=&entries[1] || base!=0x400000000ULL) ExitProcess(3);
    if(RtlLookupFunctionEntry(0x400000120ULL,&base,NULL)) ExitProcess(4);
    if(!RtlDeleteFunctionTable(entries) || RtlLookupFunctionEntry(0x400000201ULL,&base,NULL)) ExitProcess(5);
    DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"unwind lookup ok\n",17,&written,NULL); ExitProcess(0);
}
