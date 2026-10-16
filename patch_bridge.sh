#!/bin/bash
patch -p1 << 'PATCH'
--- a/win32_bridge.c
+++ b/win32_bridge.c
@@ -103,6 +103,21 @@
 	raw_write(string, cstring_length(string));
 }
 
+static void raw_uint(unsigned int value)
+{
+	char buf[16];
+	int pos = 15;
+	buf[15] = '\0';
+	if (value == 0)
+		buf[--pos] = '0';
+	while (value) {
+		buf[--pos] = '0' + (value % 10);
+		value /= 10;
+	}
+	raw_write(&buf[pos], 15 - pos);
+}
+
+
 static void raw_hex(uint64_t value)
 {
 	char text[18] = "0x0000000000000000";
@@ -131,6 +146,14 @@
 	return *left == *right;
 }
 
+static bool string_equals(const char *left, const char *right)
+{
+	while (*left && *right && *left == *right) {
+		left++; right++;
+	}
+	return *left == *right;
+}
+
 static void dump_maps(void)
 {
 	char buffer[1024];
@@ -305,17 +328,18 @@
 }
 
 static void resolve_imports(uintptr_t image_base, uint32_t import_rva,
-			    uint32_t import_size)
+			    uint32_t import_size, bool verbose)
 {
 	struct image_import_descriptor *descriptor;
 	uint32_t offset;
+	unsigned int resolved = 0, unimplemented = 0;
 
 	if (!import_rva || import_size < sizeof(*descriptor)) {
 		raw_puts("BRIDGE: no import directory\n");
 		return;
 	}
 
-	raw_puts("BRIDGE: resolving imports at "); raw_hex(image_base + import_rva);
-	raw_puts(" size="); raw_hex(import_size); raw_puts("\n");
+	if (verbose) {
+		raw_puts("BRIDGE: resolving imports at "); raw_hex(image_base + import_rva);
+		raw_puts(" size="); raw_hex(import_size); raw_puts("\n");
+	}
 	for (offset = 0; offset + sizeof(*descriptor) <= import_size;
 	     offset += sizeof(*descriptor)) {
 		uint64_t *lookup, *iat;
@@ -336,29 +360,65 @@
 			uint16_t ordinal = 0;
 			bool by_ordinal = thunk & IMAGE_ORDINAL_FLAG64;
 			void *shim;
+			bool is_unimplemented = false;
+			unsigned int old_unknown = unknown_count;
 
 			if (by_ordinal)
 				ordinal = thunk & 0xffff;
 			else
 				function = (const char *)(image_base + (uint32_t)thunk + 2);
 			shim = find_shim(dll, function, ordinal, by_ordinal);
 			*iat = (uintptr_t)shim;
-			raw_puts("BRIDGE: IAT "); raw_puts(dll); raw_puts("!");
-			if (by_ordinal) {
-				raw_puts("ordinal "); raw_hex(ordinal);
+
+			if (unknown_count > old_unknown) {
+				unimplemented++;
+				is_unimplemented = true;
 			} else {
-				raw_puts(function);
+				resolved++;
 			}
-			raw_puts(" -> "); raw_hex((uintptr_t)shim); raw_puts("\n");
+
+			if (verbose) {
+				raw_puts("BRIDGE: IAT "); raw_puts(dll); raw_puts("!");
+				if (by_ordinal) {
+					raw_puts("ordinal "); raw_hex(ordinal);
+				} else {
+					raw_puts(function);
+				}
+				raw_puts(" -> "); raw_hex((uintptr_t)shim); raw_puts("\n");
+			} else if (is_unimplemented) {
+				raw_puts("[BRIDGE] UNIMPLEMENTED: ");
+				raw_puts(dll); raw_puts("!");
+				if (by_ordinal) {
+					raw_puts("ordinal "); raw_hex(ordinal);
+				} else {
+					raw_puts(function);
+				}
+				raw_puts("\n");
+			}
 		}
 	}
+
+	if (!verbose) {
+		raw_puts("[BRIDGE] imports: resolved ");
+		raw_uint(resolved);
+		raw_puts(", unimplemented ");
+		raw_uint(unimplemented);
+		raw_puts("\n");
+	}
 }
 
 __attribute__((used, visibility("hidden")))
 void bridge_bootstrap(uintptr_t image_base, uint32_t import_rva,
-		      uint32_t import_size)
+		      uint32_t import_size, uintptr_t *sp)
 {
+	bool verbose = false;
+	long argc = sp[0];
+	uintptr_t *envp = sp + 1 + argc + 1;
+	while (*envp) {
+		if (string_equals((const char *)*envp, "BRIDGE_VERBOSE=1"))
+			verbose = true;
+		envp++;
+	}
+
 	install_crash_handler();
-	resolve_imports(image_base, import_rva, import_size);
+	resolve_imports(image_base, import_rva, import_size, verbose);
 }
 
 /* RDI=PE entry, RSI=PEB, RDX=image base, RCX=import RVA, R8=import size. */
@@ -370,6 +430,7 @@
 		"mov %rdx, %rdi\n\t"
 		"mov %rcx, %rsi\n\t"
 		"mov %r8, %rdx\n\t"
+		"mov %rsp, %rcx\n\t"
 		"call bridge_bootstrap\n\t"
 		"jmp *%r12\n\t");
 }
PATCH
