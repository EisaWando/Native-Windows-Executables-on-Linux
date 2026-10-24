/* Handcrafted PE TLS directory, without CRT or emutls support. */
#include <windows.h>
#include <stdint.h>
static uint32_t index_value = 0xdeadbeef;
struct tls_values { uint32_t initialized; int32_t negative; uint32_t zero_a, zero_b; };
static const struct { uint32_t initialized; int32_t negative; } tls_template
    __attribute__((section(".tls"), used)) = {123, -7};
static unsigned callback_count, callback_ok;
static struct tls_values *tls_data(void)
{
    void **array;
    __asm__ volatile ("mov %%gs:0x58, %0" : "=r"(array));
    return array ? array[index_value] : 0;
}
static void NTAPI callback(void *base, DWORD reason, void *reserved)
{
    struct tls_values *v = tls_data();
    callback_count++;
    callback_ok = base == GetModuleHandleW(0) && reason == DLL_PROCESS_ATTACH && !reserved &&
        index_value == 0 && v && !((uintptr_t)v & 63) && v->initialized == 123 && v->negative == -7 &&
        v->zero_a == 0 && v->zero_b == 0;
    if (v) v->zero_a = 42;
}
static PIMAGE_TLS_CALLBACK callbacks[] = {callback, 0};
const IMAGE_TLS_DIRECTORY64 _tls_used __attribute__((section(".rdata$T"), used)) = {
    (uintptr_t)&tls_template, (uintptr_t)&tls_template+sizeof(tls_template),
    (uintptr_t)&index_value, (uintptr_t)callbacks, 8, IMAGE_SCN_ALIGN_64BYTES
};
void mainCRTStartup(void)
{
    struct tls_values *v = tls_data();
    int ok = v && callback_count == 1 && callback_ok && v->initialized == 123 && v->negative == -7 &&
        v->zero_a == 42 && v->zero_b == 0;
    DWORD slot = TlsAlloc(), fls = FlsAlloc(0), written;
    ok &= TlsSetValue(slot, (void *)111) && FlsSetValue(fls, (void *)222);
    ok &= TlsGetValue(slot) == (void *)111 && FlsGetValue(fls) == (void *)222;
    ok &= tls_data() == v && v->zero_a == 42;
    ok &= TlsFree(slot) && FlsFree(fls);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok ? "tls ok: 123 -7 42 0\n" : "tls FAIL\n", ok ? 20 : 9, &written, 0);
    ExitProcess(ok ? 0 : 1);
}
