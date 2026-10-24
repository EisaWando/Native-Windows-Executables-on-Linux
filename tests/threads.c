#define UNICODE
#include <windows.h>
#include <stdint.h>
#define N 8
static volatile LONG total, protected_total, failures, destructors, attaches, detaches, suspended_ran;
static CRITICAL_SECTION critical;
static DWORD tls_slot, fls_slot, tls_index = 0xffffffff;
static HANDLE start_event, done_event, ready_semaphore;
struct local_data { DWORD initial, value, zero; };
static const DWORD template_data __attribute__((section(".tls"), used)) = 0x12345678;
static struct local_data *locals(void)
{ void **p; __asm__ volatile ("mov %%gs:0x58,%0" : "=r"(p)); return p[tls_index]; }
static void check(int ok) { if (!ok) InterlockedIncrement(&failures); }
static void NTAPI tls_callback(void *base, DWORD reason, void *reserved)
{
    (void)base; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH || reason == DLL_THREAD_ATTACH) {
        struct local_data *v = locals();
        check(v->initial == 0x12345678 && !v->value && !v->zero && !((uintptr_t)v & 63));
        v->value = GetCurrentThreadId();
    }
    if (reason == DLL_THREAD_ATTACH) InterlockedIncrement(&attaches);
    if (reason == DLL_THREAD_DETACH) InterlockedIncrement(&detaches);
}
static PIMAGE_TLS_CALLBACK tls_callbacks[] = {tls_callback, 0};
const IMAGE_TLS_DIRECTORY64 _tls_used __attribute__((section(".rdata$T"), used)) = {
    (uintptr_t)&template_data, (uintptr_t)&template_data+4, (uintptr_t)&tls_index,
    (uintptr_t)tls_callbacks, 8, IMAGE_SCN_ALIGN_64BYTES
};
static void NTAPI fls_destructor(void *value)
{ check((uintptr_t)value >= 1 && (uintptr_t)value <= N); InterlockedIncrement(&destructors); }
static DWORD WINAPI worker(void *parameter)
{
    uintptr_t number = (uintptr_t)parameter;
    check(!TlsGetValue(tls_slot) && !FlsGetValue(fls_slot));
    check(TlsSetValue(tls_slot, (void *)number) && FlsSetValue(fls_slot, (void *)number));
    check(locals()->value == GetCurrentThreadId());
    check(!TryEnterCriticalSection(&critical)); /* main holds it until all workers are ready */
    check(ReleaseSemaphore(ready_semaphore, 1, 0));
    check(WaitForSingleObject(start_event, 10000) == WAIT_OBJECT_0);
    HANDLE heap = GetProcessHeap();
    for (int i = 0; i < 10000; i++) {
        InterlockedIncrement(&total);
        EnterCriticalSection(&critical);
        if (i < 10) { check(TryEnterCriticalSection(&critical)); LeaveCriticalSection(&critical); }
        protected_total++;
        LeaveCriticalSection(&critical);
        if (i % 37 == 0) {
            check(TlsGetValue(tls_slot) == (void *)number && FlsGetValue(fls_slot) == (void *)number);
            check(locals()->value == GetCurrentThreadId());
            SetLastError(1000+number); SwitchToThread(); check(GetLastError() == 1000+number);
        }
        if (i < 2000) {
            SIZE_T size = i%500 ? 16+(i*37+number*53)%8192 : 300000;
            unsigned char *p = HeapAlloc(heap, HEAP_ZERO_MEMORY, size);
            check(p != 0);
            if (p) {
                check(HeapSize(heap, 0, p) == size && !p[0] && !p[size-1]);
                p[0] = number; p[size-1] = 99;
                if (i%127 == 0) {
                    unsigned char *q = HeapReAlloc(heap, HEAP_ZERO_MEMORY, p, size+513);
                    check(q != 0);
                    if (q) { p = q; check(p[0] == number && p[size-1] == 99 && !p[size] && !p[size+512]); }
                }
                check(HeapFree(heap, 0, p));
            }
        }
        if (i%100 == 0) {
            HBRUSH brush = CreateSolidBrush(number); LOGBRUSH lb;
            check(brush && GetObjectW(brush, sizeof(lb), &lb) == sizeof(lb) && lb.lbColor == number && DeleteObject(brush));
            check(GetModuleHandleW(L"kernel32.dll") != 0);
        }
    }
    if (number == 1) check(SetEvent(done_event));
    return 40+number;
}
static DWORD WINAPI suspended(void *p)
{ (void)p; InterlockedIncrement(&suspended_ran); ExitThread(123); }
static DWORD WINAPI abandon(void *p)
{ check(WaitForSingleObject(p, 5000) == WAIT_OBJECT_0); return 7; }
static HANDLE closed_gate, closed_done;
static DWORD WINAPI closed_worker(void *p)
{ (void)p; check(WaitForSingleObject(closed_gate, 5000) == WAIT_OBJECT_0); check(SetEvent(closed_done)); return 9; }
void mainCRTStartup(void)
{
    HANDLE threads[N]; DWORD ids[N], code, written; int expected_threads = N;
    DWORD main_tid = GetCurrentThreadId();
    check(locals()->value == main_tid);
    tls_slot = TlsAlloc(); fls_slot = FlsAlloc(fls_destructor);
    check(tls_slot != TLS_OUT_OF_INDEXES && fls_slot != FLS_OUT_OF_INDEXES);
    check(TlsSetValue(tls_slot, (void *)0x111));
    InitializeCriticalSection(&critical);
    start_event = CreateEventW(0, TRUE, FALSE, 0); done_event = CreateEventA(0, FALSE, FALSE, 0);
    ready_semaphore = CreateSemaphoreW(0, 0, N, 0);
    EnterCriticalSection(&critical);
    for (int i = 0; i < N; i++) {
        threads[i] = CreateThread(0, 0, worker, (void *)(uintptr_t)(i+1), 0, &ids[i]);
        check(threads[i] && ids[i] && ids[i] != main_tid);
    }
    for (int i = 0; i < N; i++) check(WaitForSingleObject(ready_semaphore, 10000) == WAIT_OBJECT_0);
    LeaveCriticalSection(&critical); check(SetEvent(start_event));
    check(WaitForSingleObject(done_event, 10000) == WAIT_OBJECT_0);
    check(WaitForMultipleObjects(N, threads, TRUE, 20000) == WAIT_OBJECT_0);
    for (int i = 0; i < N; i++) {
        check(GetExitCodeThread(threads[i], &code) && code == (DWORD)(41+i));
        check(CloseHandle(threads[i]));
        check(!GetExitCodeThread(threads[i], &code) && GetLastError() == ERROR_INVALID_HANDLE);
    }
    check(total == 80000 && protected_total == 80000 && destructors == N);
    check(TlsGetValue(tls_slot) == (void *)0x111 && !FlsGetValue(fls_slot));
    HANDLE t = CreateThread(0, 0, suspended, 0, CREATE_SUSPENDED, 0); expected_threads++;
    check(t && GetExitCodeThread(t, &code) && code == STILL_ACTIVE);
    SleepEx(1, FALSE); check(!suspended_ran && WaitForSingleObject(t, 0) == WAIT_TIMEOUT);
    check(ResumeThread(t) == 1 && ResumeThread(t) == 0);
    check(WaitForSingleObjectEx(t, 5000, FALSE) == WAIT_OBJECT_0 && GetExitCodeThread(t, &code) && code == 123);
    check(suspended_ran == 1 && CloseHandle(t));
    HANDLE named = CreateEventW(0, TRUE, FALSE, L"thread-test-event");
    HANDLE alias = CreateEventA(0, FALSE, TRUE, "thread-test-event");
    check(named && alias && named != alias && GetLastError() == ERROR_ALREADY_EXISTS);
    check(WaitForSingleObject(alias, 0) == WAIT_TIMEOUT && SetEvent(named));
    check(WaitForSingleObject(alias, 0) == WAIT_OBJECT_0 && WaitForSingleObject(named, 0) == WAIT_OBJECT_0);
    check(ResetEvent(alias) && CloseHandle(named) && SetEvent(alias) && CloseHandle(alias));
    HANDLE pair[2]; pair[0] = CreateEventW(0, FALSE, TRUE, 0); pair[1] = CreateEventW(0, FALSE, FALSE, 0);
    check(WaitForMultipleObjects(2, pair, TRUE, 0) == WAIT_TIMEOUT);
    check(WaitForSingleObject(pair[0], 0) == WAIT_OBJECT_0); /* wait-all did not consume it */
    check(SetEvent(pair[0]) && SetEvent(pair[1]) && WaitForMultipleObjects(2, pair, TRUE, 0) == WAIT_OBJECT_0);
    check(WaitForSingleObject(pair[0], 0) == WAIT_TIMEOUT && SetEvent(pair[1]));
    check(WaitForMultipleObjects(2, pair, FALSE, 0) == WAIT_OBJECT_0+1);
    check(CloseHandle(pair[0]) && CloseHandle(pair[1]));
    HANDLE sem = CreateSemaphoreA(0, 0, 2, 0); LONG previous = -1;
    check(WaitForSingleObject(sem, 1) == WAIT_TIMEOUT && ReleaseSemaphore(sem, 2, &previous) && previous == 0);
    check(!ReleaseSemaphore(sem, 1, 0) && GetLastError() == ERROR_TOO_MANY_POSTS);
    check(WaitForSingleObject(sem, 0) == WAIT_OBJECT_0 && WaitForSingleObject(sem, 0) == WAIT_OBJECT_0 && WaitForSingleObject(sem, 0) == WAIT_TIMEOUT);
    check(CloseHandle(sem));
    HANDLE mutex = CreateMutexW(0, TRUE, 0);
    check(WaitForSingleObject(mutex, 0) == WAIT_OBJECT_0 && ReleaseMutex(mutex) && ReleaseMutex(mutex));
    check(!ReleaseMutex(mutex) && GetLastError() == ERROR_NOT_OWNER);
    t = CreateThread(0, 0, abandon, mutex, 0, 0); expected_threads++;
    check(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    pair[0] = t; pair[1] = mutex;
    check(WaitForMultipleObjects(2, pair, TRUE, 1000) == WAIT_ABANDONED_0+1);
    check(ReleaseMutex(mutex) && CloseHandle(t) && CloseHandle(mutex));
    closed_gate = CreateEventW(0, TRUE, FALSE, 0); closed_done = CreateEventW(0, FALSE, FALSE, 0);
    t = CreateThread(0, 0, closed_worker, 0, 0, 0); expected_threads++;
    check(t && CloseHandle(t) && SetEvent(closed_gate) && WaitForSingleObject(closed_done, 5000) == WAIT_OBJECT_0);
    for (int i = 0; i < 5000 && detaches < expected_threads; i++) Sleep(1);
    check(attaches == expected_threads && detaches == expected_threads);
    check(CloseHandle(closed_gate) && CloseHandle(closed_done));
    check(WaitForSingleObject((HANDLE)1, 0) == WAIT_FAILED && GetLastError() == ERROR_INVALID_HANDLE);
    LONG exported = 0;
    LONG (WINAPI *inc)(volatile LONG *) = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "InterlockedIncrement");
    LONG (WINAPI *dec)(volatile LONG *) = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "InterlockedDecrement");
    LONG (WINAPI *exchange)(volatile LONG *, LONG) = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "InterlockedExchange");
    LONG (WINAPI *compare)(volatile LONG *, LONG, LONG) = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "InterlockedCompareExchange");
    check(inc && dec && exchange && compare);
    if (inc && dec && exchange && compare) check(inc(&exported) == 1 && dec(&exported) == 0 && exchange(&exported, 10) == 0 && compare(&exported, 20, 10) == 10 && exported == 20);
    check(CloseHandle(start_event) && CloseHandle(done_event) && CloseHandle(ready_semaphore));
    check(TlsFree(tls_slot) && FlsFree(fls_slot)); DeleteCriticalSection(&critical);
    const char *message = failures ? "threads FAIL\n" : "threads ok\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, failures ? 13 : 11, &written, 0);
    ExitProcess(failures ? 1 : 0);
}
