/* Locale identity.
 *
 * The bridge runs one fixed profile: English (United States), LCID 0x0409,
 * UTF-8 code page 65001, UTC-local time. That is the same profile
 * bridge/date_format.inc already formats dates with, so these functions are
 * real answers rather than placeholders.
 *
 * Honest limits: only en-US is installed. Requests for any other locale fail
 * with ERROR_INVALID_PARAMETER the way Windows fails for a locale that is not
 * installed, and locale-dependent formatting for other locales is not
 * implemented (see bridge/date_format.inc). LOCALE_INVARIANT (0x007f) has no
 * name in this profile and is rejected by the name conversions. */

#define PE_LCID_EN_US      0x0409u
#define PE_LCID_USER       0x0400u   /* LOCALE_USER_DEFAULT */
#define PE_LCID_SYSTEM     0x0800u   /* LOCALE_SYSTEM_DEFAULT */
#define PE_LCID_CUSTOM     0x0c00u   /* LOCALE_CUSTOM_DEFAULT */
#define PE_LOCALE_NAME_LEN 6u        /* "en-US" plus the terminator */

static const uint16_t pe_locale_name[] = { 'e', 'n', '-', 'U', 'S', 0 };

static uint16_t pe_locale_fold(uint16_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint16_t)(c + 32) : c;
}
/* Accepts "en", "en-US" and "en_US" in any case, like the locale-name APIs. */
static bool pe_locale_name_is_en_us(const uint16_t *name)
{
    if (!name) return false;
    if (pe_locale_fold(name[0]) != 'e' || pe_locale_fold(name[1]) != 'n') return false;
    if (!name[2]) return true;
    if (name[2] != '-' && name[2] != '_') return false;
    return pe_locale_fold(name[3]) == 'u' && pe_locale_fold(name[4]) == 's' && !name[5];
}
/* The user/system default LCIDs resolve to the installed locale, as on Windows. */
static bool pe_locale_lcid_installed(uint32_t lcid)
{
    return lcid == PE_LCID_EN_US || lcid == PE_LCID_USER ||
           lcid == PE_LCID_SYSTEM || lcid == PE_LCID_CUSTOM;
}
static int pe_locale_copy(uint16_t *out, int cap)
{
    if (!out || cap <= 0) { SetLastError(87); return 0; }
    if ((unsigned)cap < PE_LOCALE_NAME_LEN) { SetLastError(122); return 0; }
    memcpy(out, pe_locale_name, sizeof(pe_locale_name));
    return (int)PE_LOCALE_NAME_LEN;
}

__attribute__((ms_abi)) uint32_t GetUserDefaultLCID(void) { return PE_LCID_EN_US; }
__attribute__((ms_abi)) uint32_t GetSystemDefaultLCID(void) { return PE_LCID_EN_US; }
__attribute__((ms_abi)) uint16_t GetUserDefaultLangID(void) { return (uint16_t)PE_LCID_EN_US; }
__attribute__((ms_abi)) uint16_t GetSystemDefaultLangID(void) { return (uint16_t)PE_LCID_EN_US; }
__attribute__((ms_abi)) uint16_t GetUserDefaultUILanguage(void) { return (uint16_t)PE_LCID_EN_US; }
__attribute__((ms_abi)) uint16_t GetSystemDefaultUILanguage(void) { return (uint16_t)PE_LCID_EN_US; }
__attribute__((ms_abi)) uint16_t GetThreadUILanguage(void) { return (uint16_t)PE_LCID_EN_US; }
__attribute__((ms_abi)) uint32_t GetThreadLocale(void) { return PE_LCID_EN_US; }
__attribute__((ms_abi)) BOOL SetThreadLocale(uint32_t lcid)
{
    /* One process-wide profile: only the installed locale can be selected. */
    if (!pe_locale_lcid_installed(lcid)) { SetLastError(87); return 0; }
    return 1;
}
__attribute__((ms_abi)) int GetUserDefaultLocaleName(uint16_t *out, int cap)
{ return pe_locale_copy(out, cap); }
__attribute__((ms_abi)) int GetSystemDefaultLocaleName(uint16_t *out, int cap)
{ return pe_locale_copy(out, cap); }
__attribute__((ms_abi)) int LCIDToLocaleName(uint32_t lcid, uint16_t *out, int cap,
                                             uint32_t flags)
{
    if (flags) { SetLastError(87); return 0; }
    if (!pe_locale_lcid_installed(lcid)) { SetLastError(87); return 0; }
    return pe_locale_copy(out, cap);
}
__attribute__((ms_abi)) uint32_t LocaleNameToLCID(const uint16_t *name, uint32_t flags)
{
    if (flags) { SetLastError(87); return 0; }
    if (!pe_locale_name_is_en_us(name)) { SetLastError(87); return 0; }
    return PE_LCID_EN_US;
}
__attribute__((ms_abi)) BOOL IsValidLocale(uint32_t lcid, uint32_t flags)
{
    if (flags & ~(uint32_t)(1u | 2u | 4u)) { SetLastError(87); return 0; }
    if (!pe_locale_lcid_installed(lcid)) { SetLastError(87); return 0; }
    return 1;
}
__attribute__((ms_abi)) BOOL IsValidLocaleName(const uint16_t *name)
{
    if (!pe_locale_name_is_en_us(name)) { SetLastError(87); return 0; }
    return 1;
}
