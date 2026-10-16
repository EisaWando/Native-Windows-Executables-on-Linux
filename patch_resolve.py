import re

with open("win32_bridge.c", "r") as f:
    text = f.read()

old_res = """static void resolve_imports(uintptr_t image_base, uint32_t import_rva,
			    uint32_t import_size, bool verbose)
{
	struct image_import_descriptor *descriptor;
	uint32_t offset;

	if (!import_rva || import_size < sizeof(*descriptor)) {
		raw_puts("BRIDGE: no import directory\\n");
		return;
	}

	raw_puts("BRIDGE: resolving imports at "); raw_hex(image_base + import_rva);
	raw_puts(" size="); raw_hex(import_size); raw_puts("\\n");
	for (offset = 0; offset + sizeof(*descriptor) <= import_size;
	     offset += sizeof(*descriptor)) {
		uint64_t *lookup, *iat;
		const char *dll;
		uint32_t thunk_rva;

		descriptor = (void *)(image_base + import_rva + offset);
		if (!descriptor->name && !descriptor->first_thunk)
			break;
		dll = (const char *)(image_base + descriptor->name);
		thunk_rva = descriptor->original_first_thunk ?
			descriptor->original_first_thunk : descriptor->first_thunk;
		lookup = (uint64_t *)(image_base + thunk_rva);
		iat = (uint64_t *)(image_base + descriptor->first_thunk);

		for (; *lookup; lookup++, iat++) {
			uint64_t thunk = *lookup;
			const char *function = NULL;
			uint16_t ordinal = 0;
			bool by_ordinal = thunk & IMAGE_ORDINAL_FLAG64;
			void *shim;

			if (by_ordinal)
				ordinal = thunk & 0xffff;
			else
				function = (const char *)(image_base + (uint32_t)thunk + 2);
			shim = find_shim(dll, function, ordinal, by_ordinal);
			*iat = (uintptr_t)shim;
			raw_puts("BRIDGE: IAT "); raw_puts(dll); raw_puts("!");
			if (by_ordinal) {
				raw_puts("ordinal "); raw_hex(ordinal);
			} else {
				raw_puts(function);
			}
			raw_puts(" -> "); raw_hex((uintptr_t)shim); raw_puts("\\n");
		}
	}
}"""

new_res = """static void resolve_imports(uintptr_t image_base, uint32_t import_rva,
			    uint32_t import_size, bool verbose)
{
	struct image_import_descriptor *descriptor;
	uint32_t offset;
	unsigned int resolved = 0, unimplemented = 0;

	if (!import_rva || import_size < sizeof(*descriptor)) {
		raw_puts("BRIDGE: no import directory\\n");
		return;
	}

	if (verbose) {
		raw_puts("BRIDGE: resolving imports at "); raw_hex(image_base + import_rva);
		raw_puts(" size="); raw_hex(import_size); raw_puts("\\n");
	}
	for (offset = 0; offset + sizeof(*descriptor) <= import_size;
	     offset += sizeof(*descriptor)) {
		uint64_t *lookup, *iat;
		const char *dll;
		uint32_t thunk_rva;

		descriptor = (void *)(image_base + import_rva + offset);
		if (!descriptor->name && !descriptor->first_thunk)
			break;
		dll = (const char *)(image_base + descriptor->name);
		thunk_rva = descriptor->original_first_thunk ?
			descriptor->original_first_thunk : descriptor->first_thunk;
		lookup = (uint64_t *)(image_base + thunk_rva);
		iat = (uint64_t *)(image_base + descriptor->first_thunk);

		for (; *lookup; lookup++, iat++) {
			uint64_t thunk = *lookup;
			const char *function = NULL;
			uint16_t ordinal = 0;
			bool by_ordinal = thunk & IMAGE_ORDINAL_FLAG64;
			void *shim;
			bool is_unimplemented = false;
			unsigned int old_unknown = unknown_count;

			if (by_ordinal)
				ordinal = thunk & 0xffff;
			else
				function = (const char *)(image_base + (uint32_t)thunk + 2);
			shim = find_shim(dll, function, ordinal, by_ordinal);
			*iat = (uintptr_t)shim;

			if (unknown_count > old_unknown) {
				unimplemented++;
				is_unimplemented = true;
			} else {
				resolved++;
			}

			if (verbose) {
				raw_puts("BRIDGE: IAT "); raw_puts(dll); raw_puts("!");
				if (by_ordinal) {
					raw_puts("ordinal "); raw_hex(ordinal);
				} else {
					raw_puts(function);
				}
				raw_puts(" -> "); raw_hex((uintptr_t)shim); raw_puts("\\n");
			} else if (is_unimplemented) {
				raw_puts("[BRIDGE] UNIMPLEMENTED: ");
				raw_puts(dll); raw_puts("!");
				if (by_ordinal) {
					raw_puts("ordinal "); raw_hex(ordinal);
				} else {
					raw_puts(function);
				}
				raw_puts("\\n");
			}
		}
	}

	if (!verbose) {
		raw_puts("[BRIDGE] imports: resolved ");
		raw_uint(resolved);
		raw_puts(", unimplemented ");
		raw_uint(unimplemented);
		raw_puts("\\n");
	}
}"""
if old_res in text:
    with open("win32_bridge.c", "w") as f:
        f.write(text.replace(old_res, new_res))
    print("Success")
else:
    print("Failed to find old string")
