#include <windows.h>
/* Force the fault with RSP in the guard page: only the child's alt stack can
 * deliver the dumper reliably. This PE intentionally terminates with 139. */
__attribute__((naked)) static DWORD WINAPI fault(void *p)
{
    (void)p;
    __asm__ volatile ("mov %gs:0x10,%rax\n lea -16(%rax),%rsp\n movq $1,(%rsp)\n ud2");
}
void mainCRTStartup(void)
{
    DWORD tid, written;
    HANDLE h = CreateThread(0, 0, fault, 0, CREATE_SUSPENDED, &tid);
    if (!h) ExitProcess(1);
    char buffer[64] = "fault tid "; char digits[16]; int count = 0, n = 10;
    do { digits[count++] = '0'+tid%10; tid /= 10; } while (tid);
    while (count) buffer[n++] = digits[--count]; buffer[n++] = '\n';
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, n, &written, 0);
    ResumeThread(h); WaitForSingleObject(h, INFINITE); ExitProcess(2);
}
