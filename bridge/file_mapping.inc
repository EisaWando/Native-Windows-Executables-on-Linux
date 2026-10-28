/* Section objects are reference-counted by both handles and mapped views. */
struct pe_section { int fd; uint64_t size; DWORD protect, flags; unsigned char *image; size_t image_file_size; struct pe_image_layout layout; uint16_t path[4096]; };
struct pe_section_view { struct pe_section_view *next; struct pe_wait_object *object; void *base; size_t size; DWORD protect, type; };
static struct pe_section_view *pe_section_views;
static void pe_section_destroy(struct pe_section *s)
{
    if(s->fd>=0) raw_syscall3(SYS_close,s->fd,0,0);
    if(s->image) raw_syscall3(SYS_munmap,(long)s->image,s->image_file_size,0);
    raw_syscall3(SYS_munmap,(long)s,sizeof(*s),0);
}
static bool pe_section_image(struct pe_section *s)
{
    if(!s->size || s->size>134217728) return false;
    s->image_file_size=s->size; s->image=pe_raw_map(s->size); if(!s->image) return false;
    size_t n=0; while(n<s->size) { long r=raw_syscall4(SYS_pread64,s->fd,(long)(s->image+n),s->size-n,n); if(r<=0) return false; n+=r; }
    return pe_image_parse(s->image,s->image_file_size,&s->layout);
}
__attribute__((ms_abi)) HANDLE CreateFileMappingW(HANDLE file,void *security,DWORD protect,DWORD high,DWORD low,const uint16_t *name)
{
    (void)security; DWORD mode=protect&0xff, flags=protect&~0xffu; uint64_t size=((uint64_t)high<<32)|low;
    if(flags & 0x4000000u) { SetLastError(50); return NULL; }
    if((mode!=2 && mode!=4 && mode!=8) || (flags&~(0x1000000u|0x4000000u|0x8000000u)) || ((flags&0x1000000u) && file==(HANDLE)(intptr_t)-1)) { SetLastError(87); return NULL; }
    PE_STATE_GUARD; bool exists=false; struct pe_wait_object *o=pe_wait_new(PE_SECTION,name,&exists); if(!o) return NULL;
    if(exists) { HANDLE h=pe_wait_handle_new(o); if(h) SetLastError(183); return h; }
    struct pe_section *s=pe_raw_map(sizeof(*s)); if(!s) { pe_wait_reap(); SetLastError(8); return NULL; }
    s->fd=-1; o->u.section=s; s->protect=mode; s->flags=flags;
    if(file==(HANDLE)(intptr_t)-1) {
        if(!size) { pe_wait_reap(); SetLastError(87); return NULL; }
        s->fd=raw_syscall3(SYS_memfd_create,(long)"pe-section",1,0);
        if(s->fd<0 || raw_syscall3(SYS_ftruncate,s->fd,size,0)<0) { pe_wait_reap(); SetLastError(8); return NULL; }
    } else {
        s->fd=pe_fd_pin(file); if(s->fd<0) { pe_wait_reap(); return NULL; }
        struct stat st; if(raw_syscall3(SYS_fstat,s->fd,(long)&st,0)<0 || st.st_size<0) { pe_wait_reap(); SetLastError(6); return NULL; }
        if(!size) size=st.st_size;
        if(size>(uint64_t)st.st_size && (mode!=4 || raw_syscall3(SYS_ftruncate,s->fd,size,0)<0)) { pe_wait_reap(); SetLastError(5); return NULL; }
        char link[64]="/proc/self/fd/", path[4096]; unsigned pos=14; int fd=s->fd; char digits[16]; unsigned k=0;
        do { digits[k++]='0'+fd%10; fd/=10; } while(fd && k<sizeof(digits));
        while(k) link[pos++]=digits[--k]; link[pos]=0;
        long n=raw_syscall4(SYS_readlinkat,-100,(long)link,(long)path,sizeof(path)-1);
        if(n>0) { path[n]=0; MultiByteToWideChar(65001,0,path,-1,s->path,4096); }
    }
    s->size=size;
    if((flags&0x1000000u) && !pe_section_image(s)) { pe_wait_reap(); SetLastError(193); return NULL; }
    HANDLE h=pe_wait_handle_new(o); if(!h) pe_wait_reap(); return h;
}
__attribute__((ms_abi)) HANDLE CreateFileMappingA(HANDLE file,void *security,DWORD protect,DWORD high,DWORD low,const char *name)
{ uint16_t wide[256]; if(name && !MultiByteToWideChar(65001,8,name,-1,wide,256)) return NULL; return CreateFileMappingW(file,security,protect,high,low,name?wide:NULL); }
__attribute__((ms_abi)) HANDLE OpenFileMappingW(DWORD access,BOOL inherit,const uint16_t *name)
{
    (void)access;(void)inherit; if(!name || !*name) { SetLastError(87); return NULL; }
    PE_STATE_GUARD; for(struct pe_wait_object *o=pe_wait_objects;o;o=o->next) if(o->kind==PE_SECTION && o->named && pe_wait_name_equal(o->name,name)) return pe_wait_handle_new(o);
    SetLastError(2); return NULL;
}
__attribute__((ms_abi)) HANDLE OpenFileMappingA(DWORD access,BOOL inherit,const char *name)
{ uint16_t wide[256]; if(!name || !MultiByteToWideChar(65001,8,name,-1,wide,256)) { SetLastError(87); return NULL; } return OpenFileMappingW(access,inherit,wide); }
static int pe_section_prot(DWORD flags)
{ int p=(flags & 4)?1:0; if(flags&2) p|=3; if(flags&1) p|=3; if(flags&0x20) p|=4; return p; }
__attribute__((ms_abi)) void *MapViewOfFileEx(HANDLE handle,DWORD access,DWORD high,DWORD low,size_t bytes,void *address)
{
    uint64_t offset=((uint64_t)high<<32)|low; if(offset&0xffff) { SetLastError(1132); return NULL; }
    if(!access || (access&~0x27u)) { SetLastError(87); return NULL; }
    PE_STATE_GUARD; struct pe_wait_object *o=pe_wait_lookup(handle,PE_SECTION); if(!o) return NULL;
    struct pe_section *s=o->u.section; if((access&2) && s->protect!=4 && !(s->flags&0x1000000u)) { SetLastError(5); return NULL; }
    if(offset>s->size || (bytes && bytes>s->size-offset)) { SetLastError(87); return NULL; }
    bool image=!!(s->flags&0x1000000u); if(image && (offset || bytes)) { SetLastError(87); return NULL; }
    size_t length=image?s->layout.image_size:(bytes?bytes:s->size-offset); if(!length) { SetLastError(87); return NULL; }
    struct pe_section_view *view=pe_raw_map(sizeof(*view)); if(!view) { SetLastError(8); return NULL; }
    int prot=pe_section_prot(access), flags=(access&1)?2:1; if(address) flags|=0x100000;
    long base=raw_syscall6(SYS_mmap,(long)address,length,image?3:prot,image?(flags|0x20):flags,image?-1:s->fd,image?0:offset);
    if(base<0) { raw_syscall3(SYS_munmap,(long)view,sizeof(*view),0); SetLastError(8); return NULL; }
    if(image) {
        unsigned char *dest=(unsigned char *)base;
        for(size_t i=0;i<s->layout.header_size;i++) dest[i]=s->image[i];
        for(unsigned i=0;i<s->layout.section_count;i++) {
            const unsigned char *p=s->image+s->layout.sections+(size_t)i*40; uint32_t va=pe_image_u32(p+12), raw=pe_image_u32(p+20), n=pe_image_u32(p+16);
            for(uint32_t j=0;j<n;j++) dest[va+j]=s->image[raw+j];
        }
        raw_syscall3(SYS_mprotect,base,(s->layout.header_size+4095)&~4095ULL,1);
        for(unsigned i=0;i<s->layout.section_count;i++) {
            const unsigned char *p=s->image+s->layout.sections+(size_t)i*40; uint32_t vs=pe_image_u32(p+8), va=pe_image_u32(p+12), n=pe_image_u32(p+16), chars=pe_image_u32(p+36);
            if(n>vs) vs=n; int mp=((chars&0x40000000u)?1:0)|((chars&0x80000000u)?2:0)|((chars&0x20000000u)?4:0);
            if(vs && raw_syscall3(SYS_mprotect,base+(va&~4095u),((uint64_t)(va&4095)+vs+4095)&~4095ULL,mp)<0) { raw_syscall3(SYS_munmap,base,length,0); raw_syscall3(SYS_munmap,(long)view,sizeof(*view),0); SetLastError(193); return NULL; }
        }
    }
    view->base=(void *)base;view->size=length;view->object=o;view->protect=s->protect;view->type=image?0x1000000:0x40000;view->next=pe_section_views;pe_section_views=view;o->refs++;return view->base;
}
__attribute__((ms_abi)) void *MapViewOfFile(HANDLE h,DWORD access,DWORD high,DWORD low,size_t size)
{ return MapViewOfFileEx(h,access,high,low,size,NULL); }
__attribute__((ms_abi)) BOOL UnmapViewOfFile(const void *base)
{
    PE_STATE_GUARD; struct pe_section_view **p=&pe_section_views; while(*p && (*p)->base!=base) p=&(*p)->next;
    if(!*p) { SetLastError(487); return 0; } struct pe_section_view *v=*p; *p=v->next;
    raw_syscall3(SYS_munmap,(long)v->base,v->size,0); v->object->refs--; raw_syscall3(SYS_munmap,(long)v,sizeof(*v),0);pe_wait_reap();return 1;
}
__attribute__((ms_abi)) BOOL FlushViewOfFile(const void *base,size_t bytes)
{
    PE_STATE_GUARD; uintptr_t a=(uintptr_t)base; for(struct pe_section_view *v=pe_section_views;v;v=v->next) {
        uintptr_t b=(uintptr_t)v->base; if(a>=b && a-b<v->size) { if(!bytes) bytes=v->size-(a-b); if(bytes>v->size-(a-b)) { SetLastError(87); return 0; }
            uintptr_t start=a&~4095ULL; return raw_syscall3(SYS_msync,start,(a-start)+bytes,4)==0; }
    } SetLastError(487); return 0;
}
struct pe_memory_basic { void *base, *allocation; DWORD allocation_protect, pad; size_t region_size; DWORD state, protect, type, pad2; };
__attribute__((ms_abi)) size_t VirtualQuery(const void *address,struct pe_memory_basic *info,size_t size)
{
    if(!address || !info || size<sizeof(*info)) { SetLastError(87); return 0; }
    PE_STATE_GUARD; uintptr_t a=(uintptr_t)address;
    for(struct pe_section_view *v=pe_section_views;v;v=v->next) {
        uintptr_t base=(uintptr_t)v->base; if(a<base || a-base>=v->size) continue;
        struct pe_section *s=v->object->u.section; uintptr_t region=base; size_t span=(v->size+4095)&~4095ULL; DWORD p=v->protect;
        if(v->type==0x1000000) {
            p=2; span=(s->layout.header_size+4095)&~4095ULL;
            for(unsigned i=0;i<s->layout.section_count;i++) {
                const unsigned char *h=s->image+s->layout.sections+(size_t)i*40;
                uint32_t va=pe_image_u32(h+12), n=pe_image_u32(h+8), raw=pe_image_u32(h+16), ch=pe_image_u32(h+36); if(raw>n)n=raw;
                uintptr_t start=base+(va&~4095u); size_t bytes=((size_t)(va&4095)+n+4095)&~4095ULL;
                if(a>=start && a-start<bytes) { region=start;span=bytes; bool r=!!(ch&0x40000000u),w=!!(ch&0x80000000u),x=!!(ch&0x20000000u); p=x?(w?0x40:r?0x20:0x10):(w?4:r?2:1); break; }
            }
            if(a>=region+span) { region=a&~4095ULL;span=4096;p=1; }
        }
        *info=(struct pe_memory_basic){(void*)region,(void*)base,v->protect,0,span,0x1000,p,v->type,0};return sizeof(*info);
    }
    SetLastError(487);return 0;
}
__attribute__((ms_abi)) DWORD GetMappedFileNameW(HANDLE process,const void *address,uint16_t *out,DWORD capacity)
{
    if(process!=(HANDLE)(intptr_t)-1 || !address || !out || !capacity) { SetLastError(87); return 0; }
    PE_STATE_GUARD; uintptr_t a=(uintptr_t)address;
    for(struct pe_section_view *v=pe_section_views;v;v=v->next) { uintptr_t b=(uintptr_t)v->base;
        if(a>=b && a-b<v->size) { uint16_t *path=v->object->u.section->path; if(!*path) { SetLastError(2);return 0; }
            DWORD i=0; while(path[i] && i+1<capacity) { out[i]=path[i]; i++; } out[i]=0;
            if(path[i]) { SetLastError(122);return capacity; } return i; }
    } SetLastError(487);return 0;
}
