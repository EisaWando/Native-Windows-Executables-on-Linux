/* Read-only identity projection, not a Windows ACL/impersonation engine.
 * Linux euid 0 maps to elevated Administrators; ordinary users do not. */
struct pe_sid {
	uint8_t revision, count, authority[6];
	uint32_t sub[15];
};
__attribute__((ms_abi)) BOOL IsValidSid(const void *ptr)
{
	const struct pe_sid *s = ptr;
	return s && s->revision == 1 && s->count <= 15;
}
__attribute__((ms_abi)) DWORD GetLengthSid(const void *s)
{
	if (!IsValidSid(s)) {
		SetLastError(1337);
		return 0;
	}
	return 8 + 4 * ((const struct pe_sid *)s)->count;
}
__attribute__((ms_abi)) BOOL AllocateAndInitializeSid(const uint8_t *authority,
						      uint8_t count, DWORD a,
						      DWORD b, DWORD c, DWORD d,
						      DWORD e, DWORD f, DWORD g,
						      DWORD h, void **out)
{
	if (!authority || !out || count > 8) {
		SetLastError(87);
		return 0;
	}
	struct pe_sid *s = pe_heap_alloc(8 + 4 * count, PE_HEAP_ZERO);
	if (!s) {
		SetLastError(8);
		return 0;
	}
	s->revision = 1;
	s->count = count;
	memcpy(s->authority, authority, 6);
	DWORD values[] = { a, b, c, d, e, f, g, h };
	memcpy(s->sub, values, count * 4);
	*out = s;
	return 1;
}
__attribute__((ms_abi)) void *FreeSid(void *sid)
{
	if (sid)
		pe_heap_free(sid);
	return NULL;
}
__attribute__((ms_abi)) BOOL EqualSid(const void *a, const void *b)
{
	if (!IsValidSid(a) || !IsValidSid(b))
		return 0;
	DWORD n = GetLengthSid(a);
	return n == GetLengthSid(b) && !memcmp(a, b, n);
}
__attribute__((ms_abi)) BOOL CopySid(DWORD size, void *out, const void *sid)
{
	if (!out || !IsValidSid(sid)) {
		SetLastError(1337);
		return 0;
	}
	DWORD n = GetLengthSid(sid);
	if (size < n) {
		SetLastError(122);
		return 0;
	}
	memcpy(out, sid, n);
	return 1;
}
__attribute__((ms_abi)) DWORD *GetSidSubAuthority(void *sid, DWORD index)
{
	struct pe_sid *s = sid;
	if (!IsValidSid(s) || index >= s->count) {
		SetLastError(1337);
		return NULL;
	}
	return &s->sub[index];
}
__attribute__((ms_abi)) uint8_t *GetSidSubAuthorityCount(void *sid)
{
	if (!IsValidSid(sid)) {
		SetLastError(1337);
		return NULL;
	}
	return &((struct pe_sid *)sid)->count;
}
__attribute__((ms_abi)) void *GetSidIdentifierAuthority(void *sid)
{
	if (!IsValidSid(sid)) {
		SetLastError(1337);
		return NULL;
	}
	return ((struct pe_sid *)sid)->authority;
}
static struct pe_sid pe_identity_sid(DWORD uid)
{
	return (struct pe_sid){
		.revision = 1,
		.count = 5,
		.authority = { 0, 0, 0, 0, 0, 5 },
		.sub = { 21, 0x73b34075, 0x94af4b77, 0xbcf1c21a,
			 uid >= UINT32_MAX - 1000 ? uid : uid + 1000 }
	};
}
static bool pe_sid_is(const struct pe_sid *s, unsigned authority,
		      unsigned count, DWORD a, DWORD b)
{
	static const uint8_t zero[5];
	return s->count == count && !memcmp(s->authority, zero, 5) &&
	       s->authority[5] == authority && s->sub[0] == a &&
	       (count < 2 || s->sub[1] == b);
}
__attribute__((ms_abi)) BOOL CheckTokenMembership(HANDLE token, const void *sid,
						  BOOL *member)
{
	if (!member || !IsValidSid(sid)) {
		SetLastError(87);
		return 0;
	}
	if (token) {
		PE_STATE_GUARD;
		if (!pe_wait_lookup(token, PE_TOKEN))
			return 0;
		SetLastError(1309);
		return 0;
	}
	DWORD uid = raw_syscall3(SYS_geteuid, 0, 0, 0);
	const struct pe_sid *s = sid;
	struct pe_sid user = pe_identity_sid(uid);
	*member = EqualSid(s, &user) || pe_sid_is(s, 1, 1, 0, 0) ||
		  pe_sid_is(s, 5, 1, 11, 0) || pe_sid_is(s, 5, 1, 4, 0) ||
		  pe_sid_is(s, 2, 1, 0, 0) || pe_sid_is(s, 5, 2, 32, 545) ||
		  (!uid && pe_sid_is(s, 5, 2, 32, 544));
	return 1;
}
__attribute__((ms_abi)) BOOL OpenProcessToken(HANDLE process, DWORD access,
					      HANDLE *out)
{
	if (process != (HANDLE)(intptr_t)-1) {
		SetLastError(6);
		return 0;
	}
	if (!out) {
		SetLastError(87);
		return 0;
	}
	if (access & 0x12000000u)
		access = 0xf01ff;
	else if (access & 0x80000000u)
		access = 0x20008;
	if (access & ~0xf01ffu) {
		SetLastError(5);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_wait_object *o = pe_raw_map(sizeof(*o));
	if (!o) {
		SetLastError(8);
		return 0;
	}
	o->kind = PE_TOKEN;
	o->u.token.uid = raw_syscall3(SYS_geteuid, 0, 0, 0);
	o->u.token.access = access;
	o->next = pe_wait_objects;
	pe_wait_objects = o;
	HANDLE h = pe_wait_handle_new(o);
	if (!h) {
		pe_wait_reap();
		return 0;
	}
	*out = h;
	return 1;
}
__attribute__((ms_abi)) BOOL OpenThreadToken(HANDLE thread, DWORD access,
					     BOOL self, HANDLE *out)
{
	(void)access;
	(void)self;
	if (!out) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	if (!pe_wait_lookup(thread, PE_WAIT_THREAD))
		return 0;
	SetLastError(1008);
	return 0;
}
struct pe_sid_attributes {
	void *sid;
	DWORD attributes, pad;
};
__attribute__((ms_abi)) BOOL GetTokenInformation(HANDLE h, unsigned cls,
						 void *out, DWORD cap,
						 DWORD *required)
{
	if (!required) {
		SetLastError(87);
		return 0;
	}
	PE_STATE_GUARD;
	struct pe_wait_object *o = pe_wait_lookup(h, PE_TOKEN);
	if (!o)
		return 0;
	if (!(o->u.token.access & 8)) {
		SetLastError(5);
		return 0;
	}
	DWORD uid = o->u.token.uid, value = 0, size = 4;
	bool sid = false;
	struct pe_sid identity = pe_identity_sid(uid);
	DWORD attrs = 0;
	switch (cls) {
	case 1:
	case 4:
		sid = true;
		size = (cls == 1 ? 16 : 8) + 28;
		break;
	case 8:
		value = 1;
		break; /* primary */
	case 12:
		value = 1;
		break; /* emulated interactive session */
	case 18:
		value = 1;
		break; /* no linked UAC token */
	case 20:
		value = !uid;
		break;
	case 21:
	case 23:
	case 24:
	case 26:
	case 29:
		value = 0;
		break;
	case 25:
		sid = true;
		identity = (struct pe_sid){ .revision = 1,
					    .count = 1,
					    .authority = { 0, 0, 0, 0, 0, 16 },
					    .sub = { uid ? 8192 : 12288 } };
		size = 28;
		attrs = 0x20;
		break;
	default:
		SetLastError(50);
		return 0;
	}
	*required = size;
	if (!out || cap < size) {
		SetLastError(122);
		return 0;
	}
	memset(out, 0, size);
	if (sid) {
		unsigned offset = cls == 4 ? 8 : 16;
		*(void **)out = (char *)out + offset;
		if (cls != 4)
			((struct pe_sid_attributes *)out)->attributes = attrs;
		memcpy((char *)out + offset, &identity, size - offset);
	} else
		*(DWORD *)out = value;
	return 1;
}
