/* Synthetic MSVC metadata: validates diagnostics, not C++ unwinding. */
#include <windows.h>
#include <stdint.h>
static struct { uint64_t vft, spare; char name[32]; } td = {0, 0, ".?AVdiagnostic_test@@"};
static uint32_t ct[7], cta[2], ti[4];
static unsigned char object[32];
void mainCRTStartup(void)
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(0), found = 0;
    void *(__attribute__((ms_abi)) *header)(void *, void **) =
        (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "RtlPcToFileHeader");
    if (!header || header(&td, (void **)&found) != (void *)base || found != base)
        ExitProcess(1);
    uint32_t nt = *(uint32_t *)(base+0x3c), size = *(uint32_t *)(base+nt+24+0x38);
    if (header((void *)(base+size), (void **)&found) || found) ExitProcess(2);
    if (header((void *)(base-1), (void **)&found) || found) ExitProcess(3);
    ct[1] = (uintptr_t)&td-base; cta[0] = 1; cta[1] = (uintptr_t)ct-base;
    ti[3] = (uintptr_t)cta-base;
    for (int i = 0; i < 32; i++) object[i] = i;
    ULONG_PTR params[4] = {0x19930520, (uintptr_t)object, (uintptr_t)ti, base};
    RaiseException(0xe06d7363, 1, 4, params);
    ExitProcess(4);
}
