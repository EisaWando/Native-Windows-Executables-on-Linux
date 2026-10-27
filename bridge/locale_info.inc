/* GetLocaleInfoW / GetLocaleInfoA / GetLocaleInfoEx for the fixed en-US
 * profile (bridge/locale.inc). Values are the documented English (United
 * States) defaults, so GetDateFormatW's "M/d/yyyy" and the short date here
 * agree.
 *
 * Honest limits:
 *  - Only the installed locale (0x0409 and the user/system/custom default
 *    LCIDs that resolve to it) is answered; anything else fails with
 *    ERROR_INVALID_PARAMETER.
 *  - LCType values that are not in the tables below fail with
 *    ERROR_INVALID_PARAMETER instead of returning an invented value.
 *  - LOCALE_IDEFAULTCODEPAGE / LOCALE_IDEFAULTANSICODEPAGE report the
 *    profile's code page (65001, see GetACP/GetOEMCP) rather than Windows'
 *    437/1252, because that is the code page this bridge really uses.
 *  - LOCALE_FONTSIGNATURE, LOCALE_SKEYBOARDSTOINSTALL and the LOCALE_IINTL*
 *    numeric LCType values are not implemented. */

#define PE_LOCALE_NOUSEROVERRIDE 0x80000000u
#define PE_LOCALE_USE_CP_ACP     0x40000000u
#define PE_LOCALE_RETURN_NUMBER  0x20000000u
#define PE_LOCALE_TYPE_MASK      0x0fffffffu   /* LCType ids live below the flag nibble */
#define PE_LOCALE_FLAGS_MASK     0xf0000000u
#define PE_PROFILE_CODEPAGE      65001u

struct pe_locale_string_value { uint32_t id; const char *text; };
struct pe_locale_number_value { uint32_t id; uint32_t value; bool hex; };

static const char *const pe_locale_day_names[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};
static const char *const pe_locale_day_abbrev[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};
static const char *const pe_locale_month_names[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};
static const char *const pe_locale_month_abbrev[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
    "Nov", "Dec"
};
static const struct pe_locale_string_value pe_locale_strings[] = {
    { 0x00000002, "English (United States)" },  /* LOCALE_SLANGUAGE / SLOCALIZEDDISPLAYNAME */
    { 0x00000003, "ENU" },                      /* LOCALE_SABBREVLANGNAME */
    { 0x00000004, "English" },                  /* LOCALE_SNATIVELANGNAME */
    { 0x00000007, "USA" },                      /* LOCALE_SABBREVCTRYNAME */
    { 0x00000008, "United States" },            /* LOCALE_SNATIVECTRYNAME */
    { 0x0000000c, "," },                        /* LOCALE_SLIST */
    { 0x0000000e, "." },                        /* LOCALE_SDECIMAL */
    { 0x0000000f, "," },                        /* LOCALE_STHOUSAND */
    { 0x00000010, "3;0" },                      /* LOCALE_SGROUPING */
    { 0x00000013, "0123456789" },               /* LOCALE_SNATIVEDIGITS */
    { 0x00000014, "$" },                        /* LOCALE_SCURRENCY */
    { 0x00000015, "USD" },                      /* LOCALE_SINTLSYMBOL */
    { 0x00000016, "." },                        /* LOCALE_SMONDECIMALSEP */
    { 0x00000017, "," },                        /* LOCALE_SMONTHOUSANDSEP */
    { 0x0000001d, "/" },                        /* LOCALE_SDATE */
    { 0x0000001e, ":" },                        /* LOCALE_STIME */
    { 0x0000001f, "M/d/yyyy" },                 /* LOCALE_SSHORTDATE */
    { 0x00000020, "dddd, MMMM d, yyyy" },       /* LOCALE_SLONGDATE */
    { 0x00000028, "AM" },                       /* LOCALE_SAM / LOCALE_S1159 */
    { 0x00000029, "PM" },                       /* LOCALE_SPM / LOCALE_S2359 */
    { 0x00000050, "" },                         /* LOCALE_SPOSITIVESIGN */
    { 0x00000051, "-" },                        /* LOCALE_SNEGATIVESIGN */
    { 0x00000059, "en" },                       /* LOCALE_SISO639LANGNAME */
    { 0x0000005a, "US" },                       /* LOCALE_SISO3166CTRYNAME */
    { 0x0000005c, "en-US" },                    /* LOCALE_SNAME */
    { 0x00000072, "English (United States)" },  /* LOCALE_SENGLISHDISPLAYNAME */
    { 0x00000073, "English (United States)" },  /* LOCALE_SNATIVEDISPLAYNAME */
    { 0x00001001, "English" },                  /* LOCALE_SENGLANGUAGE */
    { 0x00001002, "United States" },            /* LOCALE_SENGCOUNTRY */
    { 0x00001003, "h:mm:ss tt" },               /* LOCALE_STIMEFORMAT */
};
static const struct pe_locale_number_value pe_locale_numbers[] = {
    { 0x00000001, 0x00000409, true },   /* LOCALE_ILANGUAGE (hexadecimal without the flag) */
    { 0x0000000b, PE_PROFILE_CODEPAGE, false },  /* LOCALE_IDEFAULTCODEPAGE */
    { 0x00001004, PE_PROFILE_CODEPAGE, false },  /* LOCALE_IDEFAULTANSICODEPAGE */
    { 0x00000011, 2, false },           /* LOCALE_IDIGITS */
    { 0x00000012, 1, false },           /* LOCALE_ILZERO */
    { 0x00000019, 2, false },           /* LOCALE_ICURRDIGITS */
    { 0x0000001b, 0, false },           /* LOCALE_ICURRENCY */
    { 0x0000001c, 1, false },           /* LOCALE_INEGCURR */
    { 0x00000021, 0, false },           /* LOCALE_IDATE */
    { 0x00000023, 0, false },           /* LOCALE_ITIME */
    { 0x00000024, 1, false },           /* LOCALE_ICENTURY */
    { 0x00000025, 0, false },           /* LOCALE_ITLZERO */
    { 0x00000026, 0, false },           /* LOCALE_IDAYLZERO */
    { 0x00000027, 0, false },           /* LOCALE_IMONLZERO */
    { 0x00001009, 1, false },           /* LOCALE_ICALENDARTYPE (GREGORIAN) */
    { 0x0000100c, 0, false },           /* LOCALE_IFIRSTDAYOFWEEK (Sunday) */
    { 0x0000100d, 0, false },           /* LOCALE_IFIRSTWEEKOFYEAR */
    { 0x00001010, 1, false },           /* LOCALE_INEGNUMBER */
};

