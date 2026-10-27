/* Clipboard and global memory (HGLOBAL).
 *
 * Honest scope, see docs/SESSION_STATE.md:
 *  - The clipboard is process-local. There is no cross-process or X11
 *    selection bridge yet, so text copied by a native app is not visible here
 *    and data set here is not announced to the X server.
 *  - Implicit format synthesis (CF_TEXT <-> CF_UNICODETEXT and friends) is not
 *    implemented; a format is available only if it was actually stored.
 *  - WM_RENDERFORMAT is delivered for delayed rendering when the data is
 *    requested. WM_RENDERALLFORMATS before the owner is destroyed is not sent.
 *
 * GMEM_FIXED and GMEM_MOVEABLE share one representation: the handle is also a
 * valid pointer, so callers that skip GlobalLock still work. Lock counting and
 * the documented "GlobalUnlock returns zero when the lock count reaches zero,
 * with GetLastError() == NO_ERROR" quirk are honoured.
 * A handle must come from GlobalAlloc; arbitrary values cannot be validated
 * without dereferencing them (Windows faults on garbage handles as well). */

#define PE_GMEM_MAGIC    0x4d454d47ULL   /* "GMEM" */
#define PE_GMEM_MOVEABLE 0x0002u
#define PE_GMEM_ZEROINIT 0x0040u
#define PE_GMEM_INVALID  0x8000u
#define PE_WM_DESTROYCLIPBOARD 0x0307u
#define PE_WM_RENDERFORMAT     0x0305u

struct pe_gmem { uint64_t magic; uint64_t size; uint32_t flags; uint32_t locks; };

static struct pe_gmem *pe_gmem_block(HANDLE h)
{
    struct pe_gmem *b = h ? (struct pe_gmem *)h - 1 : NULL;
    return b && b->magic == PE_GMEM_MAGIC ? b : NULL;
}

__attribute__((ms_abi)) HANDLE GlobalAlloc(unsigned flags, uint64_t size)
{
    if (!size) size = 1;
    if (size > (1ULL << 40)) { SetLastError(8); return NULL; }
    struct pe_gmem *b = pe_heap_alloc(size + sizeof(*b), 0);
    if (!b) { SetLastError(8); return NULL; }
    b->magic = PE_GMEM_MAGIC; b->size = size; b->flags = flags; b->locks = 0;
    if (flags & PE_GMEM_ZEROINIT) memset(b + 1, 0, size);
    return (HANDLE)(b + 1);
}
__attribute__((ms_abi)) void *GlobalLock(HANDLE h)
{
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return NULL; }
    /* Documented: fixed objects always have a lock count of zero. */
    if (b->flags & PE_GMEM_MOVEABLE) b->locks++;
    return (void *)h;
}
__attribute__((ms_abi)) int GlobalUnlock(HANDLE h)
{
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return 0; }
    if (!(b->flags & PE_GMEM_MOVEABLE)) return 1;
    if (!b->locks) { SetLastError(158); return 0; }   /* ERROR_NOT_LOCKED */
    if (!--b->locks) { SetLastError(0); return 0; }   /* unlocked: NO_ERROR */
    return 1;
}
__attribute__((ms_abi)) uint64_t GlobalSize(HANDLE h)
{
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return 0; }
    return b->size;
}
__attribute__((ms_abi)) unsigned GlobalFlags(HANDLE h)
{
    struct pe_gmem *b = pe_gmem_block(h);
    return b ? b->flags : PE_GMEM_INVALID;
}
__attribute__((ms_abi)) HANDLE GlobalHandle(const void *p)
{
    if (pe_gmem_block((HANDLE)p)) return (HANDLE)p;
    SetLastError(6); return NULL;
}
__attribute__((ms_abi)) HANDLE GlobalFree(HANDLE h)
{
    if (!h) return NULL;                       /* documented: NULL is a no-op */
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return h; }     /* failure returns the handle */
    b->magic = 0;                              /* catches double free */
    pe_heap_free(b);
    return NULL;
}
__attribute__((ms_abi)) HANDLE GlobalReAlloc(HANDLE h, uint64_t size, unsigned flags)
{
    struct pe_gmem *b = pe_gmem_block(h);
    if (!b) { SetLastError(6); return NULL; }
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

struct pe_clip_format { unsigned format; HANDLE memory; bool delayed; };
static struct pe_clip_format *pe_clip_formats;
static unsigned pe_clip_count, pe_clip_capacity;
static unsigned pe_clip_opens;        /* nested OpenClipboard count (0 = closed) */
static unsigned pe_clip_sequence;     /* GetClipboardSequenceNumber */
static HANDLE pe_clip_open_hwnd;      /* window passed to OpenClipboard (0 = task) */
static HANDLE pe_clip_owner;          /* owner of the current contents */
static bool pe_clip_owner_set;        /* EmptyClipboard assigned an owner (maybe NULL) */

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
    if (entry->memory && pe_gmem_block(entry->memory)) GlobalFree(entry->memory);
    entry->memory = NULL; entry->delayed = false;
}

