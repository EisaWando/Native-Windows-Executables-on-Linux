#include <windows.h>
void mainCRTStartup(void)
{
    static SYSTEMTIME st, leap = {2000, 2, 0, 29, 12, 34, 56, 123};
    FILETIME zero = {0,0}, ft, local; DWORD written; int ok = 1;
    ok &= FileTimeToSystemTime(&zero, &st) && st.wYear == 1601 && st.wMonth == 1 && st.wDay == 1 && st.wDayOfWeek == 1;
    ok &= SystemTimeToFileTime(&leap, &ft) && FileTimeToSystemTime(&ft, &st) && st.wYear == 2000 && st.wMonth == 2 && st.wDay == 29 && st.wDayOfWeek == 2 && st.wMilliseconds == 123;
    ok &= CompareFileTime(&ft, &zero) > 0 && CompareFileTime(&ft, &ft) == 0;
    leap.wYear = 1900; ok &= !SystemTimeToFileTime(&leap, &local) && GetLastError() == ERROR_INVALID_PARAMETER;
    ok &= FileTimeToLocalFileTime(&ft, &local) && !CompareFileTime(&ft, &local);
    GetSystemTime(&st); ok &= st.wYear >= 2020 && st.wMonth >= 1 && st.wMonth <= 12;
    static TIME_ZONE_INFORMATION tz; ok &= GetTimeZoneInformation(&tz) == TIME_ZONE_ID_UNKNOWN && tz.Bias == 0;
    static SYSTEMTIME shifted, restored;
    leap.wYear = 2000; tz.Bias = -90;
    ok &= SystemTimeToTzSpecificLocalTime(&tz, &leap, &shifted) && shifted.wHour == 14 && shifted.wMinute == 4;
    ok &= TzSpecificLocalTimeToSystemTime(&tz, &shifted, &restored) && restored.wHour == 12 && restored.wMinute == 34;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "calendar ok\n" : "calendar FAIL\n", ok ? 12 : 14, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
