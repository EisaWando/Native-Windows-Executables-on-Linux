/* Clipboard and global memory (HGLOBAL).
 *
 * Honest scope, see docs/SESSION_STATE.md:
 *  - The clipboard is process-local. There is no cross-process or X11
 *    selection bridge yet, so text copied by a native app is not visible here
 *    and data set here is not announced to the X server.
 *  - CF_TEXT and CF_OEMTEXT use the bridge UTF-8 codepage profile; text
 *    formats are synthesized lazily from the first explicit text format.
 *  - WM_RENDERFORMAT is delivered for delayed rendering when the data is
 *    requested. WM_RENDERALLFORMATS before the owner is destroyed is not sent.
 *
 * GMEM_FIXED and GMEM_MOVEABLE share one representation: the handle is also a
 * valid pointer, so callers that skip GlobalLock still work. Lock counting and
 * the documented "GlobalUnlock returns zero when the lock count reaches zero,
 * with GetLastError() == NO_ERROR" quirk are honoured.
 * A locked registry validates HGLOBAL values without dereferencing arbitrary handles. */

#define PE_GMEM_MAGIC    0x4d454d47ULL   /* "GMEM" */
#define PE_GMEM_MOVEABLE 0x0002u
#define PE_GMEM_ZEROINIT 0x0040u
#define PE_GMEM_INVALID  0x8000u
#define PE_WM_DESTROYCLIPBOARD 0x0307u
#define PE_WM_RENDERFORMAT     0x0305u

struct pe_gmem { uint64_t magic; uint64_t size; uint32_t flags; uint32_t locks; bool clipboard_owned; struct pe_gmem *next; };
static struct pe_gmem *pe_gmem_head;

static struct pe_gmem *pe_gmem_block(HANDLE h)
{
    for (struct pe_gmem *b = pe_gmem_head; b; b = b->next)
        if ((HANDLE)(b+1) == h) return b;
    return NULL;
}

__attribute__((ms_abi)) HANDLE GlobalAlloc(unsigned flags, uint64_t size)
{
    PE_STATE_GUARD;
    if (!size) size = 1;
    if (size > (1ULL << 40)) { SetLastError(8); return NULL; }
    struct pe_gmem *b = pe_heap_alloc(size + sizeof(*b), 0);
    if (!b) { SetLastError(8); return NULL; }
    b->magic = PE_GMEM_MAGIC; b->size = size; b->flags = flags; b->locks = 0; b->clipboard_owned = false;
    b->next = pe_gmem_head; pe_gmem_head = b;
    if (flags & PE_GMEM_ZEROINIT) memset(b + 1, 0, size);
    return (HANDLE)(b + 1);
}
__attribute__((ms_abi)) void *GlobalLock(HANDLE h)
{
    PE_STATE_GUARD;
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return NULL; }
    /* Documented: fixed objects always have a lock count of zero. */
    if (b->flags & PE_GMEM_MOVEABLE) b->locks++;
    return (void *)h;
}
__attribute__((ms_abi)) int GlobalUnlock(HANDLE h)
{
    PE_STATE_GUARD;
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return 0; }
    if (!(b->flags & PE_GMEM_MOVEABLE)) return 1;
    if (!b->locks) { SetLastError(158); return 0; }   /* ERROR_NOT_LOCKED */
    if (!--b->locks) { SetLastError(0); return 0; }   /* unlocked: NO_ERROR */
    return 1;
}
__attribute__((ms_abi)) uint64_t GlobalSize(HANDLE h)
{
    PE_STATE_GUARD;
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return 0; }
    return b->size;
}
__attribute__((ms_abi)) unsigned GlobalFlags(HANDLE h)
{
    PE_STATE_GUARD;
    struct pe_gmem *b = pe_gmem_block(h);
    return b ? b->flags : PE_GMEM_INVALID;
}
__attribute__((ms_abi)) HANDLE GlobalHandle(const void *p)
{
    PE_STATE_GUARD;
    if (pe_gmem_block((HANDLE)p)) return (HANDLE)p;
    SetLastError(6); return NULL;
}
__attribute__((ms_abi)) HANDLE GlobalFree(HANDLE h)
{
    PE_STATE_GUARD;
    if (!h) return NULL;                       /* documented: NULL is a no-op */
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return h; }     /* failure returns the handle */
    if (b->clipboard_owned) { SetLastError(5); return h; }
    struct pe_gmem **link = &pe_gmem_head;
    while (*link != b) link = &(*link)->next;
    *link = b->next; b->magic = 0;
    pe_heap_free(b);
    return NULL;
}
__attribute__((ms_abi)) HANDLE GlobalReAlloc(HANDLE h, uint64_t size, unsigned flags)
{
    PE_STATE_GUARD;
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return NULL; }
    if (b->clipboard_owned) { SetLastError(5); return NULL; }
    unsigned locks = b->locks; uint64_t old = b->size;
    if (!size) size = 1;
    HANDLE moved = GlobalAlloc(flags ? flags : b->flags, size);
    if (!moved) return NULL;
    memcpy((void *)moved, (void *)h, old < size ? old : size);
    struct pe_gmem *nb = pe_gmem_block(moved);
    nb->locks = locks;
    GlobalFree(h);
    return moved;
}

