/* Bounds-checked PE file layout used by image-section mappings. */
struct pe_image_layout {
    const unsigned char *data;
    size_t file_size, sections;
    uint32_t image_size, header_size, section_alignment;
    uint16_t section_count;
};
static bool pe_image_bounds(size_t size, uint64_t offset, uint64_t length)
{ return offset <= size && length <= size-offset; }
static uint16_t pe_image_u16(const unsigned char *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t pe_image_u32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static bool pe_image_parse(const void *data, size_t size, struct pe_image_layout *out)
{
    const unsigned char *p=data;
    if(!p || !out || !pe_image_bounds(size,0,64) || pe_image_u16(p)!=0x5a4d) return false;
    uint32_t nt=pe_image_u32(p+0x3c);
    if(!pe_image_bounds(size,nt,24) || pe_image_u32(p+nt)!=0x4550) return false;
    uint16_t count=pe_image_u16(p+nt+6), optlen=pe_image_u16(p+nt+20);
    uint64_t opt=(uint64_t)nt+24, sections=opt+optlen;
    if(!count || count>96 || optlen<64 || !pe_image_bounds(size,opt,optlen) ||
       !pe_image_bounds(size,sections,(uint64_t)count*40)) return false;
    uint16_t magic=pe_image_u16(p+opt);
    if(magic!=0x20b && magic!=0x10b) return false;
    uint32_t alignment=pe_image_u32(p+opt+32), image=pe_image_u32(p+opt+56), headers=pe_image_u32(p+opt+60);
    if(!alignment || (alignment & (alignment-1)) || !image || image%alignment ||
       !headers || headers>image || headers>size || sections+(uint64_t)count*40>headers) return false;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *s=p+sections+(size_t)i*40;
        uint32_t vs=pe_image_u32(s+8), va=pe_image_u32(s+12), rawsize=pe_image_u32(s+16), raw=pe_image_u32(s+20);
        uint32_t span=vs>rawsize?vs:rawsize;
        if(va%alignment || va>image || span>image-va || (rawsize && !pe_image_bounds(size,raw,rawsize))) return false;
        for(unsigned j=0;j<i;j++) {
            const unsigned char *t=p+sections+(size_t)j*40;
            uint32_t tv=pe_image_u32(t+12), ts=pe_image_u32(t+8), tr=pe_image_u32(t+16);
            uint32_t tspan=ts>tr?ts:tr;
            if(span && tspan && (uint64_t)va < (uint64_t)tv+tspan && (uint64_t)tv < (uint64_t)va+span) return false;
        }
    }
    *out=(struct pe_image_layout){p,size,(size_t)sections,image,headers,alignment,count}; return true;
}
