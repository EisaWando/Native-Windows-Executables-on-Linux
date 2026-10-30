/* Windows wildcard matching, with optional semicolon-delimited alternatives. */
static int32_t pe_path_match_spec(const uint16_t *file,const uint16_t *spec,DWORD flags)
{
    if(!file || !spec || (flags&~0x10001u)) return (int32_t)0x80070057u;
    uint16_t pattern[4096];
    do {
        if(!(flags&0x10000u)) while(*spec==' ') spec++;
        unsigned n=0;
        while(*spec && (!(flags&1) || *spec!=';')) {
            if(n>=4095) return (int32_t)0x80070057u;
            pattern[n++]=*spec++;
        }
        pattern[n]=0;
        if(n && pe_find_match(pattern,file,false)) return 0;
        if(!*spec) break;
        spec++;
    } while(true);
    return 1;
}
__attribute__((ms_abi)) int32_t PathMatchSpecExW(const uint16_t *file,const uint16_t *spec,DWORD flags)
{ return pe_path_match_spec(file,spec,flags); }
__attribute__((ms_abi)) BOOL PathMatchSpecW(const uint16_t *file,const uint16_t *spec)
{ return pe_path_match_spec(file,spec,1)==0; }
__attribute__((ms_abi)) int32_t PathMatchSpecExA(const char *file,const char *spec,DWORD flags)
{
    uint16_t wide_file[4096],wide_spec[4096];
    if(!file || !spec || !MultiByteToWideChar(65001,8,file,-1,wide_file,4096) ||
       !MultiByteToWideChar(65001,8,spec,-1,wide_spec,4096)) return (int32_t)0x80070057u;
    return pe_path_match_spec(wide_file,wide_spec,flags);
}
__attribute__((ms_abi)) BOOL PathMatchSpecA(const char *file,const char *spec)
{ return PathMatchSpecExA(file,spec,1)==0; }
