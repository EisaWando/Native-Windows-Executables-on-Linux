/* LCMapStringW / LCMapStringA / LCMapStringEx and the CharUpper / CharLower
 * family, using the simple lower/upper deltas in the generated table
 * bridge/char_type_ranges.inc (see bridge/char_type_gen.py).
 *
 * Honest limits:
 *  - Only case transformations are implemented. LCMAP_SORTKEY, LCMAP_BYTEREV,
 *    LCMAP_HIRAGANA/KATAKANA, LCMAP_HALFWIDTH/FULLWIDTH, the Chinese
 *    transformations, LCMAP_SORTHANDLE and LCMAP_HASH fail with
 *    ERROR_INVALID_FLAGS rather than returning an invented result.
 *  - The NORM_* flags have no effect on case mapping, as documented.
 *  - Only one-to-one case mappings exist (as in Windows): U+00DF stays U+00DF
 *    when upper-cased, it does not expand to "SS".
 *  - LCMAP_TITLECASE maps the first character of each word to uppercase and
 *    the other characters of the word to lowercase.
 *  - The ANSI variants work in the profile's code page (65001, UTF-8), so their
 *    counts are byte counts (see bridge/char_type.inc for the same caveat). */

#define PE_LCMAP_LOWERCASE     0x00000100u
#define PE_LCMAP_UPPERCASE     0x00000200u
#define PE_LCMAP_TITLECASE     0x00000300u
#define PE_LCMAP_SORTKEY       0x00000400u
#define PE_LCMAP_LINGUISTIC    0x01000000u
#define PE_NORM_IGNORECASE     0x00000001u
#define PE_NORM_IGNORENONSPACE 0x00000002u
#define PE_NORM_IGNORESYMBOLS  0x00000004u
#define PE_NORM_IGNOREKANATYPE 0x00010000u
#define PE_NORM_IGNOREWIDTH    0x00020000u
#define PE_NORM_LINGUISTIC     0x08000000u
#define PE_LCMAP_CASE_BITS     (PE_LCMAP_LOWERCASE | PE_LCMAP_UPPERCASE)
#define PE_LCMAP_SUPPORTED     (PE_LCMAP_CASE_BITS | PE_LCMAP_LINGUISTIC | \
                                PE_NORM_IGNORECASE | PE_NORM_IGNORENONSPACE | \
                                PE_NORM_IGNORESYMBOLS | PE_NORM_IGNOREKANATYPE | \
                                PE_NORM_IGNOREWIDTH | PE_NORM_LINGUISTIC)
#define PE_CASE_RANGE_COUNT    (sizeof(pe_case_ranges) / sizeof(pe_case_ranges[0]))

static uint32_t pe_case_lookup(uint32_t cp, bool upper)
{
    unsigned low = 0, high = PE_CASE_RANGE_COUNT;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        const struct pe_case_range *range = &pe_case_ranges[mid];
        if (cp < range->first) { high = mid; continue; }
        if (cp > range->last) { low = mid + 1; continue; }
        int32_t delta = upper ? range->upper_delta : range->lower_delta;
        return delta ? (uint32_t)((int64_t)cp + delta) : cp;
    }
    return cp;
}
static uint32_t pe_case_upper(uint32_t cp) { return pe_case_lookup(cp, true); }
static uint32_t pe_case_lower(uint32_t cp) { return pe_case_lookup(cp, false); }

/* Maps in place or to another buffer; case mapping never changes the length. */
static BOOL pe_case_map(uint32_t flags, const uint16_t *src, int count, uint16_t *dest)
{
    uint32_t mode = flags & PE_LCMAP_CASE_BITS;
    bool word_start = true;
    for (int i = 0; i < count; i++) {
        uint32_t cp = src[i], mapped = cp;
        if (cp) {
            if (mode == PE_LCMAP_UPPERCASE) mapped = pe_case_upper(cp);
            else if (mode == PE_LCMAP_TITLECASE) mapped = word_start ? pe_case_upper(cp) : pe_case_lower(cp);
            else if (mode == PE_LCMAP_LOWERCASE) mapped = pe_case_lower(cp);
            word_start = !(pe_char_type(cp, PE_CT_CTYPE1) & (PE_C1_ALPHA | PE_C1_DIGIT));
        }
        dest[i] = (uint16_t)mapped;
    }
    return 1;
}
static BOOL pe_lcmap_check(uint32_t flags)
{
    if (flags & ~PE_LCMAP_SUPPORTED) { SetLastError(1004); return 0; }
    return 1;
}
/* Shared string path. count is in characters and already resolved; when count
 * is 0 the caller asked for the required buffer size. Returns the mapped
 * character count, which excludes a terminator the source carried. */
