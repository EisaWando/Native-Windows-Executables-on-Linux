# Notepad++ 8.9.8 x64 COMCTL32 ordinal audit

Complete ordinal-only import list from `objdump -p npp.8.9.8.portable.x64/notepad++.exe`.
Names below are established by the calling ABI / installed commctrl.h signatures,
not by an export-name table in this PE. The current public MinGW comctl32.def has
names but no ordinal numbers; it cannot independently prove ordinal mappings.

| Ordinal | IAT VA | Name | Evidence / binding |
|---|---|---|---|
| 0x11 (17) | 0x14044d108 | InitCommonControls | ABI-inferred: call 0x1400c4eb8 consumes no arguments/result, immediately before ImageList_Create. Bound to existing InitCommonControls -> InitCommonControlsEx({8, ICC_WIN95_CLASSES=0xff}). |
| 0x159 (345) | 0x14044d170 | TaskDialogIndirect | ABI-inferred: config initialized with cbSize=0xa0 at 0x1400210fe, copied to stack; config pointer RCX, button-out RDX, R8/R9 NULL at call 0x140021560. HRESULT tested, returned button read at 0x14002158d. Intentionally remains unimplemented (dialog task). |
| 0x17d (381) | 0x14044d150 | LoadIconWithScaleDown | Decisive five-argument HRESULT/out-HICON ABI below. Corrected from the earlier incorrect ImageList_LoadImageW inference. |
| 0x19a (410) | 0x14044d180 | SetWindowSubclass | ABI-inferred: callback address RDX, subclass ID R8, reference data R9 at 0x140013957-0x14001396a. Existing binding verified by ordinal regression. |
| 0x19b (411) | 0x14044d160 | GetWindowSubclass | ABI-inferred: callback address/ID and output reference pointer (or NULL) at 0x14001aea6-0x14001aeb9; BOOL result controls installation. Existing binding verified by ordinal regression. |
| 0x19c (412) | 0x14044d178 | RemoveWindowSubclass | ABI-inferred: HWND/callback/ID only at 0x140013bc2-0x140013bce, during destruction. Existing binding verified by ordinal regression. |
| 0x19d (413) | 0x14044d190 | DefSubclassProc | ABI-inferred: HWND/message/WPARAM/LPARAM forwarding at 0x140013bd4-0x140013bdf; WM_GETDLGCODE result ORed with 4 at 0x140013bb1-0x140013bbc. Existing binding verified by ordinal regression. |

## Why 0x17d is not ImageList_LoadImageW

Commands: `objdump -d --start-address=0x1401ebb90 --stop-address=0x1401ebc40 npp.8.9.8.portable.x64/notepad++.exe`;
`objdump -p npp.8.9.8.portable.x64/notepad++.exe`.
Relevant raw disassembly (omitting opcode bytes only):

```
1401ebbd0: mov %r13,-0x28(%rbp)          # initialize output HICON
1401ebbdc: lea -0x28(%rbp),%rax
1401ebbe0: mov %rax,0x20(%rsp)          # 5th argument is HICON*, not COLORREF
1401ebbe5: mov %edi,%r9d                # cy
1401ebbe8: mov %edi,%r8d                # cx
1401ebbeb: mov %r14d,%edx               # resource ID
1401ebbee: mov %r15,%rcx                # HINSTANCE
1401ebbf1: call *0x261559(%rip)          # IAT 0x14044d150, ordinal 381
1401ebbf7: test %eax,%eax
1401ebbf9: je 0x1401ebc1f               # S_OK is zero
1401ebc13: call *0x26233f(%rip)          # LoadImageW fallback on nonzero HRESULT
1401ebc19: mov %rax,-0x28(%rbp)          # fallback stores the HICON
1401ebc1f: mov -0x28(%rbp),%rax          # successful call reads output HICON
1401ebc23: mov %rax,%r8
1401ebc26: mov $0xffffffff,%edx
1401ebc32: call *0x261508(%rip)          # named ImageList_ReplaceIcon
```

The old seven-argument HIMAGELIST test was a self-confirming test of the wrong
signature. It has been removed. tests/commctl_ordinals.c imports 381 with the
five-argument HRESULT/HICON* signature and checks scaled pixels/alpha, NULL output
on failure, and named LoadIconWithScaleDown. ImageList_LoadImageW remains named.
The same test imports 17 and 410-413 and exercises real hidden-control/subclass
behavior, including the output reference data and forwarding result.