static const char *pe_locale_string_lookup(uint32_t id)
{
    if (id >= 0x2a && id <= 0x30) return pe_locale_day_names[id - 0x2a];      /* SDAYNAME1..7 */
    if (id >= 0x31 && id <= 0x37) return pe_locale_day_abbrev[id - 0x31];     /* SABBREVDAYNAME1..7 */
    if (id >= 0x38 && id <= 0x43) return pe_locale_month_names[id - 0x38];    /* SMONTHNAME1..12 */
    if (id >= 0x44 && id <= 0x4f) return pe_locale_month_abbrev[id - 0x44];   /* SABBREVMONTHNAME1..12 */
    for (unsigned i = 0; i < sizeof(pe_locale_strings)/sizeof(pe_locale_strings[0]); i++)
        if (pe_locale_strings[i].id == id) return pe_locale_strings[i].text;
    return NULL;
}
static const struct pe_locale_number_value *pe_locale_number_lookup(uint32_t id)
{
    for (unsigned i = 0; i < sizeof(pe_locale_numbers)/sizeof(pe_locale_numbers[0]); i++)
        if (pe_locale_numbers[i].id == id) return &pe_locale_numbers[i];
    return NULL;
}
/* Numbers are reported as decimal text unless LOCALE_RETURN_NUMBER is used;
 * LOCALE_ILANGUAGE is the documented four-digit hexadecimal exception. */
static int pe_locale_number_text(const struct pe_locale_number_value *n, char *out)
{
    char reverse[12]; int count = 0; uint32_t value = n->value;
    if (n->hex) {                    /* least significant nibble first, like the decimal path */
        for (int i = 0; i < 4; i++)
            reverse[count++] = "0123456789abcdef"[(value >> (4 * i)) & 15];
    } else {
        if (!value) reverse[count++] = '0';
        while (value) { reverse[count++] = (char)('0' + value % 10); value /= 10; }
    }
    for (int i = 0; i < count; i++) out[i] = reverse[count - 1 - i];
    out[count] = 0;
    return count;
}
/* Returns the required TCHAR count including the terminator, or 0 on failure. */
static int pe_locale_ascii(const char *text, int length, void *out, int cap, bool ansi)
{
    int need = (length >= 0 ? length : (int)cstring_length(text)) + 1;
    if (!cap) return need;                       /* documented size query */
    if (!out || cap < need) { SetLastError(122); return 0; }
    if (ansi) {
        char *dest = out;
        for (int i = 0; i < need; i++) dest[i] = text[i];
    } else {
        uint16_t *dest = out;
        for (int i = 0; i < need; i++) dest[i] = (uint16_t)(unsigned char)text[i];
    }
    return need;
}
static int pe_locale_info(uint32_t locale, uint32_t type, void *out, int cap, bool ansi)
{
    if (!pe_locale_lcid_installed(locale)) { SetLastError(87); return 0; }
    if ((type & PE_LOCALE_FLAGS_MASK) &
        ~(PE_LOCALE_NOUSEROVERRIDE | PE_LOCALE_USE_CP_ACP | PE_LOCALE_RETURN_NUMBER)) {
        SetLastError(1004);                      /* ERROR_INVALID_FLAGS */
        return 0;
    }
    uint32_t id = type & PE_LOCALE_TYPE_MASK;
    const struct pe_locale_number_value *number = pe_locale_number_lookup(id);
    if (number) {
        if (type & PE_LOCALE_RETURN_NUMBER) {
            int need = ansi ? 4 : 2;             /* DWORD in TCHAR units */
            if (!cap) return need;
            if (!out || cap < need) { SetLastError(122); return 0; }
            *(uint32_t *)out = number->value;
            return need;
        }
        char text[12];
        return pe_locale_ascii(text, pe_locale_number_text(number, text), out, cap, ansi);
    }
    if (type & PE_LOCALE_RETURN_NUMBER) { SetLastError(87); return 0; }
    const char *text = pe_locale_string_lookup(id);
    if (!text) { SetLastError(87); return 0; }
    return pe_locale_ascii(text, -1, out, cap, ansi);
}

__attribute__((ms_abi)) int GetLocaleInfoW(uint32_t locale, uint32_t type, uint16_t *out, int cap)
{
    return pe_locale_info(locale, type, out, cap, false);
}
__attribute__((ms_abi)) int GetLocaleInfoA(uint32_t locale, uint32_t type, char *out, int cap)
{
    return pe_locale_info(locale, type, out, cap, true);
}
__attribute__((ms_abi)) int GetLocaleInfoEx(const uint16_t *name, uint32_t type, uint16_t *out, int cap)
{
    if (!pe_locale_name_is_en_us(name)) { SetLastError(87); return 0; }
    return pe_locale_info(PE_LCID_EN_US, type, out, cap, false);
}
