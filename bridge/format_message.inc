__attribute__((ms_abi)) HANDLE GlobalAlloc(unsigned flags, uint64_t size);
__attribute__((ms_abi)) HANDLE GlobalFree(HANDLE memory);

struct pe_system_message { DWORD id; const char *text; };
static const struct pe_system_message pe_system_messages[] = {
    { 2, "The system cannot find the file specified.\r\n" },
    { 3, "The system cannot find the path specified.\r\n" },
    { 5, "Access is denied.\r\n" },
    { 6, "The handle is invalid.\r\n" },
    { 8, "Not enough memory resources are available to process this command.\r\n" },
    { 87, "The parameter is incorrect.\r\n" },
    { 122, "The data area passed to a system call is too small.\r\n" },
    { 126, "The specified module could not be found.\r\n" },
    { 127, "The specified procedure could not be found.\r\n" },
    { 183, "Cannot create a file when that file already exists.\r\n" },
    { 193, "%1 is not a valid Win32 application.\r\n" },
    { 995, "The I/O operation has been aborted because of either a thread exit or an application request.\r\n" },
};
static const char *pe_system_message_text(DWORD id)
{
    for (unsigned i=0;i<sizeof(pe_system_messages)/sizeof(pe_system_messages[0]);i++)
        if (pe_system_messages[i].id==id) return pe_system_messages[i].text;
    return NULL;
}
static bool pe_format_put(uint16_t *out, size_t cap, size_t *used, uint16_t c)
{
    if (*used >= cap) return false;
    if (out) out[*used]=c;
    (*used)++;
    return true;
}
static bool pe_format_message_text(const uint16_t *source, DWORD flags,
                                   const uintptr_t *arguments,
                                   uint16_t *out, size_t cap, size_t *written)
{
    size_t used=0;
    for (size_t i=0;source[i];i++) {
        uint16_t c=source[i];
        if (c!='%' || (flags&0x200)) {
            if (!pe_format_put(out,cap,&used,c)) return false;
            continue;
        }
        uint16_t next=source[++i];
        if (!next) break;
        if (next=='0') break;
        if (next=='n' || next=='N') {
            if (!pe_format_put(out,cap,&used,'\r') || !pe_format_put(out,cap,&used,'\n')) return false;
            continue;
        }
        if (next=='%') {
            if (!pe_format_put(out,cap,&used,'%')) return false;
            continue;
        }
        if (next>='1' && next<='9') {
            unsigned index=next-'1';
            if (source[i+1]>='0' && source[i+1]<='9') index=(index+1)*10+(source[++i]-'0')-1;
            if (source[i+1]=='!') {
                i++; while(source[i] && source[i]!='!') i++;
                if (!source[i]) return false;
            }
            const uint16_t *insert=arguments ? (const uint16_t *)(uintptr_t)arguments[index] : NULL;
            if (!insert) return false;
            for (size_t j=0;insert[j];j++) if (!pe_format_put(out,cap,&used,insert[j])) return false;
            continue;
        }
        if (!pe_format_put(out,cap,&used,next)) return false;
    }
    if (!pe_format_put(out,cap,&used,0)) return false;
    *written=used-1;
    return true;
}
__attribute__((ms_abi)) DWORD FormatMessageW(DWORD flags, const void *source,
    DWORD id, DWORD language, uint16_t *buffer, DWORD size, void *arguments)
{
    (void)language;
    if ((flags&0x400) && (flags&(0x800|0x1000))) { SetLastError(87); return 0; }
    uint16_t converted[1024]; const uint16_t *text=NULL;
    if (flags&0x400) text=(const uint16_t *)source;
    else if (flags&0x1000) {
        const char *ansi=pe_system_message_text(id);
        if (!ansi || !MultiByteToWideChar(65001,0,ansi,-1,converted,1024)) { SetLastError(317); return 0; }
        text=converted;
    } else { SetLastError((flags&0x800)?1812:87); return 0; }
    if (!text || !buffer) { SetLastError(87); return 0; }
    const uintptr_t *array=NULL;
    if (!(flags&0x200)) {
        if (!arguments) { SetLastError(87); return 0; }
        array=(flags&0x2000)?(const uintptr_t *)arguments:*(const uintptr_t **)arguments;
    }
    size_t needed=0;
    if (!pe_format_message_text(text,flags,array,NULL,(size_t)-1,&needed)) { SetLastError(87); return 0; }
    uint16_t *target=buffer; DWORD capacity=size;
    if (flags&0x100) {
        uint16_t **slot=(uint16_t **)buffer;
        capacity=(DWORD)(needed+1 < size ? size : needed+1);
        target=GlobalAlloc(0,(uint64_t)capacity*2);
        if (!target) return 0;
        *slot=target;
    } else if (size<=needed) { SetLastError(122); return 0; }
    if (!pe_format_message_text(text,flags,array,target,capacity,&needed)) {
        if (flags&0x100) GlobalFree(target);
        SetLastError(122); return 0;
    }
    return (DWORD)needed;
}
__attribute__((ms_abi)) DWORD FormatMessageA(DWORD flags, const void *source,
    DWORD id, DWORD language, char *buffer, DWORD size, void *arguments)
{
    uint16_t wide_source[2048]; const void *input=source;
    if (flags&0x400) {
        if (!source || !MultiByteToWideChar(65001,0,source,-1,wide_source,2048)) { SetLastError(87); return 0; }
        input=wide_source;
    }
    uint16_t *wide=NULL; DWORD n=FormatMessageW(flags|0x100,input,id,language,(uint16_t *)&wide,size,arguments);
    if (!n) return 0;
    int bytes=WideCharToMultiByte(65001,0,wide,-1,NULL,0,NULL,NULL);
    if (bytes<=0 || (!(flags&0x100) && (DWORD)bytes>size)) { GlobalFree(wide); SetLastError(122); return 0; }
    char *target=buffer;
    if (flags&0x100) {
        target=GlobalAlloc(0,bytes); if (!target) { GlobalFree(wide); return 0; }
        *(char **)buffer=target;
    }
    WideCharToMultiByte(65001,0,wide,-1,target,bytes,NULL,NULL);
    GlobalFree(wide); return (DWORD)(bytes-1);
}
__attribute__((ms_abi)) void *LocalAlloc(unsigned flags, size_t bytes)
{ return GlobalAlloc(flags,bytes); }
__attribute__((ms_abi)) void *LocalFree(void *memory)
{ return GlobalFree(memory); }
