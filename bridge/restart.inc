/* Registration is process-local. Automatic restart requires an external supervisor. */
static uint16_t pe_restart_command[1025];
static DWORD pe_restart_flags;
static bool pe_restart_registered;
__attribute__((ms_abi)) int32_t RegisterApplicationRestart(const uint16_t *command,DWORD flags)
{
    if(flags&~15u) return (int32_t)0x80070057u;
    unsigned n=0;if(command) { while(n<=1024 && command[n]) n++;if(n>1024)return (int32_t)0x80070057u; }
    PE_STATE_GUARD;
    for(unsigned i=0;i<n;i++) pe_restart_command[i]=command[i];
    pe_restart_command[n]=0;pe_restart_flags=flags;pe_restart_registered=true;return 0;
}
__attribute__((ms_abi)) int32_t UnregisterApplicationRestart(void)
{ PE_STATE_GUARD;pe_restart_registered=false;pe_restart_command[0]=0;pe_restart_flags=0;return 0; }
__attribute__((ms_abi)) int32_t GetApplicationRestartSettings(HANDLE process,uint16_t *out,DWORD *size,DWORD *flags)
{
    if(process!=(HANDLE)(intptr_t)-1 || !size || (!out && *size)) return (int32_t)0x80070057u;
    PE_STATE_GUARD;if(!pe_restart_registered)return (int32_t)0x80070490u;
    DWORD needed=(DWORD)lstrlenW(pe_restart_command)+1,capacity=*size;*size=needed;
    if(flags)*flags=pe_restart_flags;
    if(!out && !capacity)return 0;
    if(capacity<needed)return (int32_t)0x8007007au;
    memcpy(out,pe_restart_command,needed*2);return 0;
}
