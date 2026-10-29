/* Class names are stored as UTF-16 by the window-class registry. */
__attribute__((ms_abi)) int GetClassNameW(HANDLE hwnd,uint16_t *out,int capacity)
{
    if(!out || capacity<=0) { SetLastError(87);return 0; }
    PE_STATE_GUARD; struct pe_window *w=pe_window_lookup(hwnd,true);
    if(!w) return 0;
    int n=0; while(n<capacity-1 && w->cls->name[n]) { out[n]=w->cls->name[n];n++; }
    out[n]=0;return n;
}
__attribute__((ms_abi)) int GetClassNameA(HANDLE hwnd,char *out,int capacity)
{
    if(!out || capacity<=0) { SetLastError(87);return 0; }
    uint16_t wide[256]; if(!GetClassNameW(hwnd,wide,256)) return 0;
    int length=lstrlenW(wide), chars=length;
    while(chars>0) {
        if(chars<length && wide[chars-1]>=0xd800 && wide[chars-1]<=0xdbff) chars--;
        int bytes=WideCharToMultiByte(65001,0,wide,chars,NULL,0,NULL,NULL);
        if(bytes>0 && bytes<capacity) {
            if(!WideCharToMultiByte(65001,0,wide,chars,out,capacity-1,NULL,NULL)) return 0;
            out[bytes]=0;return bytes;
        }
        chars--;
    }
    out[0]=0;return 0;
}