struct pe_clip_format { unsigned format; HANDLE memory; bool delayed, synthetic; };
static struct pe_clip_format *pe_clip_formats;
static unsigned pe_clip_count, pe_clip_capacity;
static unsigned pe_clip_opens;        /* nested OpenClipboard count (0 = closed) */
static DWORD pe_clip_open_tid;
static unsigned pe_clip_sequence;     /* GetClipboardSequenceNumber */
static HANDLE pe_clip_open_hwnd;      /* window passed to OpenClipboard (0 = task) */
static HANDLE pe_clip_owner;          /* owner of the current contents */
static bool pe_clip_owner_set;        /* EmptyClipboard assigned an owner (maybe NULL) */

static HANDLE *pe_clip_listeners;
static unsigned pe_clip_listener_count, pe_clip_listener_capacity;
__attribute__((ms_abi)) BOOL AddClipboardFormatListener(HANDLE hwnd)
{
    PE_STATE_GUARD;
    if (!pe_window_lookup(hwnd, false)) return 0;
    for (unsigned i=0; i<pe_clip_listener_count; i++)
        if (pe_clip_listeners[i]==hwnd) return 1;
    if (pe_clip_listener_count == pe_clip_listener_capacity) {
        unsigned cap = pe_clip_listener_capacity ? pe_clip_listener_capacity*2 : 16;
        HANDLE *next = pe_raw_map(cap * sizeof(*next));
        if (!next) { SetLastError(8); return 0; }
        if (pe_clip_listeners) {
            memcpy(next,pe_clip_listeners,pe_clip_listener_count*sizeof(*next));
            raw_syscall3(SYS_munmap,(long)pe_clip_listeners,pe_clip_listener_capacity*sizeof(*next),0);
        }
        pe_clip_listeners=next; pe_clip_listener_capacity=cap;
    }
    pe_clip_listeners[pe_clip_listener_count++]=hwnd;
    return 1;
}
__attribute__((ms_abi)) BOOL RemoveClipboardFormatListener(HANDLE hwnd)
{
    PE_STATE_GUARD;
    if (!pe_window_lookup(hwnd, false)) return 0;
    for (unsigned i=0; i<pe_clip_listener_count; i++) {
        if (pe_clip_listeners[i]!=hwnd) continue;
        pe_clip_listeners[i]=pe_clip_listeners[--pe_clip_listener_count];
        return 1;
    }
    SetLastError(1168); return 0;
}
/* PostMessage only queues work; no application callback runs under this lock. */
static void pe_clip_notify(void)
{
    DWORD saved=GetLastError();
    for (unsigned i=0; i<pe_clip_listener_count;) {
        if (!pe_window_lookup(pe_clip_listeners[i], false)) {
            pe_clip_listeners[i]=pe_clip_listeners[--pe_clip_listener_count];
            continue;
        }
        PostMessageW(pe_clip_listeners[i],0x31d,0,0);
        i++;
    }
    SetLastError(saved);
}
static bool pe_clip_reserve(unsigned need)
{
    if (need <= pe_clip_capacity) return true;
    unsigned cap = pe_clip_capacity ? pe_clip_capacity * 2 : 16;
    if (cap < need) cap = need;
    if (cap > 0x4000) { SetLastError(8); return false; }
    struct pe_clip_format *entries = pe_raw_map(cap * sizeof(*entries));
    if (!entries) { SetLastError(8); return false; }
    if (pe_clip_formats) {
        memcpy(entries, pe_clip_formats, pe_clip_count * sizeof(*entries));
        raw_syscall3(SYS_munmap, (long)pe_clip_formats,
                     pe_clip_capacity * sizeof(*entries), 0);
    }
    pe_clip_formats = entries; pe_clip_capacity = cap;
    return true;
}
static int pe_clip_find(unsigned format)
{
    for (unsigned i = 0; i < pe_clip_count; i++)
        if (pe_clip_formats[i].format == format) return (int)i;
    return -1;
}
/* Only handles this process allocated with GlobalAlloc are freed; GDI handles
 * stored for CF_BITMAP/CF_PALETTE and foreign handles are left alone. */
