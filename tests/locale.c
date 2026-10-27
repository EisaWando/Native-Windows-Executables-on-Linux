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

    /* GetLocaleInfoW: strings of the en-US profile. */
    WCHAR buf[64];
    C(GetLocaleInfoW(0x409,LOCALE_SISO639LANGNAME,buf,64)==3 && !lstrcmpW(buf,L"en"));
    C(GetLocaleInfoW(0x409,LOCALE_SISO3166CTRYNAME,buf,64)==3 && !lstrcmpW(buf,L"US"));
    C(GetLocaleInfoW(0x409,LOCALE_SNAME,buf,64)==6 && !lstrcmpW(buf,L"en-US"));
    C(GetLocaleInfoW(0x409,LOCALE_SDECIMAL,buf,64)==2 && !lstrcmpW(buf,L"."));
    C(GetLocaleInfoW(0x409,LOCALE_STHOUSAND,buf,64)==2 && !lstrcmpW(buf,L","));
    C(GetLocaleInfoW(0x409,LOCALE_SGROUPING,buf,64)==4 && !lstrcmpW(buf,L"3;0"));
    C(GetLocaleInfoW(0x409,LOCALE_SCURRENCY,buf,64)==2 && !lstrcmpW(buf,L"$"));
    C(GetLocaleInfoW(0x409,LOCALE_SINTLSYMBOL,buf,64)==4 && !lstrcmpW(buf,L"USD"));
    C(GetLocaleInfoW(0x409,LOCALE_SDATE,buf,64)==2 && !lstrcmpW(buf,L"/"));
    C(GetLocaleInfoW(0x409,LOCALE_STIME,buf,64)==2 && !lstrcmpW(buf,L":"));
    C(GetLocaleInfoW(0x409,LOCALE_SSHORTDATE,buf,64)==9 && !lstrcmpW(buf,L"M/d/yyyy"));
    C(GetLocaleInfoW(0x409,LOCALE_SLONGDATE,buf,64)==19 && !lstrcmpW(buf,L"dddd, MMMM d, yyyy"));
    C(GetLocaleInfoW(0x409,LOCALE_STIMEFORMAT,buf,64)==11 && !lstrcmpW(buf,L"h:mm:ss tt"));
    C(GetLocaleInfoW(0x409,LOCALE_SAM,buf,64)==3 && !lstrcmpW(buf,L"AM"));
    C(GetLocaleInfoW(0x409,LOCALE_SPM,buf,64)==3 && !lstrcmpW(buf,L"PM"));
    C(GetLocaleInfoW(0x409,LOCALE_SDAYNAME1,buf,64)==7 && !lstrcmpW(buf,L"Monday"));
    C(GetLocaleInfoW(0x409,LOCALE_SDAYNAME7,buf,64)==7 && !lstrcmpW(buf,L"Sunday"));
    C(GetLocaleInfoW(0x409,LOCALE_SABBREVDAYNAME3,buf,64)==4 && !lstrcmpW(buf,L"Wed"));
    C(GetLocaleInfoW(0x409,LOCALE_SMONTHNAME1,buf,64)==8 && !lstrcmpW(buf,L"January"));
    C(GetLocaleInfoW(0x409,LOCALE_SMONTHNAME12,buf,64)==9 && !lstrcmpW(buf,L"December"));
    C(GetLocaleInfoW(0x409,LOCALE_SABBREVMONTHNAME9,buf,64)==4 && !lstrcmpW(buf,L"Sep"));
    C(GetLocaleInfoW(0x409,LOCALE_SPOSITIVESIGN,buf,64)==1 && !lstrcmpW(buf,L""));
    C(GetLocaleInfoW(0x409,LOCALE_SNEGATIVESIGN,buf,64)==2 && !lstrcmpW(buf,L"-"));
    C(GetLocaleInfoW(0x409,LOCALE_SNATIVEDIGITS,buf,64)==11 && !lstrcmpW(buf,L"0123456789"));
    C(GetLocaleInfoW(0x409,LOCALE_SABBREVLANGNAME,buf,64)==4 && !lstrcmpW(buf,L"ENU"));
    C(GetLocaleInfoW(0x409,LOCALE_SABBREVCTRYNAME,buf,64)==4 && !lstrcmpW(buf,L"USA"));
    C(GetLocaleInfoW(0x409,LOCALE_SNATIVELANGNAME,buf,64)==8 && !lstrcmpW(buf,L"English"));
    C(GetLocaleInfoW(0x409,LOCALE_SNATIVECTRYNAME,buf,64)==14 && !lstrcmpW(buf,L"United States"));
    C(GetLocaleInfoW(0x409,LOCALE_SENGCOUNTRY,buf,64)==14 && !lstrcmpW(buf,L"United States"));
    C(GetLocaleInfoW(0x409,LOCALE_SLANGUAGE,buf,64)==24 && !lstrcmpW(buf,L"English (United States)"));
    C(GetLocaleInfoW(0x409,LOCALE_SLOCALIZEDDISPLAYNAME,buf,64)==24 && !lstrcmpW(buf,L"English (United States)"));

    /* Numbers, as text and with LOCALE_RETURN_NUMBER. */
    C(GetLocaleInfoW(0x409,LOCALE_ILANGUAGE,buf,64)==5 && !lstrcmpW(buf,L"0409"));
    C(GetLocaleInfoW(0x409,LOCALE_IDIGITS,buf,64)==2 && !lstrcmpW(buf,L"2"));
    C(GetLocaleInfoW(0x409,LOCALE_ICENTURY,buf,64)==2 && !lstrcmpW(buf,L"1"));
    C(GetLocaleInfoW(0x409,LOCALE_IDATE,buf,64)==2 && !lstrcmpW(buf,L"0"));
    C(GetLocaleInfoW(0x409,LOCALE_SLONGDATE,0,0)==19 && GetLocaleInfoW(0x409,LOCALE_SISO639LANGNAME,0,0)==3);
    C(GetLocaleInfoW(0x409,LOCALE_ILANGUAGE|LOCALE_RETURN_NUMBER,0,0)==2);
    DWORD value=0;
    C(GetLocaleInfoW(0x409,LOCALE_IDEFAULTANSICODEPAGE|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==65001);
    C(GetLocaleInfoW(0x409,LOCALE_IDEFAULTCODEPAGE|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==65001);
    C(GetLocaleInfoW(0x409,LOCALE_ILANGUAGE|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==0x409);
    C(GetLocaleInfoW(0x409,LOCALE_ICALENDARTYPE|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==1);
    C(GetLocaleInfoW(0x409,LOCALE_IDIGITS|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==2);
    C(GetLocaleInfoW(0x409,LOCALE_INEGNUMBER|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==1);
    C(GetLocaleInfoW(0x409,LOCALE_IFIRSTDAYOFWEEK|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==0);
    C(GetLocaleInfoW(0x409,LOCALE_ICURRENCY|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==0);
    C(GetLocaleInfoW(0x409,LOCALE_INEGCURR|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==1);
    C(GetLocaleInfoW(0x409,LOCALE_ICURRDIGITS|LOCALE_RETURN_NUMBER,(LPWSTR)&value,2)==2 && value==2);

    /* Failures. */
    C(!GetLocaleInfoW(0x409,LOCALE_SLONGDATE,buf,4) && GetLastError()==122);
    C(!GetLocaleInfoW(0x409,LOCALE_SLONGDATE,0,64) && GetLastError()==122);
    C(!GetLocaleInfoW(0x409,LOCALE_IDIGITS|LOCALE_RETURN_NUMBER,(LPWSTR)&value,1) && GetLastError()==122);
    C(!GetLocaleInfoW(0x411,LOCALE_SLONGDATE,buf,64) && GetLastError()==87);
    C(!GetLocaleInfoW(0x409,0x00001234,buf,64) && GetLastError()==87);
    C(!GetLocaleInfoW(0x409,LOCALE_SDECIMAL|LOCALE_RETURN_NUMBER,buf,64) && GetLastError()==87);
    C(!GetLocaleInfoW(0x409,LOCALE_SDECIMAL|0x04000000,buf,64) && GetLastError()==87);
    C(!GetLocaleInfoW(0x409,LOCALE_SDECIMAL|0x10000000,buf,64) && GetLastError()==1004);
    C(GetLocaleInfoW(0x409,LOCALE_SDECIMAL|LOCALE_NOUSEROVERRIDE,buf,64)==2 && !lstrcmpW(buf,L"."));
    C(GetLocaleInfoW(0x409,LOCALE_SISO639LANGNAME|LOCALE_USE_CP_ACP,buf,64)==3);

    /* ANSI and Ex variants. */
    char abuf[64];
    C(GetLocaleInfoA(0x409,LOCALE_SISO639LANGNAME,abuf,64)==3 && !lstrcmpA(abuf,"en"));
    C(GetLocaleInfoA(0x409,LOCALE_SSHORTDATE,abuf,64)==9 && !lstrcmpA(abuf,"M/d/yyyy"));
    C(!GetLocaleInfoA(0x409,LOCALE_SLONGDATE,abuf,8) && GetLastError()==122);
    DWORD avalue=0;
    C(GetLocaleInfoA(0x409,LOCALE_IDEFAULTANSICODEPAGE|LOCALE_RETURN_NUMBER,(LPSTR)&avalue,4)==4 && avalue==65001);
    C(GetLocaleInfoEx(L"en-US",LOCALE_SISO639LANGNAME,buf,64)==3 && !lstrcmpW(buf,L"en"));
    C(GetLocaleInfoEx(L"en",LOCALE_SNAME,buf,64)==6 && !lstrcmpW(buf,L"en-US"));
    C(!GetLocaleInfoEx(L"ja-JP",LOCALE_SNAME,buf,64) && GetLastError()==87);
    C(!GetLocaleInfoEx(0,LOCALE_SNAME,buf,64) && GetLastError()==87);
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"locale ok\n",10,&written,0);ExitProcess(0);
}
