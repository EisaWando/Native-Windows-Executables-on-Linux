# x64 Exception Handling Design

Reference: Microsoft x64 ABI / SEH documentation (RtlDispatchException, RtlVirtualUnwind, RtlUnwindEx, RtlLookupFunctionEntry, RtlCaptureContext, __C_specific_handler, __CxxFrameHandler4).

## Goal
Implement full x64 SEH / C++ exception handling so Notepad++ can propagate C++ exceptions (code 0xe06d7363) through the bridge instead of crashing at `RaiseException`.

## Data Structures (per Microsoft x64 ABI)

### UNWIND_INFO
```
struct UNWIND_INFO {
    UBYTE Version : 3;
    UBYTE Flags : 5;
    UBYTE SizeOfProlog;
    UBYTE CountOfCodes;
    UBYTE FrameRegister : 4;
    UBYTE FrameOffset : 4;
    UNWIND_CODE UnwindCode[CountOfCodes];
    // optional: UNWIND_CODE[], then:
    union {
        ULONG ExceptionHandler;
        ULONG FunctionEntry;
    };
    // optional: ExceptionData[]
};
```
Flags:
- UNW_FLAG_EHANDLER (0x1) - has exception handler
- UNW_FLAG_UHANDLER (0x2) - has unwind handler
- UNW_FLAG_CHAININFO (0x4) - chained unwind info

### RUNTIME_FUNCTION (IMAGE_RUNTIME_FUNCTION_ENTRY)
```
struct RUNTIME_FUNCTION {
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG UnwindInfoAddress; // RVA to UNWIND_INFO
};
```

### EXCEPTION_RECORD
```
struct EXCEPTION_RECORD {
    ULONG ExceptionCode;
    ULONG ExceptionFlags;
    struct EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    ULONG NumberParameters;
    ULONG_PTR ExceptionInformation[15];
};
```

### CONTEXT (x64)
Full register set: Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8-R15, Rip, EFlags, SegCs, SegDs, SegEs, SegFs, SegGs, SegSs, MxCsr, FloatSave, SegGsBase, Debug registers, etc.

### DISPATCHER_CONTEXT
```
struct DISPATCHER_CONTEXT {
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PUNWIND_HISTORY_TABLE HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
};
```

## Key Functions

### RtlCaptureContext
```c
void RtlCaptureContext(PCONTEXT ContextRecord);
```
Captures current register state into CONTEXT.

### RtlLookupFunctionEntry
```c
PRUNTIME_FUNCTION RtlLookupFunctionEntry(
    ULONG64 ControlPc,
    PULONG64 ImageBase,
    PUNWIND_HISTORY_TABLE HistoryTable
);
```
Finds RUNTIME_FUNCTION for given PC. Used by unwinder.

### RtlVirtualUnwind
```c
ULONG64 RtlVirtualUnwind(
    ULONG HandlerType,
    ULONG64 ImageBase,
    ULONG64 ControlPc,
    PRUNTIME_FUNCTION FunctionEntry,
    PCONTEXT ContextRecord,
    PVOID *HandlerData,
    PULONG64 EstablisherFrame,
    PKNONVOLATILE_CONTEXT_POINTERS ContextPointers
);
```
Unwinds one frame. Updates ContextRecord to caller's context. Returns next RIP. If HandlerType == 1, searches for exception handler.

### RtlDispatchException
```c
BOOLEAN RtlDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
);
```
Walks stack frames calling RtlVirtualUnwind with HandlerType=1. Calls language-specific handler (__C_specific_handler) if UNW_FLAG_EHANDLER set. If handler returns ExceptionContinueSearch, continues. If ExceptionContinueExecution, resumes at TargetIp.

### RtlUnwindEx
```c
VOID RtlUnwindEx(
    PVOID TargetFrame,
    PVOID TargetIp,
    PEXCEPTION_RECORD ExceptionRecord,
    PVOID ReturnValue,
    PCONTEXT ContextRecord,
    PUNWIND_HISTORY_TABLE HistoryTable
);
```
Forces unwind to TargetFrame. Used by C++ `throw`, `longjmp`, `ExitThread`.

### Language-Specific Handlers

#### __C_specific_handler (C SEH)
```c
EXCEPTION_DISPOSITION __C_specific_handler(
    PEXCEPTION_RECORD ExceptionRecord,
    PVOID EstablisherFrame,
    PCONTEXT ContextRecord,
    PDISPATCHER_CONTEXT DispatcherContext
);
```
Walks scope table in UNWIND_INFO. Calls filter expressions. If filter returns EXCEPTION_EXECUTE_HANDLER, transfers to handler via RtlUnwindEx.

