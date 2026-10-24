/* Original FIPS 180-4 SHA-256 and RFC 2104 HMAC implementation. No libc. */
struct pe_sha256 {
	uint32_t h[8];
	uint64_t bytes;
	unsigned used;
	unsigned char block[64];
};
static const uint32_t pe_sha_k[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
	0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
	0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
	0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0xfc19dc6u,  0x240ca1ccu,
	0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
	0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
	0x6ca6351u,  0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
	0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
	0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
	0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
	0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
	0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};
static const uint32_t pe_sha_initial[8] = { 0x6a09e667u, 0xbb67ae85u,
					    0x3c6ef372u, 0xa54ff53au,
					    0x510e527fu, 0x9b05688cu,
					    0x1f83d9abu, 0x5be0cd19u };
static uint32_t pe_ror(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}
static void pe_sha_block(struct pe_sha256 *s, const unsigned char *block)
{
	uint32_t w[64];
	for (unsigned i = 0; i < 16; i++)
		w[i] = (uint32_t)block[i * 4] << 24 |
		       (uint32_t)block[i * 4 + 1] << 16 |
		       (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
	for (unsigned i = 16; i < 64; i++) {
		uint32_t a = w[i - 15], b = w[i - 2];
		w[i] = w[i - 16] + (pe_ror(a, 7) ^ pe_ror(a, 18) ^ (a >> 3)) +
		       w[i - 7] + (pe_ror(b, 17) ^ pe_ror(b, 19) ^ (b >> 10));
	}
	uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3],
		 e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (unsigned i = 0; i < 64; i++) {
		uint32_t t = h +
			     (pe_ror(e, 6) ^ pe_ror(e, 11) ^ pe_ror(e, 25)) +
			     ((e & f) ^ (~e & g)) + pe_sha_k[i] + w[i];
		uint32_t u = (pe_ror(a, 2) ^ pe_ror(a, 13) ^ pe_ror(a, 22)) +
			     ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t;
		d = c;
		c = b;
		b = a;
		a = t + u;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}
static void pe_sha_init(struct pe_sha256 *s)
{
	memset(s, 0, sizeof(*s));
	memcpy(s->h, pe_sha_initial, sizeof(s->h));
}
static void pe_sha_update(struct pe_sha256 *s, const void *data, size_t count)
{
	const unsigned char *p = data;
	s->bytes += count;
	while (count) {
		size_t n = 64 - s->used;
		if (n > count)
			n = count;
		memcpy(s->block + s->used, p, n);
		s->used += n;
		p += n;
		count -= n;
		if (s->used == 64) {
			pe_sha_block(s, s->block);
			s->used = 0;
		}
	}
}
static void pe_sha_finish(struct pe_sha256 *s, unsigned char out[32])
{
	uint64_t bits = s->bytes * 8;
	s->block[s->used++] = 128;
	if (s->used > 56) {
		memset(s->block + s->used, 0, 64 - s->used);
		pe_sha_block(s, s->block);
		s->used = 0;
	}
	memset(s->block + s->used, 0, 56 - s->used);
	for (unsigned i = 0; i < 8; i++)
		s->block[63 - i] = bits >> (i * 8);
	pe_sha_block(s, s->block);
	for (unsigned i = 0; i < 32; i++)
		out[i] = s->h[i / 4] >> (24 - 8 * (i % 4));
}
#define PE_STATUS_BAD_HANDLE ((int32_t)0xc0000008)
#define PE_STATUS_PARAMETER ((int32_t)0xc000000d)
#define PE_STATUS_NOMEM ((int32_t)0xc0000017)
#define PE_STATUS_SMALL ((int32_t)0xc0000023)
#define PE_STATUS_UNSUPPORTED ((int32_t)0xc00000bb)
#define PE_STATUS_NOT_FOUND ((int32_t)0xc0000225)
struct pe_crypto_state {
	struct pe_sha256 current, initial, outer;
	bool hmac, reusable, finished;
};
struct pe_crypto {
	struct pe_crypto *next;
	HANDLE handle;
	unsigned kind, refs;
	uint32_t lock;
	DWORD flags;
	struct pe_crypto_state hash;
};
static struct pe_crypto *pe_crypto_objects;
static unsigned pe_crypto_serial;
static struct pe_crypto *pe_crypto_new(unsigned kind)
{
	PE_STATE_GUARD;
	if (pe_crypto_serial >= 0x800000)
		return NULL;
	struct pe_crypto *o = pe_raw_map(sizeof(*o));
	if (!o)
		return NULL;
	o->handle = (HANDLE)(uintptr_t)(0x46000000ULL + 4 * pe_crypto_serial++);
	o->kind = kind;
	o->refs = 1;
	o->next = pe_crypto_objects;
	pe_crypto_objects = o;
	return o;
}
static struct pe_crypto *pe_crypto_pin(HANDLE h, unsigned kind)
{
	struct pe_crypto *o = NULL;
	{
		PE_STATE_GUARD;
		for (o = pe_crypto_objects; o; o = o->next)
			if (o->handle == h && (!kind || o->kind == kind)) {
				o->refs++;
				break;
			}
	}
	if (!o)
		return NULL;
	while (__atomic_exchange_n(&o->lock, 1, __ATOMIC_ACQUIRE))
		raw_syscall6(SYS_futex, (long)&o->lock, 128, 1, 0, 0, 0);
	return o;
}
static void pe_crypto_drop(struct pe_crypto *o)
{
	PE_STATE_GUARD;
	if (!--o->refs) {
		volatile unsigned char *p = (void *)o;
		for (size_t i = 0; i < sizeof(*o); i++)
			p[i] = 0;
		raw_syscall3(SYS_munmap, (long)o, sizeof(*o), 0);
	}
}
static void pe_crypto_unpin(struct pe_crypto *o)
{
	__atomic_store_n(&o->lock, 0, __ATOMIC_RELEASE);
	raw_syscall6(SYS_futex, (long)&o->lock, 129, 1, 0, 0, 0);
	pe_crypto_drop(o);
}
static int32_t pe_crypto_close(HANDLE h, unsigned kind)
{
	PE_STATE_GUARD;
	struct pe_crypto **link = &pe_crypto_objects;
	while (*link && ((*link)->handle != h || (*link)->kind != kind))
		link = &(*link)->next;
	if (!*link)
		return PE_STATUS_BAD_HANDLE;
	struct pe_crypto *o = *link;
	*link = o->next;
	pe_crypto_drop(o);
	return 0;
}
__attribute__((ms_abi)) int32_t
BCryptOpenAlgorithmProvider(HANDLE *out, const uint16_t *name,
			    const uint16_t *provider, DWORD flags)
{
	if (!out || !name || (flags & ~0x28u))
		return PE_STATUS_PARAMETER;
	if (!utf16_ascii_equal_ci(name, "SHA256") ||
	    (provider &&
	     !utf16_ascii_equal_ci(provider, "Microsoft Primitive Provider")))
		return PE_STATUS_NOT_FOUND;
	struct pe_crypto *o = pe_crypto_new(1);
	if (!o)
		return PE_STATUS_NOMEM;
	o->flags = flags;
	*out = o->handle;
	return 0;
}
__attribute__((ms_abi)) int32_t BCryptCloseAlgorithmProvider(HANDLE h,
							     DWORD flags)
{
	return flags ? PE_STATUS_PARAMETER : pe_crypto_close(h, 1);
}
__attribute__((ms_abi)) int32_t BCryptGetProperty(HANDLE h,
						  const uint16_t *name,
						  void *out, DWORD cap,
						  DWORD *size, DWORD flags)
{
	if (!name || !size || flags)
		return PE_STATUS_PARAMETER;
	struct pe_crypto *o = pe_crypto_pin(h, 0);
	if (!o)
		return PE_STATUS_BAD_HANDLE;
	DWORD value = 0, n = 4;
	uint16_t algorithm[] = { 'S', 'H', 'A', '2', '5', '6', 0 };
	const void *data = &value;
	int32_t r = 0;
	if (utf16_ascii_equal_ci(name, "ObjectLength"))
		value = sizeof(struct pe_crypto_state);
	else if (utf16_ascii_equal_ci(name, "HashDigestLength"))
		value = 32;
	else if (utf16_ascii_equal_ci(name, "HashBlockLength"))
		value = 64;
	else if (utf16_ascii_equal_ci(name, "IsKeyedHash"))
		value = o->kind == 1 ? !!(o->flags & 8) : o->hash.hmac;
	else if (utf16_ascii_equal_ci(name, "AlgorithmName")) {
		data = algorithm;
		n = sizeof(algorithm);
	} else
		r = PE_STATUS_UNSUPPORTED;
	if (!r) {
		*size = n;
		if (!out || cap < n)
			r = PE_STATUS_SMALL;
		else
			memcpy(out, data, n);
	}
	pe_crypto_unpin(o);
	return r;
}
__attribute__((ms_abi)) int32_t BCryptCreateHash(HANDLE algorithm, HANDLE *out,
						 void *buffer, DWORD cap,
						 const void *key, DWORD keylen,
						 DWORD flags)
{
	if (!out || (keylen && !key) || (flags & ~0x20u) ||
	    (buffer && cap < sizeof(struct pe_crypto_state)))
		return PE_STATUS_PARAMETER;
	struct pe_crypto *a = pe_crypto_pin(algorithm, 1);
	if (!a)
		return PE_STATUS_BAD_HANDLE;
	DWORD aflags = a->flags;
	pe_crypto_unpin(a);
	if (!(aflags & 8) && keylen)
		return PE_STATUS_PARAMETER;
	struct pe_crypto *o = pe_crypto_new(2);
	if (!o)
		return PE_STATUS_NOMEM;
	o->hash.hmac = !!(aflags & 8);
	o->hash.reusable = !!((aflags | flags) & 0x20);
	pe_sha_init(&o->hash.initial);
	if (o->hash.hmac) {
		unsigned char normalized[64] = { 0 }, pad[64];
		if (keylen > 64) {
			struct pe_sha256 temp;
			pe_sha_init(&temp);
			pe_sha_update(&temp, key, keylen);
			pe_sha_finish(&temp, normalized);
		} else if (keylen)
			memcpy(normalized, key, keylen);
		for (unsigned i = 0; i < 64; i++)
			pad[i] = normalized[i] ^ 0x36;
		pe_sha_update(&o->hash.initial, pad, 64);
		pe_sha_init(&o->hash.outer);
		for (unsigned i = 0; i < 64; i++)
			pad[i] = normalized[i] ^ 0x5c;
		pe_sha_update(&o->hash.outer, pad, 64);
	}
	o->hash.current = o->hash.initial;
	*out = o->handle;
	return 0;
}
__attribute__((ms_abi)) int32_t BCryptHashData(HANDLE h, const void *data,
					       DWORD size, DWORD flags)
{
	if (flags || (size && !data))
		return PE_STATUS_PARAMETER;
	struct pe_crypto *o = pe_crypto_pin(h, 2);
	if (!o)
		return PE_STATUS_BAD_HANDLE;
	int32_t r = o->hash.finished ? PE_STATUS_PARAMETER : 0;
	if (!r && size)
		pe_sha_update(&o->hash.current, data, size);
	pe_crypto_unpin(o);
	return r;
}
__attribute__((ms_abi)) int32_t BCryptFinishHash(HANDLE h, void *out,
						 DWORD size, DWORD flags)
{
	if (flags || !out || size != 32)
		return PE_STATUS_PARAMETER;
	struct pe_crypto *o = pe_crypto_pin(h, 2);
	if (!o)
		return PE_STATUS_BAD_HANDLE;
	int32_t r = o->hash.finished ? PE_STATUS_PARAMETER : 0;
	if (!r) {
		struct pe_sha256 s = o->hash.current;
		unsigned char digest[32];
		pe_sha_finish(&s, digest);
		if (o->hash.hmac) {
			s = o->hash.outer;
			pe_sha_update(&s, digest, 32);
			pe_sha_finish(&s, digest);
		}
		memcpy(out, digest, 32);
		if (o->hash.reusable)
			o->hash.current = o->hash.initial;
		else
			o->hash.finished = true;
	}
	pe_crypto_unpin(o);
	return r;
}
__attribute__((ms_abi)) int32_t BCryptDestroyHash(HANDLE h)
{
	return pe_crypto_close(h, 2);
}
__attribute__((ms_abi)) int32_t BCryptDuplicateHash(HANDLE h, HANDLE *out,
						    void *buffer, DWORD cap,
						    DWORD flags)
{
	if (!out || flags || (buffer && cap < sizeof(struct pe_crypto_state)))
		return PE_STATUS_PARAMETER;
	struct pe_crypto *o = pe_crypto_pin(h, 2);
	if (!o)
		return PE_STATUS_BAD_HANDLE;
	struct pe_crypto_state state = o->hash;
	pe_crypto_unpin(o);
	if (state.finished)
		return PE_STATUS_PARAMETER;
	struct pe_crypto *n = pe_crypto_new(2);
	if (!n)
		return PE_STATUS_NOMEM;
	n->hash = state;
	*out = n->handle;
	return 0;
}
__attribute__((ms_abi)) int32_t BCryptHash(HANDLE algorithm, const void *key,
					   DWORD keylen, const void *data,
					   DWORD size, void *out, DWORD outsize)
{
	HANDLE h;
	int32_t r = BCryptCreateHash(algorithm, &h, NULL, 0, key, keylen, 0);
	if (r)
		return r;
	r = BCryptHashData(h, data, size, 0);
	if (!r)
		r = BCryptFinishHash(h, out, outsize, 0);
	BCryptDestroyHash(h);
	return r;
}
__attribute__((ms_abi)) int32_t BCryptGenRandom(HANDLE algorithm, void *out,
						DWORD size, DWORD flags)
{
	if (algorithm || flags != 2 || (size && !out))
		return PE_STATUS_PARAMETER;
	unsigned char *p = out;
	while (size) {
		long n = raw_syscall3(SYS_getrandom, (long)p, size, 0);
		if (n == -4)
			continue;
		if (n <= 0)
			return PE_STATUS_UNSUPPORTED;
		p += n;
		size -= n;
	}
	return 0;
}
