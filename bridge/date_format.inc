/* Explicit English Gregorian / UTC-local profile; other locales/calendars fail. */
struct pe_date_text {
	uint16_t *data;
	unsigned used, capacity;
	bool failed;
};
static void pe_date_put(struct pe_date_text *b, uint16_t c)
{
	if (b->failed)
		return;
	if (b->used == b->capacity) {
		unsigned cap = b->capacity ? b->capacity * 2 : 128;
		if (cap > 1048576) {
			b->failed = true;
			SetLastError(8);
			return;
		}
		uint16_t *p = pe_raw_map((size_t)cap * 2);
		if (!p) {
			b->failed = true;
			SetLastError(8);
			return;
		}
		if (b->used)
			memcpy(p, b->data, b->used * 2);
		if (b->data)
			raw_syscall3(SYS_munmap, (long)b->data, b->capacity * 2,
				     0);
		b->data = p;
		b->capacity = cap;
	}
	b->data[b->used++] = c;
}
static void pe_date_ascii(struct pe_date_text *b, const char *s, unsigned limit)
{
	for (unsigned i = 0; s[i] && i < limit; i++)
		pe_date_put(b, (unsigned char)s[i]);
}
static void pe_date_number(struct pe_date_text *b, unsigned value,
			   unsigned digits)
{
	char reverse[12];
	unsigned n = 0;
	do {
		reverse[n++] = '0' + value % 10;
		value /= 10;
	} while (value);
	while (n < digits && n < sizeof(reverse))
		reverse[n++] = '0';
	while (n)
		pe_date_put(b, reverse[--n]);
}
static bool pe_date_locale(DWORD locale)
{
	return locale == 0 || locale == 0x400 || locale == 0x800 ||
	       locale == 0x409 || locale == 0x7f || locale == 0xc00 ||
	       locale == 0x1000 || locale == 0x1400;
}
static int pe_datetime(bool date, DWORD locale, DWORD flags,
		       const struct pe_system_time *input,
		       const uint16_t *pattern, void *out, int cap, bool ansi)
{
	if (!pe_date_locale(locale) || cap < 0) {
		SetLastError(87);
		return 0;
	}
	DWORD options = flags & 0x7fffffffu;
	if ((date && ((options & ~11u) || (options && options != 1 &&
					   options != 2 && options != 8))) ||
	    (!date && (options & ~15u)) || (pattern && (flags & 0x80000000u))) {
		SetLastError(1004);
		return 0;
	}
	if (date && pattern && options) {
		SetLastError(1004);
		return 0;
	}
	struct pe_system_time t;
	if (input)
		t = *input;
	else
		GetLocalTime(&t);
	if (date) {
		struct pe_system_time midnight = t;
		midnight.hour = midnight.minute = midnight.second =
			midnight.millis = 0;
		struct pe_filetime ft;
		if (!SystemTimeToFileTime(&midnight, &ft) ||
		    !FileTimeToSystemTime(&ft, &midnight))
			return 0;
		t.weekday = midnight.weekday;
	} else if (t.hour > 23 || t.minute > 59 || t.second > 59 ||
		   t.millis > 999) {
		SetLastError(87);
		return 0;
	}
	static const uint16_t short_date[] = { 'M', '/', 'd', '/', 'y',
					       'y', 'y', 'y', 0 };
	static const uint16_t long_date[] = { 'd', 'd', 'd', 'd', ',', ' ', 'M',
					      'M', 'M', 'M', ' ', 'd', ',', ' ',
					      'y', 'y', 'y', 'y', 0 };
	static const uint16_t year_month[] = { 'M', 'M', 'M', 'M', ' ',
					       'y', 'y', 'y', 'y', 0 };
	static const uint16_t clock[] = { 'h', ':', 'm', 'm', ':', 's',
					  's', ' ', 't', 't', 0 };
	if (!pattern)
		pattern = date ? (options == 2 ? long_date :
				  options == 8 ? year_month :
						 short_date) :
				 clock;
	static const char *const days[] = { "Sunday",	 "Monday",   "Tuesday",
					    "Wednesday", "Thursday", "Friday",
					    "Saturday" };
	static const char *const months[] = {
		"January",   "February", "March",    "April",
		"May",	     "June",	 "July",     "August",
		"September", "October",	 "November", "December"
	};
	struct pe_date_text b = { 0 };
	unsigned last_field_end = 0;
	bool skip_separator = false;
	for (unsigned i = 0; pattern[i] && !b.failed;) {
		uint16_t c = pattern[i++];
		if (c == '\'') {
			if (pattern[i] == '\'') {
				pe_date_put(&b, '\'');
				i++;
				continue;
			}
			while (pattern[i]) {
				if (pattern[i] == '\'') {
					i++;
					if (pattern[i] == '\'') {
						pe_date_put(&b, '\'');
						i++;
						continue;
					}
					break;
				}
				pe_date_put(&b, pattern[i++]);
			}
			continue;
		}
		bool field =
			date ? (c == 'd' || c == 'M' || c == 'y' || c == 'g') :
			       (c == 'h' || c == 'H' || c == 'm' || c == 's' ||
				c == 't');
		if (!field) {
			if (!skip_separator ||
			    ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
				pe_date_put(&b, c);
			continue;
		}
		skip_separator = false;
		unsigned n = 1;
		while (pattern[i] == c) {
			n++;
			i++;
		}
		if (!date && ((c == 'm' && (options & 1)) ||
			      (c == 's' && (options & 3)) ||
			      (c == 't' && (options & 4)))) {
			b.used = last_field_end;
			if (c == 't')
				skip_separator = true;
			continue;
		}
		if (date) {
			if (c == 'd') {
				if (n >= 3)
					pe_date_ascii(&b, days[t.weekday],
						      n == 3 ? 3 : UINT32_MAX);
				else
					pe_date_number(&b, t.day, n);
			} else if (c == 'M') {
				if (n >= 3)
					pe_date_ascii(&b, months[t.month - 1],
						      n == 3 ? 3 : UINT32_MAX);
				else
					pe_date_number(&b, t.month, n);
			} else if (c == 'y')
				pe_date_number(&b,
					       n <= 2 ? t.year % 100 : t.year,
					       n == 1 ? 1 :
					       n > 5  ? 5 :
							n);
			else
				pe_date_ascii(&b, "AD", 2);
		} else {
			if (c == 't')
				pe_date_ascii(&b, t.hour < 12 ? "AM" : "PM",
					      n == 1 ? 1 : 2);
			else {
				unsigned value = c == 'm' ? t.minute :
						 c == 's' ? t.second :
							    t.hour;
				if (c == 'h' && !(options & 8))
					value = value % 12 ? value % 12 : 12;
				pe_date_number(&b, value, n > 1 ? 2 : 1);
			}
		}
		last_field_end = b.used;
	}
	pe_date_put(&b, 0);
	int result = 0;
	if (!b.failed) {
		if (ansi) {
			int needed = WideCharToMultiByte(
				65001, 0, b.data, b.used, NULL, 0, NULL, NULL);
			if (!cap)
				result = needed;
			else if (!out || cap < needed)
				SetLastError(122);
			else
				result = WideCharToMultiByte(65001, 0, b.data,
							     b.used, out, cap,
							     NULL, NULL);
		} else if (!cap)
			result = b.used;
		else if (!out || (unsigned)cap < b.used)
			SetLastError(122);
		else {
			memcpy(out, b.data, b.used * 2);
			result = b.used;
		}
	}
	if (b.data)
		raw_syscall3(SYS_munmap, (long)b.data, b.capacity * 2, 0);
	return result;
}
__attribute__((ms_abi)) int GetDateFormatW(DWORD locale, DWORD flags,
					   const struct pe_system_time *time,
					   const uint16_t *pattern,
					   uint16_t *out, int cap)
{
	return pe_datetime(true, locale, flags, time, pattern, out, cap, false);
}
__attribute__((ms_abi)) int GetTimeFormatW(DWORD locale, DWORD flags,
					   const struct pe_system_time *time,
					   const uint16_t *pattern,
					   uint16_t *out, int cap)
{
	return pe_datetime(false, locale, flags, time, pattern, out, cap,
			   false);
}
static int pe_datetime_ansi(bool date, DWORD locale, DWORD flags,
			    const struct pe_system_time *time,
			    const char *pattern, char *out, int cap)
{
	uint16_t *wide = NULL;
	int units = 0;
	if (pattern) {
		units = MultiByteToWideChar(65001, 0, pattern, -1, NULL, 0);
		if (!units)
			return 0;
		wide = pe_raw_map((size_t)units * 2);
		if (!wide) {
			SetLastError(8);
			return 0;
		}
		MultiByteToWideChar(65001, 0, pattern, -1, wide, units);
	}
	int result =
		pe_datetime(date, locale, flags, time, wide, out, cap, true);
	if (wide)
		raw_syscall3(SYS_munmap, (long)wide, (size_t)units * 2, 0);
	return result;
}
__attribute__((ms_abi)) int GetDateFormatA(DWORD locale, DWORD flags,
					   const struct pe_system_time *time,
					   const char *pattern, char *out,
					   int cap)
{
	return pe_datetime_ansi(true, locale, flags, time, pattern, out, cap);
}
__attribute__((ms_abi)) int GetTimeFormatA(DWORD locale, DWORD flags,
					   const struct pe_system_time *time,
					   const char *pattern, char *out,
					   int cap)
{
	return pe_datetime_ansi(false, locale, flags, time, pattern, out, cap);
}
static bool pe_date_locale_name(const uint16_t *name)
{
	return !name || !*name || utf16_ascii_equal_ci(name, "en-US") ||
	       utf16_ascii_equal_ci(name, "en");
}
__attribute__((ms_abi)) int GetDateFormatEx(const uint16_t *name, DWORD flags,
					    const struct pe_system_time *time,
					    const uint16_t *pattern,
					    uint16_t *out, int cap,
					    const uint16_t *calendar)
{
	if (!pe_date_locale_name(name) || calendar) {
		SetLastError(87);
		return 0;
	}
	return GetDateFormatW(0x409, flags, time, pattern, out, cap);
}
__attribute__((ms_abi)) int GetTimeFormatEx(const uint16_t *name, DWORD flags,
					    const struct pe_system_time *time,
					    const uint16_t *pattern,
					    uint16_t *out, int cap)
{
	if (!pe_date_locale_name(name)) {
		SetLastError(87);
		return 0;
	}
	return GetTimeFormatW(0x409, flags, time, pattern, out, cap);
}
