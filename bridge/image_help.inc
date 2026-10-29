/* ImageHlp header/RVA helpers; RVA translation follows a raw file layout. */
__attribute__((ms_abi)) void *ImageNtHeader(void *base)
{
    if(!base) return NULL; uintptr_t p=(uintptr_t)base; uint16_t mz; uint32_t off, signature;
    if(!diagnostic_read(p,&mz,2) || mz!=0x5a4d || !diagnostic_read(p+0x3c,&off,4) || off>0x1000000 || p+off<p ||
       !diagnostic_read(p+off,&signature,4) || signature!=0x4550) return NULL;
    return (void *)(p+off);
}
__attribute__((ms_abi)) void *ImageRvaToSection(void *nt,void *base,DWORD rva)
{
    (void)base; if(!nt) return NULL; uintptr_t p=(uintptr_t)nt; uint32_t signature; uint16_t count, optional;
    if(!diagnostic_read(p,&signature,4) || signature!=0x4550 || !diagnostic_read(p+6,&count,2) || count>96 ||
       !diagnostic_read(p+20,&optional,2)) return NULL;
    uintptr_t section=p+24+optional; if(section<p) return NULL;
    for(unsigned i=0;i<count;i++) { uint32_t va,size; uintptr_t s=section+i*40;
        if(!diagnostic_read(s+12,&va,4) || !diagnostic_read(s+16,&size,4)) return NULL;
        if(rva>=va && rva-va<size) return (void *)s;
    } return NULL;
}
__attribute__((ms_abi)) void *ImageRvaToVa(void *nt,void *base,DWORD rva,void **last)
{
    void *section=ImageRvaToSection(nt,base,rva); if(!section) return NULL;
    uint32_t va,raw; if(!diagnostic_read((uintptr_t)section+12,&va,4) || !diagnostic_read((uintptr_t)section+20,&raw,4)) return NULL;
    uintptr_t result=(uintptr_t)base+raw+(rva-va); if(result<(uintptr_t)base) return NULL;
    if(last) *last=section; return (void *)result;
}
