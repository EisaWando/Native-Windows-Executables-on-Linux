/* x64 runtime function entries, as specified by the PE exception directory. */
struct pe_runtime_function { uint32_t begin, end, unwind; };
struct pe_function_table {
    struct pe_function_table *next;
    struct pe_runtime_function *entries;
    uint64_t base;
    DWORD count;
};
static struct pe_function_table *pe_function_tables;
static struct pe_runtime_function *pe_function_search(struct pe_runtime_function *entries, DWORD count, uint64_t base, uint64_t pc)
{
    if (pc<base || pc-base>UINT32_MAX) return NULL;
    uint32_t offset=(uint32_t)(pc-base);
    DWORD low=0,high=count;
    while(low<high) {
        DWORD mid=low+(high-low)/2;
        struct pe_runtime_function entry;
        if(!diagnostic_read((uintptr_t)&entries[mid],&entry,sizeof(entry))) return NULL;
        if(offset<entry.begin) high=mid;
        else if(offset>=entry.end) low=mid+1;
        else return &entries[mid];
    }
    return NULL;
}
__attribute__((ms_abi)) BOOL RtlAddFunctionTable(struct pe_runtime_function *entries,DWORD count,uint64_t base)
{
    if(!entries || !count || !base) return 0;
    struct pe_function_table *table=pe_raw_map(sizeof(*table)); if(!table) return 0;
    table->entries=entries;table->count=count;table->base=base;
    PE_STATE_GUARD; table->next=pe_function_tables;pe_function_tables=table;return 1;
}
__attribute__((ms_abi)) BOOL RtlDeleteFunctionTable(struct pe_runtime_function *entries)
{
    PE_STATE_GUARD;struct pe_function_table **link=&pe_function_tables;
    while(*link && (*link)->entries!=entries) link=&(*link)->next;
    if(!*link) return 0;
    struct pe_function_table *table=*link;*link=table->next;
    raw_syscall3(SYS_munmap,(long)table,sizeof(*table),0);return 1;
}
__attribute__((ms_abi)) struct pe_runtime_function *RtlLookupFunctionEntry(uint64_t pc,uint64_t *image_base,void *history)
{
    (void)history;
    if(image_base) *image_base=0;
    if(pc>=pe_main_base && pc<pe_main_end) {
        uint32_t nt,dir[2];
        if(diagnostic_image_read(pe_main_base+0x3c,&nt,4) &&
           diagnostic_image_read(pe_main_base+nt+24+0x88,dir,8) &&
           dir[0] && dir[1]>=sizeof(struct pe_runtime_function) &&
           dir[0]<(pe_main_end-pe_main_base) && dir[1]<=pe_main_end-pe_main_base-dir[0]) {
            struct pe_runtime_function *f=pe_function_search((void *)(pe_main_base+dir[0]),dir[1]/sizeof(*f),pe_main_base,pc);
            if(f) { if(image_base) *image_base=pe_main_base; return f; }
        }
    }
    PE_STATE_GUARD;
    for(struct pe_function_table *table=pe_function_tables;table;table=table->next) {
        struct pe_runtime_function *f=pe_function_search(table->entries,table->count,table->base,pc);
        if(f) { if(image_base) *image_base=table->base; return f; }
    }
    return NULL;
}
