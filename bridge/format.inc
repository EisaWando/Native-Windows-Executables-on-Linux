/* Original legacy USER32 formatting; public wsprintf contracts, Windows va_list.
 * Integer/string formats only, UTF-8 ACP. Bound output to the documented 1KiB. */
static unsigned pe_fmt_unit(const void *s, bool wide, unsigned i)
{
	return wide ? ((const uint16_t *)s)[i] : ((const unsigned char *)s)[i];
}
static void pe_fmt_put(void *out, bool wide, unsigned *used, unsigned value)
{
	unsigned cap = wide ? 512 : 1024;
	if (*used + 1 < cap) {
		if (wide)
			((uint16_t *)out)[*used] = value;
		else
			((char *)out)[*used] = value;
		(*used)++;
	}
}
static const uint16_t pe_fmt_null[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
static int pe_format(void *out, const void *format, bool wide,
		     __builtin_ms_va_list args)
{
	if (!out || !format) {
		SetLastError(87);
		return 0;
	}
	unsigned used = 0;
	for (unsigned pos = 0;
	     pe_fmt_unit(format, wide, pos) && used < (wide ? 511u : 1023u);) {
		unsigned c = pe_fmt_unit(format, wide, pos++);
		if (c != '%') {
			pe_fmt_put(out, wide, &used, c);
			continue;
		}
		bool left = false, zero = false, prefix = false, plus = false,
		     blank = false;
		for (;;) {
			c = pe_fmt_unit(format, wide, pos);
			if (c == '-')
				left = true;
			else if (c == '0')
				zero = true;
			else if (c == '#')
				prefix = true;
			else if (c == '+')
				plus = true;
			else if (c == ' ')
				blank = true;
			else
				break;
			pos++;
		}
		unsigned width = 0;
		int precision = -1;
		while ((c = pe_fmt_unit(format, wide, pos)) >= '0' &&
		       c <= '9') {
			width = width > 102 ? 1024 : width * 10 + c - '0';
			pos++;
		}
		if (c == '.') {
			pos++;
			precision = 0;
			while ((c = pe_fmt_unit(format, wide, pos)) >= '0' &&
			       c <= '9') {
				precision = precision > 102 ?
						    1024 :
						    precision * 10 + c - '0';
				pos++;
			}
		}
		int string_wide = -1, short_value = 0;
		bool bits64 = false;
		if (c == 'h') {
			short_value = 1;
			string_wide = 0;
			pos++;
		} else if (c == 'l' || c == 'w') {
			string_wide = 1;
			pos++;
			if (c == 'l' && pe_fmt_unit(format, wide, pos) == 'l') {
				bits64 = true;
				pos++;
			}
		} else if (c == 'I') {
			bits64 = true;
			pos++;
			if (pe_fmt_unit(format, wide, pos) == '6' &&
			    pe_fmt_unit(format, wide, pos + 1) == '4')
				pos += 2;
			else if (pe_fmt_unit(format, wide, pos) == '3' &&
				 pe_fmt_unit(format, wide, pos + 1) == '2') {
				bits64 = false;
				pos += 2;
			}
		}
		c = pe_fmt_unit(format, wide, pos);
		if (!c)
			break;
		pos++;
		uint16_t wide_buf[1024];
		char bytes[4096];
		const void *text = NULL;
		unsigned length = 0;
		char numeric[80], reverse[32], head[3];
		unsigned head_count = 0, digits_zero = 0;
		bool number = false;
		if (c == 's' || c == 'S') {
			const void *source =
				__builtin_va_arg(args, const void *);
			bool src_wide = string_wide < 0 ?
						(c == 's' ? wide : !wide) :
						string_wide;
			if (!source)
				source = src_wide ? (const void *)pe_fmt_null :
						    (const void *)"(null)";
			unsigned n = 0;
			while (n < 1023 && pe_fmt_unit(source, src_wide, n))
				n++;
			if (src_wide == wide) {
				text = source;
				length = n;
			} else if (wide) {
				int units = n ? MultiByteToWideChar(65001, 0,
								    source, n,
								    wide_buf,
								    1024) :
						0;
				if (n && !units)
					return 0;
				text = wide_buf;
				length = units;
			} else {
				int count = n ? WideCharToMultiByte(65001, 0,
								    source, n,
								    bytes, 4096,
								    NULL,
								    NULL) :
						0;
				if (n && !count)
					return 0;
				text = bytes;
				length = count;
			}
			if (precision >= 0 && length > (unsigned)precision)
				length = precision;
		} else if (c == 'c' || c == 'C') {
			unsigned v = __builtin_va_arg(args, unsigned);
			bool src_wide = string_wide < 0 ?
						(c == 'c' ? wide : !wide) :
						string_wide;
			if (src_wide && !wide) {
				uint16_t value = v;
				int n = WideCharToMultiByte(65001, 0, &value, 1,
							    bytes, 4, NULL,
							    NULL);
				text = bytes;
				length = n;
			} else {
				wide_buf[0] = src_wide ? v : (unsigned char)v;
				bytes[0] = v;
				text = wide ? (const void *)wide_buf :
					      (const void *)bytes;
				length = v ? 1 : 0;
			}
		} else if (c == 'd' || c == 'i' || c == 'u' || c == 'x' ||
			   c == 'X' || c == 'p' || c == 'o') {
			bool sign = c == 'd' || c == 'i',
			     upper = c == 'X' || c == 'p';
			uint64_t value =
				bits64 || c == 'p' ?
					__builtin_va_arg(args, uint64_t) :
					__builtin_va_arg(args, unsigned);
			if (short_value)
				value = sign ? (uint64_t)(int64_t)(int16_t)
							value :
					       (uint16_t)value;
			else if (sign && !bits64)
				value = (uint64_t)(int64_t)(int32_t)value;
			bool negative = sign && (int64_t)value < 0;
			if (negative) {
				head[head_count++] = '-';
				value = 0 - value;
			} else if (sign && plus)
				head[head_count++] = '+';
			else if (sign && blank)
				head[head_count++] = ' ';
			unsigned base = c == 'x' || c == 'X' || c == 'p' ? 16 :
					c == 'o'			 ? 8 :
									   10;
			if (prefix && base == 16 && value) {
				head[head_count++] = '0';
				head[head_count++] = upper ? 'X' : 'x';
			}
			unsigned n = 0;
			do {
				unsigned d = value % base;
				reverse[n++] =
					d < 10 ? '0' + d :
						 (upper ? 'A' : 'a') + d - 10;
				value /= base;
			} while (value);
			if (c == 'p' && precision < 16)
				precision = 16;
			if (precision > (int)n)
				digits_zero = precision - n;
			while (n)
				numeric[length++] = reverse[--n];
			text = numeric;
			number = true;
		} else {
			bytes[0] = c;
			wide_buf[0] = c;
			text = wide ? (const void *)wide_buf :
				      (const void *)bytes;
			length = 1;
		}
		unsigned total = length + head_count + digits_zero,
			 padding = width > total ? width - total : 0;
		if (!left && !(zero && number))
			for (unsigned i = 0; i < padding; i++)
				pe_fmt_put(out, wide, &used, ' ');
		for (unsigned i = 0; i < head_count; i++)
			pe_fmt_put(out, wide, &used, head[i]);
		if (!left && zero && number)
			for (unsigned i = 0; i < padding; i++)
				pe_fmt_put(out, wide, &used, '0');
		for (unsigned i = 0; i < digits_zero; i++)
			pe_fmt_put(out, wide, &used, '0');
		for (unsigned i = 0; i < length; i++)
			pe_fmt_put(out, wide, &used,
				   number ? (unsigned char)numeric[i] :
					    pe_fmt_unit(text, wide, i));
		if (left)
			for (unsigned i = 0; i < padding; i++)
				pe_fmt_put(out, wide, &used, ' ');
	}
	if (wide)
		((uint16_t *)out)[used] = 0;
	else
		((char *)out)[used] = 0;
	return used;
}
__attribute__((ms_abi)) int wsprintfW(uint16_t *out, const uint16_t *format,
				      ...)
{
	__builtin_ms_va_list args;
	__builtin_ms_va_start(args, format);
	int result = pe_format(out, format, true, args);
	__builtin_ms_va_end(args);
	return result;
}
__attribute__((ms_abi)) int wsprintfA(char *out, const char *format, ...)
{
	__builtin_ms_va_list args;
	__builtin_ms_va_start(args, format);
	int result = pe_format(out, format, false, args);
	__builtin_ms_va_end(args);
	return result;
}
__attribute__((ms_abi)) int wvsprintfW(uint16_t *out, const uint16_t *format,
				       __builtin_ms_va_list args)
{
	return pe_format(out, format, true, args);
}
__attribute__((ms_abi)) int wvsprintfA(char *out, const char *format,
				       __builtin_ms_va_list args)
{
	return pe_format(out, format, false, args);
}
