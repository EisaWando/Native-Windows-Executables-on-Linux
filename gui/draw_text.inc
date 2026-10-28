/* DrawText layout. ANSI strings use the bridge's UTF-8 ANSI profile. */
struct pe_drawtext_params { uint32_t size; int32_t tab_length,left_margin,right_margin; uint32_t drawn; };
struct pe_draw_line { unsigned begin,length; int width; };
static int pe_draw_width(HANDLE dc,const uint16_t *s,int n)
{ struct pe_text_size z; return GetTextExtentPoint32W(dc,s,n,&z)?z.x:-1; }
static BOOL pe_draw_text_output(HANDLE dc,int x,int y,const struct pe_rect *clip,const uint16_t *s,unsigned n)
{
    unsigned type;
    { PE_STATE_GUARD; struct pe_gui_object *d=pe_dc_lookup(dc); if (!d) return 0; type=d->type; }
    if (type==PE_GDI_MEMDC) return pe_text_raster(dc,x,y,clip,s,n);
    for (unsigned pos=0;pos<n;) {
        unsigned take=n-pos>128?128:n-pos;
        if (pos+take<n && s[pos+take-1]>=0xd800 && s[pos+take-1]<=0xdbff) take--;
        if (!take || !ExtTextOutW(dc,x,y,clip?4:0,clip,s+pos,take,NULL)) return 0;
        int advance=pe_draw_width(dc,s+pos,take); if (advance<0) return 0;
        x+=advance;pos+=take;
    }
    return 1;
}
static int pe_draw_text(HANDLE dc,const uint16_t *input,int count,struct pe_rect *rect,DWORD flags,struct pe_drawtext_params *params)
{
    const DWORD supported=0x00000001u|0x00000002u|0x00000004u|0x00000008u|0x00000010u|0x00000020u|
        0x00000040u|0x00000100u|0x00000400u|0x00000800u|0x00004000u|0x00008000u|0x00100000u|0x00200000u;
    if (!rect || count < -1 || (!input && count)) { SetLastError(87); return 0; }
    if (flags & ~supported || ((flags&3)==3)) { SetLastError(50); return 0; }
    if (params && params->size!=sizeof(*params)) { SetLastError(87); return 0; }
    if (count==-1) count=lstrlenW(input);
    if (count<0 || count>131072) { SetLastError(87); return 0; }
    struct pg_text_metrics tm;
    if (!GetTextMetricsW(dc,&tm) || tm.height<=0) return 0;
    if (!count) { if (params) params->drawn=0; return 0; }
    size_t cap=(size_t)count*32+16;
    uint16_t *text=pe_raw_map(cap*sizeof(*text));
    struct pe_draw_line *lines=pe_raw_map((cap+1u)*sizeof(*lines));
    unsigned char *under=pe_raw_map(cap);
    if (!text || !lines || !under) { if(text)raw_syscall3(SYS_munmap,(long)text,cap*2,0); if(lines)raw_syscall3(SYS_munmap,(long)lines,(cap+1u)*sizeof(*lines),0); if(under)raw_syscall3(SYS_munmap,(long)under,cap,0);SetLastError(8);return 0; }
    unsigned used=0,column=0; bool underline_next=false;
    for (int i=0;i<count;i++) {
        uint16_t ch=input[i];
        if (!(flags&0x800) && ch=='&') { if (i+1<count && input[i+1]=='&') i++; else { underline_next=true; continue; } }
        if (ch=='\r' || ch=='\n') {
            if (ch=='\r' && i+1<count && input[i+1]=='\n') i++;
            if (flags&0x20) ch=' '; else { text[used++]='\n';column=0;continue; }
        }
        if (ch=='\t' && (flags&0x40)) {
            unsigned tab=params && params->tab_length>0 && params->tab_length<=32 ? params->tab_length : 8;
            unsigned spaces=tab-column%tab; while(spaces--) {text[used++]=' ';column++;} continue;
        }
        if (ch=='\t') ch=' ';
        under[used]=underline_next; underline_next=false; text[used++]=ch;column++;
    }
    int margin_l=params?params->left_margin:0,margin_r=params?params->right_margin:0;
    int width=rect->right-rect->left-margin_l-margin_r; if (width<0) width=0;
    unsigned linecount=0,pos=0;int maxwidth=0,result=0;
    while (pos<used) {
        unsigned start=pos,end=pos;while(end<used && text[end]!='\n')end++;
        unsigned remaining=end-start;
        do {
            unsigned take=remaining;
            if ((flags&0x10) && !(flags&0x20) && remaining) {
                struct pe_text_size z; int fit=0;
                if (!GetTextExtentExPointW(dc,text+start,remaining,width,&fit,NULL,&z)) goto done;
                if (fit<(int)remaining) {
                    if (fit<1) fit=1;
                    take=fit;
                    unsigned space=take;
                    while(space>0 && text[start+space-1]!=' ' && text[start+space-1]!='\t') space--;
                    if (space) take=space;
                    else { while(take<remaining && text[start+take]!=' ') take++; }
                }
            }
            unsigned visible=take; while (visible && text[start+visible-1]==' ') visible--;
            int measured=pe_draw_width(dc,text+start,visible); if (measured<0) goto done;
            lines[linecount++]=(struct pe_draw_line){start,visible,measured};
            if (measured>maxwidth) maxwidth=measured;
            start+=take; remaining-=take;
            while (remaining && text[start]==' ') { start++;remaining--; }
        } while (remaining);
        pos=end<used?end+1:end;
        if (!remaining && end==used && !linecount) break;
    }
    if (!linecount) { lines[linecount++]=(struct pe_draw_line){0,0,0}; }
    if (flags&0x400) { rect->right=rect->left+margin_l+maxwidth+margin_r; rect->bottom=rect->top+linecount*tm.height; if (params) params->drawn=count; result=linecount*tm.height; goto done; }
    int top=rect->top,total_height=(int)linecount*tm.height;
    if ((flags&0x20) && (flags&4)) top=rect->top+(rect->bottom-rect->top-total_height)/2;
    else if ((flags&0x20) && (flags&8)) top=rect->bottom-total_height;
    struct pe_rect clip={rect->left+margin_l,rect->top,rect->right-margin_r,rect->bottom};
    uint16_t *line=pe_raw_map((used+4u)*sizeof(*line)); if (!line) { SetLastError(8); goto done; }
    for (unsigned i=0;i<linecount;i++) {
        struct pe_draw_line l=lines[i];unsigned len=l.length; memcpy(line,text+l.begin,len*2);int measured=l.width; bool ellipsized=false;
        if ((flags&0xc000) && measured>width) {
            ellipsized=true; bool path_done=false;
            static const uint16_t dots[]={'.','.','.'};int dotswidth=pe_draw_width(dc,dots,3);
            if (dotswidth<0) { raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done; }
            if (flags&0x4000) {
                const uint16_t *original=text+l.begin;
                unsigned tail=len;while(tail && original[tail-1]!='\\' && original[tail-1]!='/')tail--;
                if (tail) {
                    tail--; unsigned suffix=len-tail;
                    int suffix_width=pe_draw_width(dc,original+tail,suffix);
                    if (suffix_width<0) {raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
                    if (suffix_width+dotswidth>width) {
                        unsigned lo=0,hi=suffix;
                        while(lo<hi) {
                            unsigned mid=lo+(hi-lo)/2;int sw=pe_draw_width(dc,original+tail+mid,suffix-mid);
                            if(sw<0){raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
                            if(sw+dotswidth>width) lo=mid+1; else hi=mid;
                        }
                        tail+=lo;suffix-=lo;suffix_width=pe_draw_width(dc,original+tail,suffix);
                    }
                    int prefix=0;struct pe_text_size z;
                    int available=width-dotswidth-suffix_width;if(available<0)available=0;
                    if (!GetTextExtentExPointW(dc,original,tail,available,&prefix,NULL,&z)) {raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
                    memcpy(line,original,(size_t)prefix*2);unsigned k=prefix;
                    line[k++]='.';line[k++]='.';line[k++]='.';
                    memcpy(line+k,original+tail,(size_t)suffix*2);len=k+suffix;path_done=true;
                }
            }
            if (!path_done) {
                int fit=0;struct pe_text_size z;
                if (!GetTextExtentExPointW(dc,line,len,width>dotswidth?width-dotswidth:0,&fit,NULL,&z)) {raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
                len=fit;line[len++]='.';line[len++]='.';line[len++]='.';
            }
            measured=pe_draw_width(dc,line,len);if(measured<0){raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
        }
        int x=rect->left+margin_l;
        if ((flags&3)==1) x+=(width-measured)/2; else if ((flags&3)==2) x+=width-measured;
        if (len && !(flags&0x200000u) && !pe_draw_text_output(dc,x,top+(int)i*tm.height,(flags&0x100)?NULL:&clip,line,len)) {raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}
        if (!ellipsized && !(flags&0x100000u)) for (unsigned j=0;j<len;j++) if (under[l.begin+j]) {
            int left=pe_draw_width(dc,line,j), cw=pe_draw_width(dc,line+j,1);
            if (left<0 || cw<0) { raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done; }
            DWORD color; { PE_STATE_GUARD; struct pe_gui_object *d=pe_dc_lookup(dc);if(!d){raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);goto done;}color=d->u.dc.text_color; }
            int yy=top+(int)i*tm.height+tm.ascent+1;
            for (int xx=x+left;xx<x+left+cw;xx++)
                if ((flags&0x100) || (xx>=clip.left && xx<clip.right && yy>=clip.top && yy<clip.bottom)) SetPixel(dc,xx,yy,color);
        }
    }
    raw_syscall3(SYS_munmap,(long)line,(used+4u)*2,0);
    if (params) params->drawn=count;
    result=((flags&0x20) && (flags&(4|8))) ? top+total_height-rect->top : total_height;
done:
    raw_syscall3(SYS_munmap,(long)text,cap*2,0);
    raw_syscall3(SYS_munmap,(long)under,cap,0);
    raw_syscall3(SYS_munmap,(long)lines,(cap+1u)*sizeof(*lines),0);
    return result;
}
__attribute__((ms_abi)) int DrawTextW(HANDLE dc,const uint16_t *text,int count,struct pe_rect *rect,DWORD flags)
{ return pe_draw_text(dc,text,count,rect,flags,NULL); }
__attribute__((ms_abi)) int DrawTextExW(HANDLE dc,uint16_t *text,int count,struct pe_rect *rect,DWORD flags,struct pe_drawtext_params *params)
{ return pe_draw_text(dc,text,count,rect,flags,params); }
__attribute__((ms_abi)) int DrawTextA(HANDLE dc,const char *text,int count,struct pe_rect *rect,DWORD flags)
{
    if (count < -1 || (!text && count)) { SetLastError(87); return 0; }
    if (count==-1) count=cstring_length(text);
    if (!count) return 0;
    int units=MultiByteToWideChar(65001,0,text,count,NULL,0); if (!units) return 0;
    uint16_t *wide=pe_raw_map((size_t)units*2);if(!wide){SetLastError(8);return 0;}
    if (!MultiByteToWideChar(65001,0,text,count,wide,units)){raw_syscall3(SYS_munmap,(long)wide,(size_t)units*2,0);return 0;}
    int result=DrawTextW(dc,wide,units,rect,flags);raw_syscall3(SYS_munmap,(long)wide,(size_t)units*2,0);return result;
}
