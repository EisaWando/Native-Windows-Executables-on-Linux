#include <windows.h>
void mainCRTStartup(void)
{
    SRWLOCK lock; CONDITION_VARIABLE cv; DWORD written; int ok = 1;
    InitializeSRWLock(&lock); InitializeConditionVariable(&cv);
    AcquireSRWLockExclusive(&lock);
    ok &= !TryAcquireSRWLockExclusive(&lock) && !TryAcquireSRWLockShared(&lock);
    ok &= !SleepConditionVariableSRW(&cv, &lock, 1, 0) && GetLastError() == ERROR_TIMEOUT;
    ok &= !TryAcquireSRWLockExclusive(&lock); ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockShared(&lock); ok &= TryAcquireSRWLockShared(&lock) && !TryAcquireSRWLockExclusive(&lock);
    ReleaseSRWLockShared(&lock); ReleaseSRWLockShared(&lock);
    ok &= TryAcquireSRWLockExclusive(&lock); ReleaseSRWLockExclusive(&lock);
    WakeConditionVariable(&cv); WakeAllConditionVariable(&cv);
    static OSVERSIONINFOEXW version; version.dwOSVersionInfoSize = sizeof(version);
    ok &= GetVersionExW((OSVERSIONINFOW *)&version) && version.dwMajorVersion == 10 && version.dwPlatformId == VER_PLATFORM_WIN32_NT;
    SYSTEM_INFO system; GetNativeSystemInfo(&system);
    ok &= system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 && system.dwPageSize == 4096 && system.dwNumberOfProcessors == 1;
    BOOL wow = 1; ok &= IsWow64Process(GetCurrentProcess(), &wow) && !wow;
    DWORD_PTR process_mask, system_mask;
    ok &= GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask == 1 && system_mask == 1;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "sync ok\n" : "sync FAIL\n", ok ? 8 : 10, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