static void pe_clip_release(struct pe_clip_format *entry)
{
    struct pe_gmem *block = entry->memory ? pe_gmem_block(entry->memory) : NULL;
    if (block) { block->clipboard_owned = false; GlobalFree(entry->memory); }
    entry->memory = NULL; entry->delayed = false;
}

__attribute__((ms_abi)) BOOL OpenClipboard(HANDLE hwnd)
{
    PE_STATE_GUARD;
    if (hwnd && !pe_window_lookup(hwnd, false)) return 0;
    if (pe_clip_opens && (pe_clip_open_hwnd != hwnd || pe_clip_open_tid != GetCurrentThreadId())) { SetLastError(5); return 0; }
    pe_clip_open_hwnd = hwnd; pe_clip_open_tid = GetCurrentThreadId(); pe_clip_opens++;
    /* The window does not become the clipboard owner until EmptyClipboard. */
    return 1;
}
__attribute__((ms_abi)) BOOL CloseClipboard(void)
{
    PE_STATE_GUARD;
    if (!pe_clip_opens || pe_clip_open_tid != GetCurrentThreadId()) { SetLastError(1418); return 0; }
    if (!--pe_clip_opens) { pe_clip_open_hwnd = 0; pe_clip_open_tid = 0; }
    /* Contents and ownership survive a close: the system owns them now. */
    return 1;
}
__attribute__((ms_abi)) BOOL EmptyClipboard(void)
{
    HANDLE previous;
    {
        PE_STATE_GUARD;
        if (!pe_clip_opens || pe_clip_open_tid != GetCurrentThreadId()) { SetLastError(1418); return 0; }
        previous = pe_clip_owner_set ? pe_clip_owner : NULL;
        for (unsigned i = 0; i < pe_clip_count; i++) pe_clip_release(&pe_clip_formats[i]);
        pe_clip_count = 0;
        pe_clip_owner_set = true; pe_clip_owner = pe_clip_open_hwnd;
        pe_clip_sequence++;
        pe_clip_notify();
    }
    /* Tell the previous owner after releasing the bridge lock. */
    if (previous) pe_ui_send(previous, PE_WM_DESTROYCLIPBOARD, 0, 0);
    return 1;
}
__attribute__((ms_abi)) HANDLE SetClipboardData(unsigned format, HANDLE memory)
{
    PE_STATE_GUARD;
    if (!pe_clip_opens || pe_clip_open_tid != GetCurrentThreadId()) { SetLastError(1418); return NULL; }
    if (!format || format > 0xffffu) { SetLastError(87); return NULL; }
    if (!pe_clip_owner_set) { pe_clip_owner_set = true; pe_clip_owner = pe_clip_open_hwnd; }
    if (memory && (format == 1 || format == 7 || format == 13) && !pe_gmem_block(memory)) {
        SetLastError(6); return NULL;
    }
    if (memory && (format == 1 || format == 7 || format == 13) &&
        !pe_clip_reserve(pe_clip_count + 3)) return NULL;
    /* The system takes ownership. A NULL handle means delayed rendering: the
     * owner is asked for the data through WM_RENDERFORMAT on demand. */
    int existing = pe_clip_find(format);
    if (existing >= 0) {
        if (pe_clip_formats[existing].memory != memory)
            pe_clip_release(&pe_clip_formats[existing]);
    } else {
        if (!pe_clip_reserve(pe_clip_count + 1)) return NULL;
        existing = (int)pe_clip_count++;
        pe_clip_formats[existing].format = format;
        pe_clip_formats[existing].memory = NULL;
    }
    pe_clip_formats[existing].memory = memory;
    pe_clip_formats[existing].delayed = memory == NULL;
    pe_clip_formats[existing].synthetic = false;
    struct pe_gmem *owned = memory ? pe_gmem_block(memory) : NULL;
    if (owned) owned->clipboard_owned = true;
    if (memory && (format == 1 || format == 7 || format == 13)) {
        /* Invalidate stale synthesized copies when an explicit format changes. */
        for (unsigned i = 0; i < pe_clip_count; i++) {
            if (pe_clip_formats[i].synthetic) {
                pe_clip_release(&pe_clip_formats[i]);
                pe_clip_formats[i].synthetic = true;
            }
        }
        const unsigned formats[] = { 1, 7, 13 };
        for (unsigned i = 0; i < 3; i++) {
            if (pe_clip_find(formats[i]) >= 0) continue;
            if (!pe_clip_reserve(pe_clip_count + 1)) return NULL;
            pe_clip_formats[pe_clip_count++] = (struct pe_clip_format){
                .format = formats[i], .synthetic = true
            };
        }
    }
    pe_clip_sequence++;
    pe_clip_notify();
    return memory;
}
/* All conversion stays under the clipboard lock. Bounds come from GlobalSize;
 * malformed, unterminated text is rejected instead of reading past its block. */