#### __CxxFrameHandler4 (C++ EH)
```c
EXCEPTION_DISPOSITION __CxxFrameHandler4(
    PEXCEPTION_RECORD ExceptionRecord,
    PVOID EstablisherFrame,
    PCONTEXT ContextRecord,
    PDISPATCHER_CONTEXT DispatcherContext
);
```
Handles C++ exceptions (0xe06d7363). Matches catch blocks by type_info. Calls destructors during unwind. Uses type_info comparison via `__RTTICompleteObjectLocator` and `__RTTITypeDescriptor`.

### Hardware Faults to Exceptions
Windows maps hardware faults to exceptions:
- STATUS_ACCESS_VIOLATION (0xC0000005) -> EXCEPTION_ACCESS_VIOLATION
- STATUS_STACK_OVERFLOW (0xC00000FD) -> EXCEPTION_STACK_OVERFLOW
- STATUS_INTEGER_DIVIDE_BY_ZERO (0xC0000094) -> EXCEPTION_INT_DIVIDE_BY_ZERO
- STATUS_BREAKPOINT (0x80000003) -> EXCEPTION_BREAKPOINT

Kernel calls KiDispatchException -> RtlDispatchException.

## Bridge Implementation Plan

### 1. UNWIND_INFO Generation (per-function)
- Parse PE .pdata/.xdata sections
- Build RUNTIME_FUNCTION array
- Implement RtlLookupFunctionEntry

### 2. RtlVirtualUnwind
- Decode UNWIND_CODE array
- Restore non-volatile registers
- Update RSP, RIP
- Return next RIP
- If HandlerType=1, return HandlerData for language handler

### 3. RtlDispatchException
- Loop: RtlVirtualUnwind(HandlerType=1) until frame=0 or handler returns ExceptionContinueExecution
- Call language handler (__C_specific_handler / __CxxFrameHandler4)
- Handle ExceptionContinueSearch / ExceptionContinueExecution

### 4. RtlUnwindEx
- Force unwind to TargetFrame
- Call language handlers with ExceptionFlags=EXCEPTION_UNWINDING

### 5. Language Handlers
- Implement __C_specific_handler (scope table walk)
- Implement __CxxFrameHandler4 (type_info matching, destructor calls)

### 6. RaiseException
- Build EXCEPTION_RECORD
- Call RtlDispatchException
- If unhandled, call unhandled exception filter

## Test Plan (MinGW-built PEs)

| Test | Description |
|------|-------------|
| test_seh_basic.exe | `__try/__except` with filter returning EXCEPTION_EXECUTE_HANDLER |
| test_seh_finally.exe | `__try/__finally` with cleanup |
| test_cpp_throw.exe | `throw std::runtime_error` caught by `catch` |
| test_cpp_throw_dtor.exe | Throw through stack frames with local objects; verify destructors run |
| test_rethrow.exe | `catch(...)` then `throw;` |
| test_unwind_ex.exe | `RtlUnwindEx` forced unwind |
| test_nested_try.exe | Nested `__try` blocks |
| test_filter_continue.exe | Filter returning EXCEPTION_CONTINUE_SEARCH |
| test_seh_cpp_interop.exe | SEH `__try` calling C++ function that throws |
| test_bad_alloc.exe | Simulate Notepad++ std::bad_alloc (0xe06d7363) |

## Slices (one function per commit)

1. **RtlLookupFunctionEntry** - parse .pdata, binary search RUNTIME_FUNCTION
2. **RtlVirtualUnwind (unwind only)** - decode UNWIND_CODE, restore regs, no handler
3. **RtlVirtualUnwind (handler search)** - return HandlerData when UNW_FLAG_EHANDLER
3. **RtlDispatchException** - frame walk calling RtlVirtualUnwind(1)
4. **__C_specific_handler** - scope table walk, filter/handler dispatch
5. **RtlUnwindEx** - forced unwind with EXCEPTION_UNWINDING
6. **__CxxFrameHandler4** - type_info matching, catch dispatch
7. **RaiseException** - build EXCEPTION_RECORD, call RtlDispatchException
8. **Integration test** - Notepad++ std::bad_alloc propagates past bridge

## Data Sources in Bridge
- PE .pdata section -> RUNTIME_FUNCTION array
- PE .xdata section -> UNWIND_INFO blobs
- PE .rdata -> type_info, catchable type arrays
- Bridge needs to map PE RVAs to host addresses for handler calls

## Constraints
- No libc: implement unwind logic in freestanding C
- Kernel-mediated memory reads for diagnostic (already have diagnostic_read)
- Shim calls must use ms_abi
- Handler calls are indirect through function pointers in UNWIND_INFO