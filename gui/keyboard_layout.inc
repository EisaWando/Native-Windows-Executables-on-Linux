/* The bridge currently exposes one fixed US English keyboard layout. */
static const uint16_t pe_us_scan[128] = {
    [1]=0x1b,[2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]=0xbd,[13]=0xbb,[14]=8,[15]=9,[16]='Q',[17]='W',[18]='E',[19]='R',[20]='T',[21]='Y',
    [22]='U',[23]='I',[24]='O',[25]='P',[26]=0xdb,[27]=0xdd,[28]=13,[29]=0x11,[30]='A',
    [31]='S',[32]='D',[33]='F',[34]='G',[35]='H',[36]='J',[37]='K',[38]='L',[39]=0xba,
    [40]=0xde,[41]=0xc0,[42]=0xa0,[43]=0xdc,[44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',
    [49]='N',[50]='M',[51]=0xbc,[52]=0xbe,[53]=0xbf,[54]=0xa1,[55]=0x6a,[56]=0x12,[57]=0x20,
    [58]=0x14,[59]=0x70,[60]=0x71,[61]=0x72,[62]=0x73,[63]=0x74,[64]=0x75,[65]=0x76,
    [66]=0x77,[67]=0x78,[68]=0x79,[69]=0x90,[70]=0x91,[71]=0x67,[72]=0x68,[73]=0x69,
    [74]=0x6d,[75]=0x64,[76]=0x65,[77]=0x66,[78]=0x6b,[79]=0x61,[80]=0x62,[81]=0x63,
    [82]=0x60,[83]=0x6e,[87]=0x7a,[88]=0x7b
};
__attribute__((ms_abi)) HANDLE GetKeyboardLayout(DWORD tid)
{
    if(tid && raw_syscall3(SYS_tgkill,GetCurrentProcessId(),tid,0)<0) { SetLastError(87);return NULL; }
    return (HANDLE)(uintptr_t)0x04090409;
}
__attribute__((ms_abi)) BOOL GetKeyboardLayoutNameW(uint16_t *out)
{
    if(!out) { SetLastError(87);return 0; }
    static const uint16_t name[]={'0','0','0','0','0','4','0','9',0};
    memcpy(out,name,sizeof(name));return 1;
}
__attribute__((ms_abi)) BOOL GetKeyboardLayoutNameA(char *out)
{
    if(!out) { SetLastError(87);return 0; } memcpy(out,"00000409",9);return 1;
}
__attribute__((ms_abi)) unsigned MapVirtualKeyExW(unsigned code,unsigned type,HANDLE layout)
{
    if(layout && (uintptr_t)layout!=0x04090409) return 0;
    if(type==0 || type==4) {
        if(code==0x10) code=0xa0;
        for(unsigned i=1;i<128;i++) if(pe_us_scan[i]==code) return i;
        return 0;
    }
    if(type==1 || type==3) {
        if(code>=128) return 0; unsigned vk=pe_us_scan[code];
        if(type==1 && (vk==0xa0 || vk==0xa1)) return 0x10;return vk;
    }
    if(type==2) {
        if(code>='A' && code<='Z') return code;
        if(code>='0' && code<='9') return code;
        switch(code) { case 0x20:return ' ';case 0xbd:return '-';case 0xbb:return '=';case 0xdb:return '[';case 0xdd:return ']';case 0xdc:return '\\';case 0xba:return ';';case 0xde:return '\'';case 0xc0:return '`';case 0xbc:return ',';case 0xbe:return '.';case 0xbf:return '/';default:return 0; }
    }
    return 0;
}
__attribute__((ms_abi)) unsigned MapVirtualKeyW(unsigned code,unsigned type)
{ return MapVirtualKeyExW(code,type,NULL); }
__attribute__((ms_abi)) unsigned MapVirtualKeyA(unsigned code,unsigned type)
{ return MapVirtualKeyW(code,type); }
__attribute__((ms_abi)) unsigned MapVirtualKeyExA(unsigned code,unsigned type,HANDLE layout)
{ return MapVirtualKeyExW(code,type,layout); }