static HANDLE pe_clip_synthesize(unsigned format)
{
    int target = pe_clip_find(format), source = -1;
    if (target < 0 || !pe_clip_formats[target].synthetic) return NULL;
    unsigned preferred[] = { format == 13 ? 1u : 13u, format == 13 ? 7u : (format == 1 ? 7u : 1u) };
    for (unsigned i = 0; i < 2; i++) {
        int n = pe_clip_find(preferred[i]);
        if (n >= 0 && !pe_clip_formats[n].synthetic && pe_clip_formats[n].memory) { source = n; break; }
    }
    if (source < 0) return NULL;
    HANDLE h = pe_clip_formats[source].memory;
    struct pe_gmem *block = pe_gmem_block(h);
    if (!block || block->size > INT32_MAX) { SetLastError(6); return NULL; }
    unsigned sf = pe_clip_formats[source].format;
    size_t count = 0;
    if (sf == 13) {
        uint16_t *p = (uint16_t *)h;
        while (count < block->size / 2 && p[count]) count++;
        if (count == block->size / 2) { SetLastError(87); return NULL; }
    } else {
        char *p = (char *)h;
        while (count < block->size && p[count]) count++;
        if (count == block->size) { SetLastError(87); return NULL; }
    }
    int bytes;
    if (sf == 13) bytes = WideCharToMultiByte(65001, 0, h, (int)count+1, NULL, 0, NULL, NULL);
    else if (format == 13) {
        int n = MultiByteToWideChar(65001, 0, h, (int)count+1, NULL, 0);
        bytes = n > 0 && n <= INT32_MAX/2 ? n*2 : 0;
    } else bytes = (int)count+1;
    if (!bytes) return NULL;
    HANDLE result = GlobalAlloc(PE_GMEM_MOVEABLE, bytes);
    if (!result) return NULL;
    int ok;
    if (sf == 13) ok = WideCharToMultiByte(65001, 0, h, (int)count+1, result, bytes, NULL, NULL);
    else if (format == 13) ok = MultiByteToWideChar(65001, 0, h, (int)count+1, result, bytes/2);
    else { memcpy(result, h, bytes); ok = bytes; }
    if (!ok) { GlobalFree(result); return NULL; }
    pe_gmem_block(result)->clipboard_owned = true;
    pe_clip_formats[target].memory = result;
    return result;
}
__attribute__((ms_abi)) HANDLE GetClipboardData(unsigned format)
{
    bool render = false;
    HANDLE memory = NULL;
    {
        PE_STATE_GUARD;
        if (!pe_clip_opens || pe_clip_open_tid != GetCurrentThreadId()) { SetLastError(1418); return NULL; }
        int found = pe_clip_find(format);
        if (found < 0) { SetLastError(0); return NULL; }
        if (pe_clip_formats[found].synthetic && !pe_clip_formats[found].memory) {
            memory = pe_clip_synthesize(format);
        } else if (pe_clip_formats[found].delayed) {
            /* Ask the owner to render, then look the format up again. */
            render = pe_clip_owner && pe_gui_lookup(pe_clip_owner, PE_GUI_WINDOW);
        } else {
            memory = pe_clip_formats[found].memory;
        }
    }
    if (render) {
        pe_ui_send(pe_clip_owner, PE_WM_RENDERFORMAT, format, 0);
        PE_STATE_GUARD;
        int found = pe_clip_find(format);
        memory = found >= 0 && !pe_clip_formats[found].delayed
                     ? pe_clip_formats[found].memory : NULL;
    }
    return memory;
}
__attribute__((ms_abi)) BOOL IsClipboardFormatAvailable(unsigned format)
{
    PE_STATE_GUARD;
    return pe_clip_find(format) >= 0;
}
__attribute__((ms_abi)) int CountClipboardFormats(void)
{
    PE_STATE_GUARD;
    return (int)pe_clip_count;
}
__attribute__((ms_abi)) unsigned EnumClipboardFormats(unsigned format)
{
    PE_STATE_GUARD;
    if (!pe_clip_opens || pe_clip_open_tid != GetCurrentThreadId()) { SetLastError(1418); return 0; }
    if (!format) { if (!pe_clip_count) SetLastError(0); return pe_clip_count ? pe_clip_formats[0].format : 0; }
    int found = pe_clip_find(format);
    if (found >= 0 && (unsigned)found + 1 < pe_clip_count)
        return pe_clip_formats[found + 1].format;
    SetLastError(0);
    return 0;
}
__attribute__((ms_abi)) int GetPriorityClipboardFormat(const unsigned *list, int count)
{
    PE_STATE_GUARD;
    if (!list || count <= 0) { SetLastError(87); return -1; }
    for (int i = 0; i < count; i++)
        if (pe_clip_find(list[i]) >= 0) return (int)list[i];
    return pe_clip_count ? -1 : 0;   /* empty clipboard -> 0, no match -> -1 */
}
__attribute__((ms_abi)) HANDLE GetClipboardOwner(void)
{
    PE_STATE_GUARD;
    return pe_clip_owner_set ? pe_clip_owner : NULL;
}
__attribute__((ms_abi)) HANDLE GetOpenClipboardWindow(void)
{
    PE_STATE_GUARD;
    return pe_clip_opens ? pe_clip_open_hwnd : NULL;
}
__attribute__((ms_abi)) unsigned GetClipboardSequenceNumber(void)
{
    PE_STATE_GUARD;
    return pe_clip_sequence;
}