__attribute__((ms_abi)) BOOL OpenClipboard(HANDLE hwnd)
{
    PE_STATE_GUARD;
    if (hwnd && !pe_window_lookup(hwnd, false)) return 0;
    if (pe_clip_opens && pe_clip_open_hwnd != hwnd) { SetLastError(5); return 0; }
    pe_clip_open_hwnd = hwnd; pe_clip_opens++;
    /* The window does not become the clipboard owner until EmptyClipboard. */
    return 1;
}
__attribute__((ms_abi)) BOOL CloseClipboard(void)
{
    PE_STATE_GUARD;
    if (!pe_clip_opens) { SetLastError(5); return 0; }
    if (!--pe_clip_opens) pe_clip_open_hwnd = 0;
    /* Contents and ownership survive a close: the system owns them now. */
    return 1;
}
__attribute__((ms_abi)) BOOL EmptyClipboard(void)
{
    HANDLE previous;
    {
        PE_STATE_GUARD;
        if (!pe_clip_opens) { SetLastError(5); return 0; }
        previous = pe_clip_owner_set ? pe_clip_owner : NULL;
        for (unsigned i = 0; i < pe_clip_count; i++) pe_clip_release(&pe_clip_formats[i]);
        pe_clip_count = 0;
        pe_clip_owner_set = true; pe_clip_owner = pe_clip_open_hwnd;
        pe_clip_sequence++;
    }
    /* Documented: the previous owner is told its data is gone. A NULL owner
     * (OpenClipboard(NULL)) makes the following SetClipboardData fail. */
    if (previous) pe_ui_send(previous, PE_WM_DESTROYCLIPBOARD, 0, 0);
    return 1;
}
__attribute__((ms_abi)) HANDLE SetClipboardData(unsigned format, HANDLE memory)
{
    PE_STATE_GUARD;
    if (!pe_clip_opens) { SetLastError(5); return NULL; }
    if (!format || format > 0xffffu) { SetLastError(87); return NULL; }
    if (pe_clip_owner_set && !pe_clip_owner) { SetLastError(5); return NULL; }
    if (!pe_clip_owner_set) { pe_clip_owner_set = true; pe_clip_owner = pe_clip_open_hwnd; }
    /* The system takes ownership. A NULL handle means delayed rendering: the
     * owner is asked for the data through WM_RENDERFORMAT on demand. */
    int existing = pe_clip_find(format);
    if (existing >= 0) {
        pe_clip_release(&pe_clip_formats[existing]);
    } else {
        if (!pe_clip_reserve(pe_clip_count + 1)) return NULL;
        existing = (int)pe_clip_count++;
        pe_clip_formats[existing].format = format;
        pe_clip_formats[existing].memory = NULL;
    }
    pe_clip_formats[existing].memory = memory;
    pe_clip_formats[existing].delayed = memory == NULL;
    pe_clip_sequence++;
    return memory;
}
__attribute__((ms_abi)) HANDLE GetClipboardData(unsigned format)
{
    bool render = false;
    HANDLE memory = NULL;
    {
        PE_STATE_GUARD;
        if (!pe_clip_opens) { SetLastError(5); return NULL; }
        int found = pe_clip_find(format);
        if (found < 0) { SetLastError(0); return NULL; }
        if (pe_clip_formats[found].delayed) {
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
    if (!pe_clip_opens) { SetLastError(5); return 0; }
    if (!format) return pe_clip_count ? pe_clip_formats[0].format : 0;
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
