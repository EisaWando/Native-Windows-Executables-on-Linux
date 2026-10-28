/* Read RT_VERSION from a PE file without assuming it is the main image. */
struct pe_ver_file { unsigned char *p; size_t n; };
static bool pe_ver_range(struct pe_ver_file *f, uint64_t off, uint64_t n)
{ return off <= f->n && n <= f->n-off; }
static uint16_t pe_ver_u16(struct pe_ver_file *f, size_t off)
{ return pe_ver_range(f,off,2) ? (uint16_t)(f->p[off]|(f->p[off+1]<<8)) : 0; }
static uint32_t pe_ver_u32(struct pe_ver_file *f, size_t off)
{ return pe_ver_range(f,off,4) ? (uint32_t)f->p[off]|((uint32_t)f->p[off+1]<<8)|((uint32_t)f->p[off+2]<<16)|((uint32_t)f->p[off+3]<<24) : 0; }
static bool pe_ver_rva(struct pe_ver_file *f, size_t sections, unsigned count, uint32_t rva, uint32_t bytes, size_t *out)
{
    for (unsigned i=0;i<count;i++) {
        size_t s=sections+(size_t)i*40; if(!pe_ver_range(f,s,40)) return false;
        uint32_t va=pe_ver_u32(f,s+12), size=pe_ver_u32(f,s+16), raw=pe_ver_u32(f,s+20);
        if(rva>=va && rva-va<=size && bytes<=size-(rva-va) && pe_ver_range(f,(uint64_t)raw+rva-va,bytes)) { *out=(size_t)raw+rva-va; return true; }
    }
    return false;
}
static bool pe_version_read(const uint16_t *path, void *buffer, DWORD capacity, DWORD *needed)
{
    char linux_path[4096]; if(!pe_linux_path(path,linux_path,sizeof(linux_path))) return false;
    long fd=raw_syscall4(SYS_openat,-100,(long)linux_path,0x80000,0);
    if(fd<0) { SetLastError(pe_file_error(fd)); return false; }
    struct stat st; if(raw_syscall3(SYS_fstat,fd,(long)&st,0)<0 || st.st_size<64 || st.st_size>134217728) { raw_syscall3(SYS_close,fd,0,0); SetLastError(193); return false; }
    struct pe_ver_file f={pe_raw_map(st.st_size),(size_t)st.st_size};
    if(!f.p) { raw_syscall3(SYS_close,fd,0,0); SetLastError(8); return false; }
    size_t got=0; while(got<f.n) { long n=raw_syscall4(SYS_pread64,fd,(long)(f.p+got),f.n-got,got); if(n<=0) break; got+=n; }
    raw_syscall3(SYS_close,fd,0,0); bool ok=false; size_t nt, opt, sections, root, data; DWORD result=1812;
    if(got!=f.n || pe_ver_u16(&f,0)!=0x5a4d || !pe_ver_range(&f,0x3c,4)) goto done;
    nt=pe_ver_u32(&f,0x3c); if(!pe_ver_range(&f,nt,24) || pe_ver_u32(&f,nt)!=0x4550) goto done;
    unsigned count=pe_ver_u16(&f,nt+6), os=pe_ver_u16(&f,nt+20); opt=nt+24; sections=opt+os;
    if(!pe_ver_range(&f,opt,os) || !pe_ver_range(&f,sections,(uint64_t)count*40)) goto done;
    uint16_t magic=pe_ver_u16(&f,opt); unsigned dd=magic==0x20b?112:magic==0x10b?96:0;
    if(!dd || os<dd+24) goto done;
    uint32_t rva=pe_ver_u32(&f,opt+dd+16), size=pe_ver_u32(&f,opt+dd+20);
    if(!rva || size<16 || !pe_ver_rva(&f,sections,count,rva,size,&root)) goto done;
    uint32_t dir=0, entry=0;
    for(unsigned level=0;level<3;level++) {
        if(dir>size || size-dir<16) goto done;
        unsigned num=(unsigned)pe_ver_u16(&f,root+dir+12)+pe_ver_u16(&f,root+dir+14);
        if((uint64_t)num*8>size-dir-16) goto done;
        bool found=false;
        for(unsigned i=0;i<num;i++) { size_t e=root+dir+16+(size_t)i*8; uint32_t id=pe_ver_u32(&f,e);
            if((level==0 && id==16) || (level!=0 && !(id&0x80000000u))) { entry=pe_ver_u32(&f,e+4); found=true; break; } }
        if(!found || ((level<2) != !!(entry&0x80000000u))) goto done;
        dir=entry&0x7fffffffu;
    }
    if(dir>size || size-dir<16) goto done;
    rva=pe_ver_u32(&f,root+dir); DWORD n=pe_ver_u32(&f,root+dir+4);
    if(!n || !pe_ver_rva(&f,sections,count,rva,n,&data)) goto done;
    *needed=n;
    if(buffer) { if(capacity<n) { result=122; goto done; } for(DWORD i=0;i<n;i++) ((unsigned char*)buffer)[i]=f.p[data+i]; }
    ok=true;
done:
    raw_syscall3(SYS_munmap,(long)f.p,f.n,0); if(!ok) SetLastError(result); return ok;
}
__attribute__((ms_abi)) DWORD GetFileVersionInfoSizeW(const uint16_t *path,DWORD *handle)
{ DWORD n=0; if(handle) *handle=0; return pe_version_read(path,NULL,0,&n)?n:0; }
__attribute__((ms_abi)) DWORD GetFileVersionInfoSizeA(const char *path,DWORD *handle)
{ uint16_t w[4096]; if(!path || !MultiByteToWideChar(65001,8,path,-1,w,4096)) { SetLastError(87); return 0; } return GetFileVersionInfoSizeW(w,handle); }
__attribute__((ms_abi)) BOOL GetFileVersionInfoW(const uint16_t *path,DWORD handle,DWORD size,void *buffer)
{ (void)handle; DWORD n; if(!buffer) { SetLastError(87); return 0; } return pe_version_read(path,buffer,size,&n); }
__attribute__((ms_abi)) BOOL GetFileVersionInfoA(const char *path,DWORD handle,DWORD size,void *buffer)
{ uint16_t w[4096]; if(!path || !MultiByteToWideChar(65001,8,path,-1,w,4096)) { SetLastError(87); return 0; } return GetFileVersionInfoW(w,handle,size,buffer); }
