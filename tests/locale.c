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
    /* GetStringTypeW: exact classes for ASCII, Latin-1 and the terminator. */
    WCHAR sample[]=L"aAZz09 \t.,\x0100\x00a0";
    WORD types[32];
    C(GetStringTypeW(CT_CTYPE1,sample,-1,types));
    C(types[0]==0x0382 && types[1]==0x0381 && types[2]==0x0301 && types[3]==0x0302);
                                                      /* a,A are also hexadecimal digits */
    C(types[4]==0x0284 && types[5]==0x0284);          /* digits: DIGIT|XDIGIT|DEFINED */
    C(types[6]==0x0248 && types[7]==0x0268);          /* space; tab is a control too */
    C(types[8]==0x0210 && types[9]==0x0210);          /* punctuation */
    C(types[10]==0x0301);                             /* U+0100 LATIN CAPITAL A WITH MACRON */
    C(types[11]==0x0248);                             /* U+00A0 NO-BREAK SPACE */
    C(types[12]==0x0220);                             /* the terminating null is a control */
    C(GetStringTypeW(CT_CTYPE2,L"1a",2,types));
    C(types[0]==C2_EUROPENUMBER && types[1]==C2_LEFTTORIGHT);
    C(GetStringTypeW(CT_CTYPE2,L"\x0001",1,types) && types[0]==C2_NOTAPPLICABLE);
    C(GetStringTypeW(CT_CTYPE3,L"a1",2,types));
    C(types[0]==(C3_LEXICAL|C3_ALPHA) && types[1]==C3_NOTAPPLICABLE);
    C(GetStringTypeW(CT_CTYPE3,L"\x0410.",2,types));   /* Cyrillic A */
    C(types[0]==(C3_LEXICAL|C3_ALPHA) && types[1]==0);
    C(GetStringTypeW(CT_CTYPE1,L"\x3042",1,types) && (types[0]&C1_ALPHA) && (types[0]&C1_DEFINED));
    C(GetStringTypeW(CT_CTYPE3,L"\x3042",1,types) && (types[0]&C3_HIRAGANA) && (types[0]&C3_ALPHA));
    C(GetStringTypeW(CT_CTYPE1,L"\x5b57",1,types) && (types[0]&C1_ALPHA));
    C(GetStringTypeW(CT_CTYPE3,L"\x5b57",1,types) && (types[0]&C3_IDEOGRAPH));
    C(GetStringTypeW(CT_CTYPE1,L"\xd800",1,types) && (types[0]&C1_DEFINED));
    C(GetStringTypeW(CT_CTYPE3,L"\xd800",1,types) && (types[0]&C3_HIGHSURROGATE));
    C(GetStringTypeW(CT_CTYPE3,L"\xdc00",1,types) && (types[0]&C3_LOWSURROGATE));
    C(GetStringTypeW(CT_CTYPE1,L"",0,types));          /* no-op count */
    C(GetStringTypeExW(0x409,CT_CTYPE1,L"a",1,types) && types[0]==0x0382);
    C(GetStringTypeExW(0x411,CT_CTYPE1,L"a",1,types) && types[0]==0x0382); /* locale is ignored */
    C(GetStringTypeA(0x409,CT_CTYPE1,"aA0 .,",5,types));
    C(types[0]==0x0382 && types[1]==0x0381 && types[2]==0x0284 && types[3]==0x0248 && types[4]==0x0210);
    C(GetStringTypeExA(0x409,CT_CTYPE1,"ab",-1,types) && types[0]==0x0382 && types[2]==0x0220);
    C(!GetStringTypeW(0, L"a",1,types) && GetLastError()==1004);
    C(!GetStringTypeW(CT_CTYPE1|CT_CTYPE2,L"a",1,types) && GetLastError()==1004);
    C(!GetStringTypeW(CT_CTYPE1,0,1,types) && GetLastError()==87);
    C(!GetStringTypeW(CT_CTYPE1,L"a",1,0) && GetLastError()==87);
    C(!GetStringTypeW(CT_CTYPE1,(LPCWCH)types,1,types) && GetLastError()==87);
    C(GetStringTypeW(CT_CTYPE1,L"a",-2,types) && types[0]==0x0382 && types[1]==0x0220);
                                                      /* any negative count is null-terminated */
    C(!GetStringTypeA(0x411,CT_CTYPE1,"a",1,types) && GetLastError()==87);

    /* Character predicates. */
    C(IsCharAlphaW(L'a') && IsCharAlphaW(L'Z') && !IsCharAlphaW(L'7') && !IsCharAlphaW(L'.'));
    C(IsCharAlphaNumericW(L'7') && IsCharAlphaNumericW(L'a') && !IsCharAlphaNumericW(L' '));
    C(IsCharUpperW(L'A') && !IsCharUpperW(L'a') && IsCharLowerW(L'a') && !IsCharLowerW(L'A'));
    C(IsCharAlphaW(0x00e9) && IsCharLowerW(0x00e9));
    C(IsCharAlphaA('a') && IsCharAlphaNumericA('9') && IsCharUpperA('A') && IsCharLowerA('a'));
    C(!IsCharAlphaA('1') && !IsCharUpperA('a'));
    /* LCMapString and the CharUpper/CharLower family. A source without a
     * terminator must not produce one in the destination. */
    WCHAR map[32];
    for(int i=0;i<32;i++) map[i]=0;
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"Abc def",7,map,32)==7 && !lstrcmpW(map,L"ABC DEF"));
    map[3]=0x5a5a;
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"abc",3,map,32)==3 && map[3]==0x5a5a && map[0]==L'A' && map[2]==L'C');
    for(int i=0;i<32;i++) map[i]=0;
    C(LCMapStringW(0x409,LCMAP_LOWERCASE,L"AbC DEF",-1,map,32)==7 && !lstrcmpW(map,L"abc def"));
    C(LCMapStringW(0x409,LCMAP_TITLECASE,L"abc def",-1,map,32)==7 && !lstrcmpW(map,L"Abc Def"));
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"abc",3,0,0)==3);       /* required size query */
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"abc",-1,0,0)==4);      /* including the terminator */
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"\x00e9\x00c9",-1,map,32)==2 && map[0]==0x00c9);
    C(LCMapStringW(0x409,LCMAP_LOWERCASE,L"\x00dc",1,map,4)==1 && map[0]==0x00fc); /* U+00DC -> U+00FC */
    C(LCMapStringW(0x409,LCMAP_UPPERCASE,L"\x00df",1,map,4)==1 && map[0]==0x00df); /* no expansion */
    C(LCMapStringW(0x409,LCMAP_UPPERCASE|NORM_IGNORECASE|NORM_IGNORESYMBOLS,L"ab",2,map,32)==2);
    C(!LCMapStringW(0x409,LCMAP_SORTKEY,L"a",1,map,32) && GetLastError()==1004);
    C(!LCMapStringW(0x409,LCMAP_FULLWIDTH,L"a",1,map,32) && GetLastError()==1004);
    C(!LCMapStringW(0x411,LCMAP_UPPERCASE,L"a",1,map,32) && GetLastError()==87);
    C(!LCMapStringW(0x409,LCMAP_UPPERCASE,L"abcd",4,map,2) && GetLastError()==122);
    C(!LCMapStringW(0x409,LCMAP_UPPERCASE,L"",0,map,32) && GetLastError()==87);
    C(!LCMapStringW(0x409,LCMAP_UPPERCASE,0,1,map,32) && GetLastError()==87);
    for(int i=0;i<32;i++) map[i]=0;
    C(LCMapStringEx(0,LCMAP_UPPERCASE,L"abc",3,map,32,0,0,0)==3 && map[0]==L'A' && map[2]==L'C' && map[3]==0);
    C(LCMapStringEx(L"en-US",LCMAP_LOWERCASE,L"ABC",-1,map,32,0,0,0)==3 && !lstrcmpW(map,L"abc"));
    C(!LCMapStringEx(L"ja-JP",LCMAP_UPPERCASE,L"a",1,map,32,0,0,0) && GetLastError()==87);
    C(!LCMapStringEx(0,LCMAP_UPPERCASE,L"a",1,map,32,(void*)1,0,0) && GetLastError()==87);
    char amap[32];
    C(LCMapStringA(0x409,LCMAP_UPPERCASE,"abc",3,amap,32)==3 && !lstrcmpA(amap,"ABC"));
    C(LCMapStringA(0x409,LCMAP_LOWERCASE,"ABC",-1,amap,32)==3 && !lstrcmpA(amap,"abc"));
    C(LCMapStringA(0x409,LCMAP_UPPERCASE,"abcdef",6,0,0)==6);
    C(!LCMapStringA(0x409,LCMAP_UPPERCASE,"abc",3,amap,2) && GetLastError()==122);

    lstrcpyW(map,L"aBc"); C(CharUpperW(map)==map && !lstrcmpW(map,L"ABC"));
    C((ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)L'a')==(ULONG_PTR)L'A');
    lstrcpyW(map,L"aBc"); C(CharLowerW(map)==map && !lstrcmpW(map,L"abc"));
    C((ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)L'A')==(ULONG_PTR)L'a');
    lstrcpyW(map,L"abc"); C(CharUpperBuffW(map,3)==3 && !lstrcmpW(map,L"ABC"));
    C(CharLowerBuffW(map,3)==3 && !lstrcmpW(map,L"abc"));
    C(CharUpperBuffW(map,0)==0);
    lstrcpyA(amap,"aBc"); C(CharUpperA(amap)==amap && !lstrcmpA(amap,"ABC"));
    lstrcpyA(amap,"aBc"); C(CharLowerA(amap)==amap && !lstrcmpA(amap,"abc"));
    lstrcpyA(amap,"abc"); C(CharUpperBuffA(amap,3)==3 && !lstrcmpA(amap,"ABC"));
    lstrcpyA(amap,"ABC"); C(CharLowerBuffA(amap,2)==2 && !lstrcmpA(amap,"abC"));
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"locale ok\n",10,&written,0);ExitProcess(0);
}
