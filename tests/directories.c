#include <windows.h>
static int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
void mainCRTStartup(void)
{
    int ok=1;static WCHAR old[4096],wide[4096];static char full[4096];char *part;DWORD n;
    ok&=GetCurrentDirectoryW(4096,old)>0;ok&=SetCurrentDirectoryW(L"Z:\\tmp");
    ok&=GetFullPathNameA("alpha\\..\\beta\\file.txt",4096,full,&part)>0&&same(full,"Z:\\tmp\\beta\\file.txt")&&same(part,"file.txt");
    n=GetFullPathNameA("beta\\file.txt",0,0,0);ok&=n==21;
    ok&=GetFullPathNameW(L"\\tmp\\alpha\\..\\",4096,wide,0)==7;
    ok&=!SetCurrentDirectoryA("C:\\")&&GetLastError()==ERROR_INVALID_DRIVE;
    char name[96]="binfmt-pe-dir-";unsigned pid=GetCurrentProcessId();int used=14;char digits[16];int count=0;do{digits[count++]=(char)('0'+pid%10);pid/=10;}while(pid);while(count)name[used++]=digits[--count];name[used]=0;
    ok&=CreateDirectoryA(name,0);ok&=!CreateDirectoryA(name,0)&&GetLastError()==ERROR_ALREADY_EXISTS;
    ok&=SetCurrentDirectoryA(name)&&CreateDirectoryW(L"child",0);
    ok&=SetCurrentDirectoryA("..");ok&=!RemoveDirectoryA(name)&&GetLastError()==ERROR_DIR_NOT_EMPTY;
    ok&=SetCurrentDirectoryA(name)&&RemoveDirectoryW(L"child")&&SetCurrentDirectoryA("..")&&RemoveDirectoryA(name);
    ok&=SetCurrentDirectoryW(old);
    ok&=MulDiv(1,1,2)==1&&MulDiv(-1,1,2)==-1&&MulDiv(5,1,-2)==-3&&MulDiv(-5,1,-2)==3;
    ok&=MulDiv(0x7fffffff,2,1)==-1&&MulDiv(4,2,0)==-1&&MulDiv(18,96,96)==18;
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),ok?"directories ok\n":"directories FAIL\n",ok?15:17,&written,0);ExitProcess(ok?0:1);
}
