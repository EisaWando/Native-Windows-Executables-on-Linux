/* DrawState/GrayString use a temporary bitmap as the documented monochrome
 * mask. Application callbacks execute outside the bridge lock. */
typedef BOOL (__attribute__((ms_abi)) *pe_drawstate_proc)(HANDLE,intptr_t,uintptr_t,int,int);
typedef BOOL (__attribute__((ms_abi)) *pe_gray_proc)(HANDLE,intptr_t,int);
struct pe_draw_mask { HANDLE dc,bitmap,old_bitmap,old_font; int width,height; };
static bool pe_draw_mask_create(struct pe_draw_mask *m,HANDLE source,int width,int height)
{
    if(width<=0 || height<=0 || width>4096 || height>4096){SetLastError(87);return false;}
    memset(m,0,sizeof(*m));m->width=width;m->height=height;
    m->dc=CreateCompatibleDC(source);if(!m->dc)return false;
    m->bitmap=CreateCompatibleBitmap(m->dc,width,height);if(!m->bitmap)goto fail;
    m->old_bitmap=SelectObject(m->dc,m->bitmap);if(!m->old_bitmap)goto fail;
    HANDLE font=GetCurrentObject(source,PE_GDI_FONT);
    if(font)m->old_font=SelectObject(m->dc,font);
    SetBkMode(m->dc,1);SetTextColor(m->dc,0);
    struct pe_rect r={0,0,width,height};
    if(!FillRect(m->dc,&r,GetStockObject(0)))goto fail;
    return true;
fail:
    if(m->old_font)SelectObject(m->dc,m->old_font);
    if(m->old_bitmap)SelectObject(m->dc,m->old_bitmap);
    if(m->bitmap)DeleteObject(m->bitmap);
    if(m->dc)DeleteDC(m->dc);
    memset(m,0,sizeof(*m));return false;
}
static void pe_draw_mask_free(struct pe_draw_mask *m)
{
    if(m->old_font)SelectObject(m->dc,m->old_font);
    if(m->old_bitmap)SelectObject(m->dc,m->old_bitmap);
    if(m->bitmap)DeleteObject(m->bitmap);
    if(m->dc)DeleteDC(m->dc);
}
static bool pe_draw_ink(HANDLE dc,HANDLE brush,DWORD *color)
{
    PE_STATE_GUARD;
    struct pe_gui_object *d=pe_dc_lookup(dc);if(!d)return false;
    if(!brush){*color=d->u.dc.text_color;return true;}
    struct pe_gui_object *b=pe_gui_lookup(brush,PE_GDI_BRUSH);if(!b)return false;
    if(b->u.brush.style!=0){SetLastError(50);return false;}
    *color=b->u.brush.color;return true;
}
static BOOL pe_draw_mask_apply(HANDLE dc,struct pe_draw_mask *m,int x,int y,unsigned flags,DWORD ink)
{
    bool disabled=!!(flags&0x20),dither=!!(flags&0x10);
    DWORD light=GetSysColor(20),shadow=GetSysColor(16);
    if(disabled) for(int row=0;row<m->height;row++)for(int col=0;col<m->width;col++) {
        if(GetPixel(m->dc,col,row)==0xffffff)continue;
        if(PtVisible(dc,x+col+1,y+row+1))SetPixel(dc,x+col+1,y+row+1,light);
    }
    for(int row=0;row<m->height;row++)for(int col=0;col<m->width;col++) {
        DWORD value=GetPixel(m->dc,col,row);
        if(value==0xffffff || (dither && ((x+col+y+row)&1)))continue;
        if(PtVisible(dc,x+col,y+row))SetPixel(dc,x+col,y+row,disabled?shadow:ink);
    }
    return 1;
}
__attribute__((ms_abi)) BOOL GrayStringW(HANDLE dc,HANDLE brush,pe_gray_proc callback,intptr_t data,int count,int x,int y,int width,int height)
{
    if(count < -1 || (!callback && !data)){SetLastError(87);return 0;}
    DWORD ink;if(!pe_draw_ink(dc,brush,&ink))return 0;
    if(!brush)ink=GetSysColor(17);
    int n=count;
    if(!callback && n<=0)n=lstrlenW((const uint16_t *)data);
    if(!callback && (!width || !height)){
        struct pe_text_size z;if(!GetTextExtentPoint32W(dc,(const uint16_t *)data,n,&z))return 0;
        if(!width)width=z.x;if(!height)height=z.y;
    }
    struct pe_draw_mask m;if(!pe_draw_mask_create(&m,dc,width,height))return 0;
    BOOL ok;
    if(callback) ok=callback(m.dc,data,count);
    else {struct pe_rect r={0,0,width,height};ok=DrawTextW(m.dc,(const uint16_t *)data,n,&r,0x20|0x800)!=0;}
    if(ok || (callback && count==-1))ok=pe_draw_mask_apply(dc,&m,x,y,0x10,ink);
    pe_draw_mask_free(&m);return ok;
}
__attribute__((ms_abi)) BOOL DrawStateW(HANDLE dc,HANDLE brush,pe_drawstate_proc callback,intptr_t data,uintptr_t wdata,int x,int y,int width,int height,unsigned flags)
{
    unsigned type=flags&15u;
    if(type>4 || (flags&~0x86bfu) || (!data && type!=0) || (type==0 && !callback)){SetLastError(87);return 0;}
    DWORD ink;if(!pe_draw_ink(dc,(flags&0x80)?brush:NULL,&ink))return 0;
    if ((flags&0x600) && type!=2) { SetLastError(87); return 0; }
    if((type==3 || type==4) && (!width || !height)) {
        PE_STATE_GUARD;
        struct pe_gui_object *o=pe_gui_lookup((HANDLE)data,0); if(!o)return 0;
        if(type==4 && o->type!=PE_GDI_BITMAP){SetLastError(6);return 0;}
        if(type==3 && o->type!=PE_GUI_ICON && o->type!=PE_GUI_CURSOR){SetLastError(6);return 0;}
        struct pe_raster *r=pe_raster_resource(o);if(!r)return 0;
        if(!width)width=r->width;if(!height)height=r->height;
    }
    if((type==1 || type==2) && (!width || !height)){
        int n=wdata?(int)wdata:lstrlenW((const uint16_t *)data);struct pe_text_size z;
        if(n<0||!GetTextExtentPoint32W(dc,(const uint16_t *)data,n,&z))return 0;
        if(!width)width=z.x;if(!height)height=z.y;
    }
    if((type==1 || type==2) && !(flags&0xb0)) {
        int n=wdata?(int)wdata:lstrlenW((const uint16_t *)data);
        struct pe_rect r={x,y,x+width,y+height};
        return n>=0 && DrawTextW(dc,(const uint16_t *)data,n,&r,0x20|(type==1?0x800:0)|((flags&0x8000)?2:0)|((flags&0x200)?0x100000:0)|((flags&0x400)?0x200000:0))!=0;
    }
    if(type==3 && !(flags&0xb0))
        return DrawIconEx(dc,x,y,(HANDLE)data,width,height,0,NULL,3);
    struct pe_draw_mask m;if(!pe_draw_mask_create(&m,dc,width,height))return 0;
    BOOL ok=0;
    if(type==0 && !(flags&0xb0)) {
        DWORD color; int bk;
        { PE_STATE_GUARD; struct pe_gui_object *d=pe_dc_lookup(dc); if(!d){pe_draw_mask_free(&m);return 0;} color=d->u.dc.text_color; bk=d->u.dc.background_mode; }
        SetTextColor(m.dc,color);SetBkMode(m.dc,bk);
        /* Preserve the destination as the callback background, including white. */
        for(int row=0;row<height;row++)for(int col=0;col<width;col++) {
            DWORD c=GetPixel(dc,x+col,y+row);
            if(c!=UINT32_MAX)SetPixel(m.dc,col,row,c);
        }
    }
    if(type==0) ok=callback(m.dc,data,wdata,width,height);
    else if(type==1 || type==2){
        int n=wdata?(int)wdata:lstrlenW((const uint16_t *)data);
        struct pe_rect r={0,0,width,height};DWORD format=0x20 | (type==1?0x800:0) | ((flags&0x8000)?2:0) | ((flags&0x200)?0x100000:0) | ((flags&0x400)?0x200000:0);
        ok=n>=0 && DrawTextW(m.dc,(const uint16_t *)data,n,&r,format)!=0;
    } else if(type==3) ok=DrawIconEx(m.dc,0,0,(HANDLE)data,width,height,0,NULL,3);
    else {
        struct pe_bitmap info;
        if (GetObjectW((HANDLE)data,sizeof(info),&info)==sizeof(info)) {
            HANDLE source=CreateCompatibleDC(NULL);
            if(source){HANDLE old=SelectObject(source,(HANDLE)data);
                if(old){ok=StretchBlt(m.dc,0,0,width,height,source,0,0,info.width,info.height,0x00cc0020);SelectObject(source,old);}
                DeleteDC(source);}
        }
    }
    if(ok && (type==0 || type==4) && !(flags&0xb0)) {
        for(int row=0;row<height;row++)for(int col=0;col<width;col++)
            if(PtVisible(dc,x+col,y+row))SetPixel(dc,x+col,y+row,GetPixel(m.dc,col,row));
    } else if(ok) ok=pe_draw_mask_apply(dc,&m,x,y,flags,ink);
    pe_draw_mask_free(&m);return ok;
}
