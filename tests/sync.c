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
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "sync ok\n" : "sync FAIL\n", ok ? 8 : 10, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
