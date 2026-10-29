/* /proc/self/cmdline preserves the Linux exec arguments as NUL-separated UTF-8. */
static bool pe_cmdline_init(void)
{
    size_t cap=262144, used=0; char *input=pe_raw_map(cap), *out=pe_raw_map(cap*4);
    if(!input || !out) return false;
    long fd=raw_syscall3(SYS_open,(long)"/proc/self/cmdline",0,0); if(fd<0) return false;
    while(used<cap-1) { long n=raw_syscall3(SYS_read,fd,(long)(input+used),cap-1-used); if(n<=0) break; used+=n; }
    raw_syscall3(SYS_close,fd,0,0); input[used]=0;
    char executable[4100]; if(!pe_windows_path(executable,sizeof(executable),0)) return false;
    size_t pos=0, cursor=0; unsigned arg=0;
    while(cursor<used) {
        size_t end=cursor; while(end<used && input[end]) end++;
        const char *s=arg?input+cursor:executable; size_t len=arg?end-cursor:(size_t)cstring_length(executable);
        bool quote=!len; for(size_t i=0;i<len;i++) if(s[i]==' ' || s[i]=='\t') quote=true;
        if(arg) out[pos++]=' '; if(quote) out[pos++]='"';
        for(size_t i=0;i<len;) {
            size_t slash=0; while(i<len && s[i]=='\\') { slash++;i++; }
            if(i<len && s[i]=='"') { for(size_t j=0;j<2*slash+1;j++) out[pos++]='\\';out[pos++]='"';i++; }
            else { size_t copies=(quote && i==len)?slash*2:slash; for(size_t j=0;j<copies;j++) out[pos++]='\\';if(i<len)out[pos++]=s[i++]; }
            if(pos>=cap*4-8) return false;
        }
        if(quote) out[pos++]='"'; cursor=end+1; arg++;
    }
    if(!arg) return false; out[pos]=0; pe_cmdline_a=out;
    int count=MultiByteToWideChar(65001,8,out,-1,NULL,0); if(!count) return false;
    pe_cmdline_w=pe_raw_map((size_t)count*2); if(!pe_cmdline_w || !MultiByteToWideChar(65001,8,out,-1,pe_cmdline_w,count)) return false;
    raw_syscall3(SYS_munmap,(long)input,cap,0);
    return true;
}
__attribute__((ms_abi)) uint16_t **CommandLineToArgvW(const uint16_t *command,int *argc)
{
    if(!command || !argc) { SetLastError(87);return NULL; }
    if(!*command) command=GetCommandLineW();
    size_t length=lstrlenW(command), maxargs=length+2;
    if(length>32767) { SetLastError(87);return NULL; }
    size_t bytes=maxargs*sizeof(uint16_t *)+(length+2)*2;
    uint16_t **args=LocalAlloc(0,bytes); if(!args) return NULL;
    uint16_t *dest=(uint16_t *)(args+maxargs); size_t i=0; int count=0;
    if(command[0]==' ' || command[0]=='\t') { args[count++]=dest; *dest++=0; }
    while(command[i]) {
        while(command[i]==' ' || command[i]=='\t') i++; if(!command[i]) break;
        args[count++]=dest; bool quoted=false;
        while(command[i] && (quoted || (command[i]!=' ' && command[i]!='\t'))) {
            size_t slash=0; while(command[i]=='\\') { slash++;i++; }
            if(command[i]=='"') {
                for(size_t j=0;j<slash/2;j++) *dest++='\\';
                if(slash&1) { *dest++='"'; i++; }
                else { i++; if(quoted && command[i]=='"') { *dest++='"';i++; } else quoted=!quoted; }
            } else { for(size_t j=0;j<slash;j++) *dest++='\\'; if(command[i] && (quoted || (command[i]!=' ' && command[i]!='\t'))) *dest++=command[i++]; }
        }
        *dest++=0;
    }
    args[count]=NULL;*argc=count;return args;
}