static int pe_lcmap_wide(uint32_t flags, const uint16_t *src, int count,
                         uint16_t *dest, int cap)
{
    int required = count;
    if (!cap) return required;                       /* size query, dest unused */
    if (!dest || cap < required) { SetLastError(122); return 0; }
    if (!pe_case_map(flags, src, count, dest)) return 0;
    return count && src[count - 1] == 0 ? count - 1 : count;
}
__attribute__((ms_abi)) int LCMapStringW(uint32_t locale, uint32_t flags, const uint16_t *src,
                                        int count, uint16_t *dest, int cap)
{
    if (!pe_locale_lcid_installed(locale)) { SetLastError(87); return 0; }
    if (!pe_lcmap_check(flags)) return 0;
    if (!src || !count) { SetLastError(87); return 0; }   /* documented: not 0 */
    return pe_lcmap_wide(flags, src, count < 0 ? (int)lstrlenW(src) + 1 : count, dest, cap);
}
__attribute__((ms_abi)) int LCMapStringEx(const uint16_t *name, uint32_t flags, const uint16_t *src,
                                         int count, uint16_t *dest, int cap,
                                         void *version, void *reserved, intptr_t sort_handle)
{
    /* LOCALE_NAME_USER_DEFAULT is NULL; the other two parameters are reserved. */
    if (name && !pe_locale_name_is_en_us(name)) { SetLastError(87); return 0; }
    if (version || reserved || sort_handle) { SetLastError(87); return 0; }
    if (!pe_lcmap_check(flags)) return 0;
    if (!src || !count) { SetLastError(87); return 0; }
    return pe_lcmap_wide(flags, src, count < 0 ? (int)lstrlenW(src) + 1 : count, dest, cap);
}
__attribute__((ms_abi)) int LCMapStringA(uint32_t locale, uint32_t flags, const char *src,
                                        int count, char *dest, int cap)
{
    if (!pe_locale_lcid_installed(locale)) { SetLastError(87); return 0; }
    if (!pe_lcmap_check(flags)) return 0;
    if (!src || !count) { SetLastError(87); return 0; }
    int units = count < 0 ? (int)cstring_length(src) + 1 : count;
    uint16_t *wide = pe_heap_alloc((uint64_t)(units + 1) * 2, 0);
    if (!wide) { SetLastError(8); return 0; }
    int characters = MultiByteToWideChar(65001, 0, src, count, wide, units + 1);
    if (characters <= 0 || !pe_case_map(flags, wide, characters, wide)) {
        pe_heap_free(wide);
        return 0;
    }
    int bytes = WideCharToMultiByte(65001, 0, wide, characters, NULL, 0, NULL, NULL);
    int terminator = characters && !wide[characters - 1];
    int result = bytes - (terminator ? 1 : 0);
    if (!cap) { pe_heap_free(wide); return bytes; }
    if (!dest || cap < bytes) { pe_heap_free(wide); SetLastError(122); return 0; }
    int written = WideCharToMultiByte(65001, 0, wide, characters, dest, cap, NULL, NULL);
    pe_heap_free(wide);
    return written > 0 ? result : 0;
}

__attribute__((ms_abi)) DWORD CharUpperBuffW(uint16_t *string, DWORD count)
{
    if (!string) { SetLastError(87); return 0; }
    for (DWORD i = 0; i < count; i++) string[i] = (uint16_t)pe_case_upper(string[i]);
    return count;
}
__attribute__((ms_abi)) DWORD CharLowerBuffW(uint16_t *string, DWORD count)
{
    if (!string) { SetLastError(87); return 0; }
    for (DWORD i = 0; i < count; i++) string[i] = (uint16_t)pe_case_lower(string[i]);
    return count;
}
__attribute__((ms_abi)) DWORD CharUpperBuffA(char *string, DWORD count)
{
    if (!string) { SetLastError(87); return 0; }
    for (DWORD i = 0; i < count; i++) string[i] = (char)pe_case_upper((uint8_t)string[i]);
    return count;
}
__attribute__((ms_abi)) DWORD CharLowerBuffA(char *string, DWORD count)
{
    if (!string) { SetLastError(87); return 0; }
    for (DWORD i = 0; i < count; i++) string[i] = (char)pe_case_lower((uint8_t)string[i]);
    return count;
}
/* A value below 0x10000 is a single character packed into the pointer, which is
 * the raw-resource idiom these functions also accept (see the documentation). */
__attribute__((ms_abi)) uint16_t *CharUpperW(uint16_t *string)
{
    if ((uintptr_t)string < 0x10000) return (uint16_t *)(uintptr_t)pe_case_upper((uint32_t)(uintptr_t)string);
    if (string) for (uint16_t *p = string; *p; p++) *p = (uint16_t)pe_case_upper(*p);
    return string;
}
__attribute__((ms_abi)) uint16_t *CharLowerW(uint16_t *string)
{
    if ((uintptr_t)string < 0x10000) return (uint16_t *)(uintptr_t)pe_case_lower((uint32_t)(uintptr_t)string);
    if (string) for (uint16_t *p = string; *p; p++) *p = (uint16_t)pe_case_lower(*p);
    return string;
}
__attribute__((ms_abi)) char *CharUpperA(char *string)
{
    if ((uintptr_t)string < 0x10000) return (char *)(uintptr_t)pe_case_upper((uint32_t)(uintptr_t)string);
    if (string) for (char *p = string; *p; p++) *p = (char)pe_case_upper((uint8_t)*p);
    return string;
}
__attribute__((ms_abi)) char *CharLowerA(char *string)
{
    if ((uintptr_t)string < 0x10000) return (char *)(uintptr_t)pe_case_lower((uint32_t)(uintptr_t)string);
    if (string) for (char *p = string; *p; p++) *p = (char)pe_case_lower((uint8_t)*p);
    return string;
}
