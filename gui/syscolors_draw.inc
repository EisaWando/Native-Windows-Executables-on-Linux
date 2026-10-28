/* Process-local system colors. System brushes are stable, shared GUI objects. */
static HANDLE pe_system_brushes[31];
__attribute__((ms_abi)) HANDLE GetSysColorBrush(int index)
{
    if (index<0 || index>=31) { SetLastError(87); return NULL; }
    PE_STATE_GUARD;
    if (!pe_system_brushes[index]) {
        struct pe_gui_object *b=pe_gui_new(PE_GDI_BRUSH);
        if (!b) return NULL;
        b->shared=1; b->u.brush.style=0; b->u.brush.color=pe_system_colors[index];
        pe_system_brushes[index]=pe_gui_handle(b);
    }
    return pe_system_brushes[index];
}
static BOOL __attribute__((ms_abi)) pe_system_color_notify(HANDLE hwnd,intptr_t unused)
{
    (void)unused;
    InvalidateRect(hwnd,NULL,1);
    SendMessageW(hwnd,0x15,0,0); /* WM_SYSCOLORCHANGE */
    return 1;
}
__attribute__((ms_abi)) BOOL SetSysColors(int count,const int *indexes,const DWORD *colors)
{
    if (count<0 || (count && (!indexes || !colors))) { SetLastError(87); return 0; }
    for (int i=0;i<count;i++) if (indexes[i]<0 || indexes[i]>=31) { SetLastError(87); return 0; }
    {
        PE_STATE_GUARD;
        for (int i=0;i<count;i++) {
            int n=indexes[i]; pe_system_colors[n]=colors[i];
            if (pe_system_brushes[n]) {
                struct pe_gui_object *b=pe_gui_lookup(pe_system_brushes[n],PE_GDI_BRUSH);
                if (b) b->u.brush.color=colors[i];
            }
        }
    }
    if (count) EnumWindows(pe_system_color_notify,0);
    return 1;
}
__attribute__((ms_abi)) int FrameRect(HANDLE dc,const struct pe_rect *r,HANDLE brush)
{
    if (!r) { SetLastError(87); return 0; }
    { PE_STATE_GUARD; if (!pe_dc_lookup(dc)) return 0; }
    if (r->right<=r->left || r->bottom<=r->top) return 1;
    struct pe_rect a={r->left,r->top,r->right,r->top+1};
    if (!FillRect(dc,&a,brush)) return 0;
    a=(struct pe_rect){r->left,r->bottom-1,r->right,r->bottom}; if (!FillRect(dc,&a,brush)) return 0;
    a=(struct pe_rect){r->left,r->top,r->left+1,r->bottom}; if (!FillRect(dc,&a,brush)) return 0;
    a=(struct pe_rect){r->right-1,r->top,r->right,r->bottom}; return FillRect(dc,&a,brush);
}
__attribute__((ms_abi)) BOOL InvertRect(HANDLE dc,const struct pe_rect *r)
{
    if (!r) { SetLastError(87); return 0; }
    { PE_STATE_GUARD; if (!pe_dc_lookup(dc)) return 0; }
    if (r->right<=r->left || r->bottom<=r->top) return 1;
    PE_STATE_GUARD;
    struct pe_gui_object *d=pe_dc_lookup(dc); if (!d) return 0;
    if (d->type!=PE_GDI_MEMDC) { SetLastError(50); return 0; }
    struct pe_gui_object *b=pe_gui_lookup(d->u.dc.bitmap,PE_GDI_BITMAP);
    struct pe_raster *dst=b?pe_raster_resource(b):NULL; if (!dst) return 0;
    int l=r->left<0?0:r->left,t=r->top<0?0:r->top;
    int right=r->right>dst->width?dst->width:r->right,bottom=r->bottom>dst->height?dst->height:r->bottom;
    for (int y=t;y<bottom;y++) for (int x=l;x<right;x++)
        if (!d->clip_active || pe_region_contains(&d->region,x,y)) pe_raster_set(dst,x,y,pe_raster_get(dst,x,y)^0x00ffffffu);
    return 1;
}
__attribute__((ms_abi)) BOOL DrawFocusRect(HANDLE dc,const struct pe_rect *r)
{
    if (!r) { SetLastError(87); return 0; }
    { PE_STATE_GUARD; if (!pe_dc_lookup(dc)) return 0; }
    if (r->right<=r->left || r->bottom<=r->top) return 1;
    PE_STATE_GUARD;
    struct pe_gui_object *d=pe_dc_lookup(dc); if (!d) return 0;
    if (d->type!=PE_GDI_MEMDC) { SetLastError(50); return 0; }
    struct pe_gui_object *b=pe_gui_lookup(d->u.dc.bitmap,PE_GDI_BITMAP);
    struct pe_raster *dst=b?pe_raster_resource(b):NULL; if (!dst) return 0;
    int l=r->left<0?0:r->left,t=r->top<0?0:r->top;
    int right=r->right>dst->width?dst->width:r->right,bottom=r->bottom>dst->height?dst->height:r->bottom;
    for (int y=t;y<bottom;y++) for (int x=l;x<right;x++)
        if ((x==r->left || x==r->right-1 || y==r->top || y==r->bottom-1) && ((x+y)&1)==0 &&
            (!d->clip_active || pe_region_contains(&d->region,x,y))) pe_raster_set(dst,x,y,pe_raster_get(dst,x,y)^0x00ffffffu);
    return 1;
}

