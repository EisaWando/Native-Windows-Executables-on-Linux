/* FreeType glyph coverage for off-screen 32-bit DCs. The helper supplies
 * grayscale masks; the bridge blends them into the selected software bitmap. */
static bool pe_text_raster(HANDLE dc,int x,int y,const struct pe_rect *clip,const uint16_t *text,int count)
{
    struct pe_logfont font; DWORD fg,bg; int mode,extra;
    {
        PE_STATE_GUARD;
        struct pe_gui_object *d=pe_dc_lookup(dc);
        if (!d || d->type!=PE_GDI_MEMDC) { SetLastError(6); return false; }
        struct pe_gui_object *f=pe_gui_lookup(d->u.dc.font,PE_GDI_FONT);
        if (!f) return false;
        font=f->u.font; fg=pe_color_rgb(d->u.dc.text_color);
        bg=pe_color_rgb(d->u.dc.background); mode=d->u.dc.background_mode; extra=d->u.dc.character_extra;
    }
    struct pg_text_metrics metrics; if (!GetTextMetricsW(dc,&metrics)) return false;
    struct pe_ui *ui=pe_ui_current(); if (!ui) return false;
    int pen=x;
    for (int i=0;i<count;i++) {
        uint32_t cp=text[i];
        if (cp>=0xd800 && cp<=0xdbff && i+1<count && text[i+1]>=0xdc00 && text[i+1]<=0xdfff)
            cp=0x10000+((cp-0xd800)<<10)+(text[++i]-0xdc00);
        else if (cp>=0xd800 && cp<=0xdfff) cp=0xfffd;
        unsigned offset=0,total=0; bool first=true;
        do {
            struct pg_packet p={.op=PG_FONT_GLYPH,.a=cp,.b=offset,.c=font.height};
            int n=WideCharToMultiByte(65001,0,font.face,-1,p.text,PG_TEXT,NULL,NULL);
            if (!n) return false;
            p.length=n-1;
            if (!pe_ui_rpc(ui,&p)) return false;
            int width=p.a,height=p.b,left=p.c,top=p.d,advance=(int32_t)p.color,ascent=(int32_t)p.background;
            if (width<0 || height<0 || p.flags>65536 || p.length>PG_TEXT-1 || offset>p.flags || p.length>p.flags-offset) { SetLastError(13); return false; }
            total=p.flags;
            {
                PE_STATE_GUARD;
                struct pe_gui_object *d=pe_dc_lookup(dc),*b=d?pe_gui_lookup(d->u.dc.bitmap,PE_GDI_BITMAP):NULL;
                struct pe_raster *dst=b?pe_raster_resource(b):NULL;
                if (!dst) return false;
                if (first && mode==2) {
                    int a=pen<0?0:pen, c=pen+advance; if (c>dst->width) c=dst->width;
                    int t=y<0?0:y, bottom=y+metrics.height;
                    if (bottom>dst->height) bottom=dst->height;
                    for (int yy=t;yy<bottom;yy++) for (int xx=a;xx<c;xx++)
                        if ((!clip || (xx>=clip->left && xx<clip->right && yy>=clip->top && yy<clip->bottom)) &&
                            (!d->clip_active || pe_region_contains(&d->region,xx,yy))) pe_raster_set(dst,xx,yy,0xff000000u|bg);
                }
                for (unsigned j=0;j<p.length;j++) {
                    unsigned pos=offset+j; if (!width) break;
                    int xx=pen+left+(int)(pos%(unsigned)width), yy=y+ascent-top+(int)(pos/(unsigned)width);
                    if (xx<0 || yy<0 || xx>=dst->width || yy>=dst->height ||
                        (clip && (xx<clip->left || xx>=clip->right || yy<clip->top || yy>=clip->bottom)) ||
                        (d->clip_active && !pe_region_contains(&d->region,xx,yy))) continue;
                    unsigned alpha=(unsigned char)p.text[j]; if (!alpha) continue;
                    uint32_t old=pe_raster_get(dst,xx,yy), result=0xff000000u;
                    for (unsigned shift=0;shift<24;shift+=8)
                        result|=((((fg>>shift)&255)*alpha+((old>>shift)&255)*(255-alpha)+127)/255)<<shift;
                    pe_raster_set(dst,xx,yy,result);
                }
            }
            offset+=p.length;first=false;
            if (offset>=total) { pen+=advance+extra; break; }
            if (!p.length) { SetLastError(13); return false; }
        } while (offset<total);
    }
    return true;
}
