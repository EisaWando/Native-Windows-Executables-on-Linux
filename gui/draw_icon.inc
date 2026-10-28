/* DrawIconEx retains the independent DIB XOR and AND planes. Legacy icons
 * require both planes: a transparent mask bit can still carry an XOR pixel. */
__attribute__((ms_abi)) BOOL PtVisible(HANDLE,int,int);
struct pe_icon_planes { uint32_t *pixels; unsigned char *mask; int width,height; bool alpha; };
static void pe_icon_planes_free(struct pe_icon_planes *p)
{
    if (p->pixels) raw_syscall3(SYS_munmap,(long)p->pixels,(size_t)p->width*p->height*4,0);
    if (p->mask) raw_syscall3(SYS_munmap,(long)p->mask,(size_t)p->width*p->height,0);
}
static bool pe_icon_planes_read(struct pe_gui_object *o,struct pe_icon_planes *out)
{
    struct pe_raster *r=pe_raster_resource(o); if (!r) return false;
    out->width=r->width;out->height=r->height;size_t n=(size_t)r->width*r->height;
    out->pixels=pe_raw_map(n*4);out->mask=pe_raw_map(n);
    if (!out->pixels || !out->mask) { SetLastError(8);pe_icon_planes_free(out);return false; }
    if (!o->image.data || !o->image.bytes) {
        for(size_t i=0;i<n;i++){out->pixels[i]=r->pixels[i];out->mask[i]=!(r->pixels[i]>>24);if(r->pixels[i]>>24)out->alpha=true;}
        return true;
    }
    size_t bytes=o->image.bytes;unsigned char *data=pe_raw_map(bytes);
    if (!data) {SetLastError(8);pe_icon_planes_free(out);return false;}
    bool ok=false;
    if (!diagnostic_image_read(o->image.data,data,bytes) || bytes<40) goto done;
    uint32_t header,compression,used,masks[4]={0xff0000,0xff00,0xff,0};int32_t w,h;uint16_t bpp;
    memcpy(&header,data,4);memcpy(&w,data+4,4);memcpy(&h,data+8,4);memcpy(&bpp,data+14,2);
    memcpy(&compression,data+16,4);memcpy(&used,data+32,4);
    if(header<40||header>bytes||w!=r->width||h==INT32_MIN||!h||((h<0?-h:h)/2)!=r->height||
       !(bpp==1||bpp==4||bpp==8||bpp==16||bpp==24||bpp==32)||!(compression==0||compression==3||compression==6))goto done;
    bool top=h<0;size_t off=header;
    if(bpp==16){masks[0]=0x7c00;masks[1]=0x3e0;masks[2]=31;}
    if(compression){unsigned m=compression==6?4:3;if(bytes<40+m*4||(header!=40&&header<40+m*4))goto done;
        memcpy(masks,data+40,m*4);if(header==40)off+=m*4;if(header>=56)memcpy(&masks[3],data+52,4);}
    unsigned colors=bpp<=8?(used?used:1u<<bpp):0;
    if ((bpp<=8 && colors>(1u<<bpp)) || off>bytes || colors>(bytes-off)/4)goto done;
    size_t palette=off;off+=(size_t)colors*4;
    size_t stride=(((size_t)w*bpp+31)/32)*4,maskstride=((w+31u)/32)*4;
    if(off>bytes || (size_t)r->height>(bytes-off)/stride)goto done;
    size_t moff=off+stride*r->height;
    bool mask_present=moff<=bytes && (size_t)r->height<=(bytes-moff)/maskstride;
    for(int y=0;y<r->height;y++)for(int x=0;x<r->width;x++){
        const unsigned char *row=data+off+(size_t)(top?y:r->height-1-y)*stride;
        uint32_t pixel=0,c;
        if(bpp<=8){unsigned i=bpp==8?row[x]:bpp==4?((row[x/2]>>(x&1?0:4))&15):((row[x/8]>>(7-x%8))&1);
            if(i>=colors)goto done;memcpy(&c,data+palette+i*4,4);c=(c&0xffffff)|0xff000000u;}
        else if(bpp==24){const unsigned char *q=row+x*3;c=0xff000000u|((uint32_t)q[2]<<16)|((uint32_t)q[1]<<8)|q[0];}
        else {memcpy(&pixel,row+(size_t)x*(bpp/8),bpp/8);c=(pe_dib_component(pixel,masks[0])<<16)|(pe_dib_component(pixel,masks[1])<<8)|pe_dib_component(pixel,masks[2]);
            unsigned a=masks[3]?pe_dib_component(pixel,masks[3]):bpp==32?pixel>>24:255;
            if(bpp==32 && a)out->alpha=true;c|=a<<24;}
        size_t i=(size_t)y*r->width+x;out->pixels[i]=c;
        out->mask[i]=mask_present && (data[moff+(size_t)(top?y:r->height-1-y)*maskstride+x/8]&(128u>>(x%8)));
    }
    ok=true;
done:
    raw_syscall3(SYS_munmap,(long)data,bytes,0);
    if(!ok){pe_icon_planes_free(out);SetLastError(1812);}return ok;
}
__attribute__((ms_abi)) BOOL DrawIconEx(HANDLE dc,int x,int y,HANDLE icon,int cx,int cy,unsigned step,HANDLE brush,unsigned flags)
{
    (void)step;
    if(cx<0||cy<0||!(flags&3)||(flags&~0x1fu)){SetLastError(87);return 0;}
    struct pe_icon_planes p={0}; DWORD background=0;
    {
        PE_STATE_GUARD;
        if(!pe_dc_lookup(dc))return 0;
        struct pe_gui_object *o=pe_gui_lookup(icon,0);
        if(!o || (o->type!=PE_GUI_ICON && o->type!=PE_GUI_CURSOR)){SetLastError(6);return 0;}
        if(brush){struct pe_gui_object *b=pe_gui_lookup(brush,PE_GDI_BRUSH);if(!b)return 0;
            if(b->u.brush.style!=0){SetLastError(50);return 0;}background=b->u.brush.color;}
        if(!pe_icon_planes_read(o,&p))return 0;
    }
    if(!cx)cx=(flags&8)?GetSystemMetrics(11):p.width;
    if(!cy)cy=(flags&8)?GetSystemMetrics(12):p.height;
    if(cx<1||cy<1||cx>4096||cy>4096||x>INT32_MAX-cx||y>INT32_MAX-cy){pe_icon_planes_free(&p);SetLastError(87);return 0;}
    uint32_t *staged=pe_raw_map((size_t)cx*cy*4);if(!staged){pe_icon_planes_free(&p);SetLastError(8);return 0;}
    for(int j=0;j<cy;j++)for(int i=0;i<cx;i++){
        size_t src=(size_t)((int64_t)j*p.height/cy)*p.width+(int64_t)i*p.width/cx;
        uint32_t image=p.pixels[src],old=brush?background:GetPixel(dc,x+i,y+j);
        uint32_t result;
        if((flags&3)==1)result=p.mask[src]?0xffffff:0;
        else if((flags&3)==2)result=pe_color_rgb(image);
        else if(p.alpha)result=pe_color_rgb(pe_iml_mix(image,0xff000000u|pe_color_rgb(old),image>>24));
        else result=(old & (p.mask[src]?0xffffff:0)) ^ pe_color_rgb(image);
        staged[(size_t)j*cx+i]=result;
    }
    for(int j=0;j<cy;j++)for(int i=0;i<cx;i++)
        if(PtVisible(dc,x+i,y+j)) SetPixel(dc,x+i,y+j,staged[(size_t)j*cx+i]);
    raw_syscall3(SYS_munmap,(long)staged,(size_t)cx*cy*4,0);pe_icon_planes_free(&p);return 1;
}