/* Classic, deterministic 3-D edge palette (COLOR_* constants). */
static BOOL pe_edge_side(HANDLE dc,int l,int t,int r,int b,int color)
{
    struct pe_rect rect={l,t,r,b};
    return FillRect(dc,&rect,GetSysColorBrush(color));
}
static BOOL pe_edge_diagonal(HANDLE dc,struct pe_rect r,unsigned flags,int color,int offset)
{
    int x0,y0,x1,y1;
    if ((flags&3)==3 || (flags&12)==12) { SetLastError(87); return 0; }
    if (flags&2) { x1=(flags&4)?r.right-1:r.left; y1=r.top+offset; x0=(flags&4)?r.left:r.right-1; y0=r.bottom-1; }
    else { x1=(flags&4)?r.right-1:r.left; y1=r.bottom-1-offset; x0=(flags&4)?r.left:r.right-1; y0=r.top; }
    int dx=x1>x0?x1-x0:x0-x1, sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0, sy=y0<y1?1:-1, error=dx+dy;
    DWORD c=GetSysColor(color);
    for (;;) { SetPixel(dc,x0,y0,c); if(x0==x1 && y0==y1) break;int e=2*error;if(e>=dy){error+=dy;x0+=sx;}if(e<=dx){error+=dx;y0+=sy;} }
    return 1;
}
__attribute__((ms_abi)) BOOL DrawEdge(HANDLE dc,struct pe_rect *r,unsigned edge,unsigned flags)
{
    if (!r || (edge&~15u) || ((edge&3)==3) || ((edge&12)==12) || (flags&~0xf81fu)) { SetLastError(87); return 0; }
    { PE_STATE_GUARD; if (!pe_dc_lookup(dc)) return 0; }
    struct pe_rect box=*r;int layers=0;
    for (int layer=0;layer<2;layer++) {
        unsigned raised=layer?4:1,sunken=layer?8:2;
        if (!(edge&(raised|sunken))) continue;
        bool up=!!(edge&raised);
        int tl=up?(layer?20:22):(layer?16:21);
        int br=up?(layer?16:21):(layer?20:22);
        if (flags&0x8000) { tl=up?5:6;br=up?6:5; }
        else if (flags&0x4000) { tl=br=16; }
        else if (flags&0x1000) { tl=up?20:16;br=up?16:20; }
        if (flags&0x10) { if (!pe_edge_diagonal(dc,box,flags,tl,layer)) return 0; }
        else {
            if ((flags&1) && !pe_edge_side(dc,box.left,box.top,box.left+1,box.bottom,tl)) return 0;
            if ((flags&2) && !pe_edge_side(dc,box.left,box.top,box.right,box.top+1,tl)) return 0;
            if ((flags&4) && !pe_edge_side(dc,box.right-1,box.top,box.right,box.bottom,br)) return 0;
            if ((flags&8) && !pe_edge_side(dc,box.left,box.bottom-1,box.right,box.bottom,br)) return 0;
        }
        if (flags&1) box.left++; if (flags&2) box.top++; if (flags&4) box.right--; if (flags&8) box.bottom--;
        layers++;
    }
    if ((flags&0x800) && box.left<box.right && box.top<box.bottom && !FillRect(dc,&box,GetSysColorBrush(15))) return 0;
    if ((flags&0x2000) && layers) *r=box;
    return 1;
}
static void pe_frame_pixel(HANDLE dc,int x,int y,DWORD color)
{ (void)SetPixel(dc,x,y,color); }
static void pe_frame_line(HANDLE dc,int x0,int y0,int x1,int y1,DWORD color)
{
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,err=dx+dy;
    for (;;) {pe_frame_pixel(dc,x0,y0,color);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
__attribute__((ms_abi)) BOOL DrawFrameControl(HANDLE dc,struct pe_rect *rect,unsigned type,unsigned state)
{
    if (!rect || type<1 || type>4 || (state&~0xff1fu)) { SetLastError(87); return 0; }
    unsigned style=state&0x1f;
    if ((type==1 && style>4) || (type==2 && style!=0 && style!=1 && style!=2 && style!=4) ||
        (type==3 && style!=0 && style!=1 && style!=2 && style!=3 && style!=5 && style!=8 && style!=16) ||
        (type==4 && style!=0 && style!=1 && style!=2 && style!=4 && style!=8 && style!=16)) { SetLastError(50); return 0; }
    { PE_STATE_GUARD; if (!pe_dc_lookup(dc)) return 0; }
    if (rect->right<=rect->left || rect->bottom<=rect->top) return 1;
    struct pe_rect box=*rect; bool pushed=!!(state&0x200), checked=!!(state&0x400), inactive=!!(state&0x100);
    DWORD ink=GetSysColor(inactive?17:18);
    if (type==4 && (style==0 || style==1 || style==2 || style==4 || style==8)) {
        if (!FillRect(dc,&box,GetSysColorBrush(5))) return 0;
        if (!DrawEdge(dc,&box,0x0a,0x200f | ((state&0x8000)?0x8000:0))) return 0;
    } else {
        if (!FillRect(dc,&box,GetSysColorBrush(15))) return 0;
        if (!DrawEdge(dc,&box,pushed?0x0a:0x05,0x200f | ((state&0x8000)?0x8000:0))) return 0;
    }
    int width=box.right-box.left,height=box.bottom-box.top;
    int cx=(box.left+box.right)/2,cy=(box.top+box.bottom)/2;
    if (pushed) {cx++;cy++;}
    int radius=(width<height?width:height)/3; if(radius<1) radius=1;
    if (type==4 && (style==1 || style==2 || style==4)) {
        int outer=(box.right-box.left<box.bottom-box.top?box.right-box.left:box.bottom-box.top)/2;
        for(int dy=-outer;dy<=outer;dy++) for(int dx=-outer;dx<=outer;dx++) {
            int rr=dx*dx+dy*dy;
            if (rr>outer*outer || rr<(outer-1)*(outer-1)) continue;
            pe_frame_pixel(dc,cx+dx,cy+dy,GetSysColor(dx+dy<0?16:20));
        }
    }
    if (type==4 && (style==0 || style==8) && checked) {
        pe_frame_line(dc,cx-radius,cy,cx-1,cy+radius,ink);
        pe_frame_line(dc,cx-1,cy+radius,cx+radius,cy-radius,ink);
    } else if (type==4 && (style==1 || style==2 || style==4) && checked) {
        for(int y=-radius/2;y<=radius/2;y++) for(int x=-radius/2;x<=radius/2;x++)
            if(x*x+y*y<=radius*radius/4) pe_frame_pixel(dc,cx+x,cy+y,ink);
    } else if (type==3 && style<=5) {
        unsigned dir=style==5?1:style;
        for(int i=0;i<=radius;i++) for(int j=-i;j<=i;j++) {
            int x=cx,y=cy;
            if(dir==0){x+=j;y+=i-radius/2;} else if(dir==1){x+=j;y+=radius/2-i;}
            else if(dir==2){x+=i-radius/2;y+=j;} else {x+=radius/2-i;y+=j;}
            pe_frame_pixel(dc,x,y,ink);
        }
    } else if (type==2 && (style==0 || style==4)) {
        int sign=style==4?-1:1;
        for(int i=0;i<=radius;i++) for(int j=-i;j<=i;j++) pe_frame_pixel(dc,cx+sign*(radius/2-i),cy+j,ink);
    } else if (type==2 && style==1) {
        pe_frame_line(dc,cx-radius,cy,cx-1,cy+radius,ink);pe_frame_line(dc,cx-1,cy+radius,cx+radius,cy-radius,ink);
    } else if (type==2 && style==2) {
        for(int y=-radius/2;y<=radius/2;y++)for(int x=-radius/2;x<=radius/2;x++)if(x*x+y*y<=radius*radius/4)pe_frame_pixel(dc,cx+x,cy+y,ink);
    } else if (type==1 && style==0) {
        pe_frame_line(dc,cx-radius,cy-radius,cx+radius,cy+radius,ink);pe_frame_line(dc,cx+radius,cy-radius,cx-radius,cy+radius,ink);
    } else if (type==1 && style==1) pe_frame_line(dc,cx-radius,cy+radius/2,cx+radius,cy+radius/2,ink);
    else if (type==1 && (style==2 || style==3)) {
        pe_frame_line(dc,cx-radius,cy-radius,cx+radius,cy-radius,ink);pe_frame_line(dc,cx-radius,cy-radius,cx-radius,cy+radius,ink);
        pe_frame_line(dc,cx+radius,cy-radius,cx+radius,cy+radius,ink);pe_frame_line(dc,cx-radius,cy+radius,cx+radius,cy+radius,ink);
    } else if (type==1 && style==4) {
        pe_frame_line(dc,cx-radius,cy-radius/2,cx-radius/2,cy-radius,ink);
        pe_frame_line(dc,cx-radius/2,cy-radius,cx+radius/2,cy-radius,ink);
        pe_frame_line(dc,cx+radius/2,cy-radius,cx+radius,cy-radius/2,ink);
        pe_frame_line(dc,cx+radius,cy-radius/2,cx,cy,ink);
        pe_frame_line(dc,cx,cy,cx,cy+radius/2,ink);
        pe_frame_pixel(dc,cx,cy+radius,ink);
    } else if ((type==3 && (style==8 || style==16))) {
        for(int i=0;i<3;i++) pe_frame_line(dc,box.right-1-i*3,box.bottom-1,box.right-1,box.bottom-1-i*3,ink);
    } else if ((type==4 && style!=0 && style!=1 && style!=2 && style!=4 && style!=8 && style!=16) ||
               (type==1 && style>4) || (type==2 && style!=0 && style!=1 && style!=2 && style!=4) ||
               (type==3 && style>5 && style!=8 && style!=16)) { SetLastError(50); return 0; }
    if (state&0x2000) *rect=box;
    return 1;
}
