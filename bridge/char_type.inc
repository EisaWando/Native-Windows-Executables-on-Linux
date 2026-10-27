/* GetStringTypeW / GetStringTypeA / GetStringTypeExW / GetStringTypeExA and the
 * IsChar* character predicates.
 *
 * Character classes come from the generated table bridge/char_type_ranges.inc
 * (Unicode 16 character database, see bridge/char_type_gen.py), so C1_ALPHA
 * covers letters, syllabaries and ideographs and CT_CTYPE2 uses the Unicode
 * bidirectional categories, as the Windows documentation describes.
 *
 * Honest limits:
 *  - GetStringTypeA/ExA decode the source with the profile's code page (65001,
 *    UTF-8) and write one WORD per UTF-16 code unit of the decoded text, so the
 *    ANSI length is a byte count while the output length counts characters.
 *    Windows behaves the same way in its DBCS locales.
 *  - The Locale parameter of GetStringTypeExW is ignored, as documented.
 *  - IsChar*A treats the byte as the code point with the same value; only ASCII
 *    is unambiguous because the profile's ANSI code page is UTF-8.
 *  - A count of 0 is accepted as a successful no-op (Windows does not document
 *    that case). */

#define PE_CT_CTYPE1      0x0001u
#define PE_CT_CTYPE2      0x0002u
#define PE_CT_CTYPE3      0x0004u
#define PE_C1_UPPER       0x0001u
#define PE_C1_LOWER       0x0002u
#define PE_C1_DIGIT       0x0004u
#define PE_C1_ALPHA       0x0100u
#define PE_CHAR_TYPE_COUNT (sizeof(pe_char_type_ranges) / sizeof(pe_char_type_ranges[0]))

static uint16_t pe_char_type(uint32_t cp, uint32_t info)
{
    unsigned low = 0, high = PE_CHAR_TYPE_COUNT;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        const struct pe_char_type_range *range = &pe_char_type_ranges[mid];
        if (cp < range->first) { high = mid; continue; }
        if (cp > range->last) { low = mid + 1; continue; }
        if (info == PE_CT_CTYPE1) return (uint16_t)range->ctype1;
        if (info == PE_CT_CTYPE2) return (uint16_t)range->ctype2;
        return range->ctype3;
    }
    return 0;
}
static BOOL pe_string_type(uint32_t info, const uint16_t *src, int count, uint16_t *out)
{
    if (info != PE_CT_CTYPE1 && info != PE_CT_CTYPE2 && info != PE_CT_CTYPE3) {
        SetLastError(1004);                      /* ERROR_INVALID_FLAGS */
        return 0;
    }
    /* Documented: the source and destination must not be the same buffer. */
    if (!src || !out || (const void *)src == (const void *)out) { SetLastError(87); return 0; }
    for (int i = 0; i < count; i++) out[i] = pe_char_type(src[i], info);
    return 1;
}
__attribute__((ms_abi)) BOOL GetStringTypeW(uint32_t info, const uint16_t *src,
                                            int count, uint16_t *out)
{
    if (!src) { SetLastError(87); return 0; }        /* any negative count means null-terminated */
    if (!count) {
        if (!out) { SetLastError(87); return 0; }
        return 1;
    }
    /* A negative count means "null-terminated", and the terminator is typed as
     * well, so the caller's array needs length + 1 words. */
    return pe_string_type(info, src, count < 0 ? (int)lstrlenW(src) + 1 : count, out);
}
__attribute__((ms_abi)) BOOL GetStringTypeExW(uint32_t locale, uint32_t info,
                                              const uint16_t *src, int count, uint16_t *out)
{
    (void)locale;                 /* documented: not used by the Unicode version */
    return GetStringTypeW(info, src, count, out);
}
static BOOL pe_string_type_ansi(uint32_t locale, uint32_t info, const char *src,
                                int count, uint16_t *out)
{
    if (!pe_locale_lcid_installed(locale)) { SetLastError(87); return 0; }
    if (info != PE_CT_CTYPE1 && info != PE_CT_CTYPE2 && info != PE_CT_CTYPE3) {
        SetLastError(1004);
        return 0;
    }
    if (!src || !out || (const void *)src == (const void *)out) { SetLastError(87); return 0; }
    if (!count) return 1;
    /* UTF-8 never needs more UTF-16 units than bytes it was decoded from. */
    int units = count < 0 ? (int)cstring_length(src) + 1 : count;
    uint16_t *wide = pe_heap_alloc((uint64_t)(units + 1) * 2, 0);
    if (!wide) { SetLastError(8); return 0; }
    int characters = MultiByteToWideChar(65001, 0, src, count, wide, units + 1);
    if (characters <= 0) { pe_heap_free(wide); return 0; }
    for (int i = 0; i < characters; i++) out[i] = pe_char_type(wide[i], info);
    pe_heap_free(wide);
    return 1;
}
__attribute__((ms_abi)) BOOL GetStringTypeA(uint32_t locale, uint32_t info, const char *src,
                                            int count, uint16_t *out)
{
    return pe_string_type_ansi(locale, info, src, count, out);
}
__attribute__((ms_abi)) BOOL GetStringTypeExA(uint32_t locale, uint32_t info, const char *src,
                                              int count, uint16_t *out)
{
    return pe_string_type_ansi(locale, info, src, count, out);
}

static BOOL pe_char_predicate(uint32_t cp, uint16_t mask)
{
    return (pe_char_type(cp, PE_CT_CTYPE1) & mask) != 0;
}
__attribute__((ms_abi)) BOOL IsCharAlphaW(uint16_t ch) { return pe_char_predicate(ch, PE_C1_ALPHA); }
__attribute__((ms_abi)) BOOL IsCharAlphaNumericW(uint16_t ch)
{ return pe_char_predicate(ch, PE_C1_ALPHA | PE_C1_DIGIT); }
__attribute__((ms_abi)) BOOL IsCharUpperW(uint16_t ch) { return pe_char_predicate(ch, PE_C1_UPPER); }
__attribute__((ms_abi)) BOOL IsCharLowerW(uint16_t ch) { return pe_char_predicate(ch, PE_C1_LOWER); }
__attribute__((ms_abi)) BOOL IsCharAlphaA(char ch) { return pe_char_predicate((uint8_t)ch, PE_C1_ALPHA); }
__attribute__((ms_abi)) BOOL IsCharAlphaNumericA(char ch)
{ return pe_char_predicate((uint8_t)ch, PE_C1_ALPHA | PE_C1_DIGIT); }
__attribute__((ms_abi)) BOOL IsCharUpperA(char ch) { return pe_char_predicate((uint8_t)ch, PE_C1_UPPER); }
__attribute__((ms_abi)) BOOL IsCharLowerA(char ch) { return pe_char_predicate((uint8_t)ch, PE_C1_LOWER); }
