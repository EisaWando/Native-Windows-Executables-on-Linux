#define UNICODE
#include <windows.h>
static void fail(unsigned line)
{
    char b[64]="FAIL locale line "; unsigned n=16; char r[12]; unsigned i=0;
    do { r[i++]='0'+line%10; line/=10; } while(line);
    while(i) b[n++]=r[--i]; b[n++]='\n'; DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),b,n,&written,0); ExitProcess(1);
}
#define C(x) do { if(!(x)) fail(__LINE__); } while(0)
void mainCRTStartup(void)
{
    WCHAR name[16], small[4]={1,2,3,4};
    /* The installed profile is English (United States). */
    C(GetUserDefaultLCID()==0x409 && GetSystemDefaultLCID()==0x409);
    C(GetUserDefaultLangID()==0x409 && GetSystemDefaultLangID()==0x409);
    C(GetUserDefaultUILanguage()==0x409 && GetSystemDefaultUILanguage()==0x409);
    C(GetThreadUILanguage()==0x409 && GetThreadLocale()==0x409);
    C(SetThreadLocale(0x409) && GetThreadLocale()==0x409);
    C(!SetThreadLocale(0x411) && GetLastError()==87);          /* ja-JP not installed */

    C(GetUserDefaultLocaleName(name,16)==6 && !lstrcmpW(name,L"en-US"));
    C(GetSystemDefaultLocaleName(name,16)==6 && !lstrcmpW(name,L"en-US"));
    C(!GetUserDefaultLocaleName(small,4) && GetLastError()==122);
    C(!GetUserDefaultLocaleName(0,16) && GetLastError()==87);
    C(!GetUserDefaultLocaleName(name,0) && GetLastError()==87);

    C(LCIDToLocaleName(0x409,name,16,0)==6 && !lstrcmpW(name,L"en-US"));
    C(LCIDToLocaleName(0x400,name,16,0)==6 && !lstrcmpW(name,L"en-US"));
    C(LCIDToLocaleName(0x800,name,16,0)==6 && !lstrcmpW(name,L"en-US"));
    C(!LCIDToLocaleName(0x411,name,16,0) && GetLastError()==87);
    C(!LCIDToLocaleName(0x409,small,4,0) && GetLastError()==122);
    C(!LCIDToLocaleName(0x409,name,16,1) && GetLastError()==87);

    C(LocaleNameToLCID(L"en-US",0)==0x409);
    C(LocaleNameToLCID(L"EN-us",0)==0x409);
    C(LocaleNameToLCID(L"en_US",0)==0x409);
    C(LocaleNameToLCID(L"en",0)==0x409);
    C(!LocaleNameToLCID(L"ja-JP",0) && GetLastError()==87);
    C(!LocaleNameToLCID(0,0) && GetLastError()==87);

    C(IsValidLocale(0x409,LCID_INSTALLED));
    C(IsValidLocale(0x409,LCID_SUPPORTED));
    C(!IsValidLocale(0x411,LCID_INSTALLED) && GetLastError()==87);
    C(!IsValidLocale(0x409,8) && GetLastError()==87);

    C(IsValidLocaleName(L"en-US") && IsValidLocaleName(L"en") && IsValidLocaleName(L"en_us"));
    C(!IsValidLocaleName(L"ja-JP") && GetLastError()==87);
    C(!IsValidLocaleName(0) && GetLastError()==87);
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"locale ok\n",10,&written,0);ExitProcess(0);
}
