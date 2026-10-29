#include <windows.h>
void mainCRTStartup(void)
{
    HKL layout=GetKeyboardLayout(0); WCHAR name[KL_NAMELENGTH]; char ansi[KL_NAMELENGTH];
    if((ULONG_PTR)layout!=0x04090409 || !GetKeyboardLayoutNameW(name) || lstrcmpW(name,L"00000409")) ExitProcess(1);
    if(!GetKeyboardLayoutNameA(ansi) || lstrcmpA(ansi,"00000409")) ExitProcess(2);
    if(MapVirtualKeyW('A',MAPVK_VK_TO_VSC)!=0x1e || MapVirtualKeyW(0x1e,MAPVK_VSC_TO_VK)!='A' ||
       MapVirtualKeyW('A',MAPVK_VK_TO_CHAR)!='A' || MapVirtualKeyExW('Z',MAPVK_VK_TO_VSC,layout)!=0x2c) ExitProcess(3);
    if(MapVirtualKeyW(0xffff,MAPVK_VK_TO_VSC) || GetKeyboardLayoutNameW(NULL)) ExitProcess(4);
    DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"keyboard layout ok\n",19,&n,NULL);ExitProcess(0);
}
