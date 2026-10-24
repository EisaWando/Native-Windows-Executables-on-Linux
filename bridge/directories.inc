/* Process-wide cwd follows the existing Z: -> / mapping. */
__attribute__((ms_abi)) BOOL SetCurrentDirectoryW(const uint16_t *path)
{
	char native[4096];
	if (!pe_linux_path(path, native, sizeof(native)))
		return 0;
	long r = raw_syscall3(SYS_chdir, (long)native, 0, 0);
	if (r < 0) {
		SetLastError(r == -2 || r == -20 ? 3 : pe_file_error(r));
		return 0;
	}
	return 1;
}
__attribute__((ms_abi)) BOOL CreateDirectoryW(const uint16_t *path,
					      void *security)
{
	if (security) {
		SetLastError(50);
		return 0;
	}
	char native[4096];
	if (!pe_linux_path(path, native, sizeof(native)))
		return 0;
	long r = raw_syscall3(SYS_mkdir, (long)native, 0777, 0);
	if (r < 0) {
		SetLastError(r == -17 ? 183 : pe_file_error(r));
		return 0;
	}
	return 1;
}
__attribute__((ms_abi)) BOOL RemoveDirectoryW(const uint16_t *path)
{
	char native[4096];
	if (!pe_linux_path(path, native, sizeof(native)))
		return 0;
	long r = raw_syscall3(SYS_rmdir, (long)native, 0, 0);
	if (r < 0) {
		SetLastError(r == -39 ? 145 : pe_file_error(r));
		return 0;
	}
	return 1;
}
__attribute__((ms_abi)) DWORD GetFullPathNameW(const uint16_t *path, DWORD cap,
					       uint16_t *out, uint16_t **part)
{
	char native[4096], joined[8192], normalized[8192];
	uint16_t wide[8192];
	if (!pe_linux_path(path, native, sizeof(native)))
		return 0;
	unsigned used = 0;
	if (native[0] != '/') {
		long n = raw_syscall3(SYS_getcwd, (long)joined, sizeof(joined),
				      0);
		if (n < 0) {
			SetLastError(3);
			return 0;
		}
		used = n - 1;
		if (used > 1)
			joined[used++] = '/';
	}
	size_t len = cstring_length(native);
	if (used + len + 1 > sizeof(joined)) {
		SetLastError(206);
		return 0;
	}
	memcpy(joined + used, native, len + 1);
	unsigned n = 1;
	normalized[0] = '/';
	const char *p = joined;
	while (*p) {
		while (*p == '/')
			p++;
		const char *start = p;
		while (*p && *p != '/')
			p++;
		size_t count = p - start;
		if (!count)
			break;
		if (count == 1 && start[0] == '.')
			continue;
		if (count == 2 && start[0] == '.' && start[1] == '.') {
			if (n > 1) {
				while (n > 1 && normalized[n - 1] != '/')
					n--;
				if (n > 1)
					n--;
			}
			continue;
		}
		if (n > 1)
			normalized[n++] = '/';
		memcpy(normalized + n, start, count);
		n += count;
	}
	bool trailing = len && native[len - 1] == '/';
	if (trailing && n > 1)
		normalized[n++] = '/';
	normalized[n] = 0;
	wide[0] = 'Z';
	wide[1] = ':';
	int units =
		MultiByteToWideChar(65001, 8, normalized, -1, wide + 2, 8190);
	if (!units)
		return 0;
	unsigned length = units + 1;
	for (unsigned i = 2; i < length; i++)
		if (wide[i] == '/')
			wide[i] = '\\';
	if (cap <= length)
		return length + 1;
	if (!out) {
		SetLastError(87);
		return 0;
	}
	memcpy(out, wide, (length + 1) * 2);
	if (part) {
		uint16_t *last = out + 2;
		for (uint16_t *s = out + 2; *s; s++)
			if (*s == '\\')
				last = s + 1;
		*part = *last ? last : NULL;
	}
	return length;
}
__attribute__((ms_abi)) BOOL SetCurrentDirectoryA(const char *path)
{
	uint16_t w[4096];
	return path && MultiByteToWideChar(65001, 8, path, -1, w, 4096) ?
		       SetCurrentDirectoryW(w) :
		       (SetLastError(87), 0);
}
__attribute__((ms_abi)) BOOL CreateDirectoryA(const char *path, void *security)
{
	uint16_t w[4096];
	return path && MultiByteToWideChar(65001, 8, path, -1, w, 4096) ?
		       CreateDirectoryW(w, security) :
		       (SetLastError(87), 0);
}
__attribute__((ms_abi)) BOOL RemoveDirectoryA(const char *path)
{
	uint16_t w[4096];
	return path && MultiByteToWideChar(65001, 8, path, -1, w, 4096) ?
		       RemoveDirectoryW(w) :
		       (SetLastError(87), 0);
}
__attribute__((ms_abi)) DWORD GetFullPathNameA(const char *path, DWORD cap,
					       char *out, char **part)
{
	uint16_t w[4096], full[8192];
	if (!path || !MultiByteToWideChar(65001, 8, path, -1, w, 4096)) {
		SetLastError(87);
		return 0;
	}
	DWORD n = GetFullPathNameW(w, 8192, full, NULL);
	if (!n || n >= 8192) {
		SetLastError(206);
		return 0;
	}
	int bytes =
		WideCharToMultiByte(65001, 0, full, -1, NULL, 0, NULL, NULL);
	if (!bytes)
		return 0;
	if (cap < (DWORD)bytes)
		return bytes;
	if (!out) {
		SetLastError(87);
		return 0;
	}
	if (!WideCharToMultiByte(65001, 0, full, -1, out, cap, NULL, NULL))
		return 0;
	if (part) {
		char *last = out + 2;
		for (char *s = out + 2; *s; s++)
			if (*s == '\\')
				last = s + 1;
		*part = *last ? last : NULL;
	}
	return bytes - 1;
}
/* DPI/font setup's checked signed multiply/divide, nearest with ties away. */
__attribute__((ms_abi)) int32_t MulDiv(int32_t number, int32_t numerator,
				       int32_t denominator)
{
	if (!denominator)
		return -1;
	int64_t product = (int64_t)number * numerator, d = denominator,
		q = product / d, r = product % d;
	uint64_t rem = r < 0 ? -r : r, div = d < 0 ? -d : d;
	if (rem * 2 >= div)
		q += ((product < 0) ^ (d < 0)) ? -1 : 1;
	return q < INT32_MIN || q > INT32_MAX ? -1 : q;
}
