# AGENTS.md — read this first (rules for any AI coding agent)

Project: **binfmt_pe** — run Windows x64 `.exe` files natively on Linux with no Wine.
- `fs/binfmt_pe.c` = kernel module (loads PE files, hands off to the bridge). Do NOT edit it unless a task says so.
- `win32_bridge.c` + `bridge/*.inc` + `gui/*.inc` = the userland Windows runtime (this is where almost all work happens).
- `gui/host*.c` = native X11 helper process (Xlib + FreeType).
- `tests/*.c` = small Windows test programs built with mingw-w64 and run by `tests/run_all.sh`.
- Longer docs: `HANDOFF_v3.md`, `docs/GUI_DESIGN.md`, `docs/SESSION_STATE.md` (latest state), `docs/NEXT_TASKS.md` (your task list).

## WARNING: this repository contains the whole Linux kernel source tree
NEVER run `ls -R`, `find .`, `tree`, `rg` or `grep -r` on the repository root. They print tens of thousands of lines and will flood your context.
Only look inside: the repo root files, `bridge/`, `gui/`, `tests/`, `docs/`, and `fs/binfmt_pe.c`.
Always limit output: `| head -n 40`, `| tail -n 40`, `sed -n '100,160p' FILE`, `rg -n 'pattern' win32_bridge.c bridge gui tests`.
`win32_bridge.c` is very large: never `cat` it; read it in windows of at most 120 lines.

## Absolute rules
1. Do ONE small task at a time, then stop and report. Do not start the next task by yourself unless the task list says to continue.
2. Never run `sudo`, `insmod`, `rmmod` or `run2.sh` (they need a password). The kernel module must already be loaded. If any test program exits with **126** ("cannot execute binary file"), STOP and tell the owner: "binfmt_pe is not loaded".
3. Never invent output. Only report commands you actually ran and what they printed.
4. Do not rewrite, reformat or "clean up" existing code. Add new code in new `.inc` files where possible and register shims the same way existing ones are registered (`lookup_shim()` in `win32_bridge.c`, using `ascii_equal_ci`).
5. Keep the bridge freestanding: no libc. Use raw syscalls and the helpers that already exist.
6. Commit locally after every green test run with a clear message. Do NOT `git push`.
7. After each commit update `docs/SESSION_STATE.md` (current commit, last green test run, Notepad++ stop and import counts, next 3 steps).
8. If you are unsure, ask the owner. A wrong guess costs more than a question.
9. EVIDENCE RULE: every claim in a report must be backed by raw output pasted from a command you ran in this session: the PASS/FAIL lines of `bash tests/run_all.sh`, the first lines of `/tmp/err.txt` after running Notepad++, and the line printed by `git log --oneline -1` after each commit. Never write a commit hash, test result or import count that you did not just see printed. If a command failed or you did not run it, say so plainly.
10. YOU run and verify everything yourself (build, tests, Notepad++). The owner is not your test runner. A change is "done" only when the Notepad++ stop or the import count visibly changed in YOUR run, or the new test passed in YOUR run.
11. DO NOT RE-IMPLEMENT. Before adding any Windows function, check it does not already exist: `rg -n 'FunctionName' win32_bridge.c bridge gui | head`. Duplicates break the build or silently replace real code.
12. NO FAKE STUBS. Do not register a function that just returns 0 or a made-up handle to get past a stop. Implement real, documented behavior, or stop and report that the function needs a larger feature.
13. Ordinal imports (`dll!ordinal 0x...`): find the real function name from the public mingw-w64 `.def` files or Microsoft documentation, state which function it is, and match its real signature before implementing.

## Commands
Build the bridge:
```
gcc -fno-pic -no-pie -fno-stack-protector -static -nostdlib -ffreestanding -Wl,-Ttext-segment=0x70000000 -Wl,-e,_start -o win32_bridge.so win32_bridge.c
```
Run all tests (builds test programs with mingw, runs them, then runs Notepad++ informationally):
```
bash tests/run_all.sh > /tmp/run.log 2>&1; tail -n 15 /tmp/run.log; grep FAIL /tmp/run.log
```
Display (X11) tests, only if a graphical session is available:
```
PE_GUI_TEST=1 bash tests/run_all.sh > /tmp/run.log 2>&1
```
Run Notepad++ and see where it stops:
```
./npp.8.9.8.portable.x64/notepad++.exe > /tmp/out.txt 2> /tmp/err.txt; echo $status; head -n 3 /tmp/err.txt; tail -n 2 /tmp/err.txt
```
(The owner uses the fish shell: use `$status`, not `$?`, when you give commands to the owner. In `bash -c` scripts use `$?`.)

Reading a stop: `CALLED UNIMPLEMENTED: DLL!Function ret=... rcx=... rdx=... r8=... r9=... bt=...` means Notepad++ called a Windows function that is not implemented yet. rcx, rdx, r8, r9 are its first four arguments (Windows x64 calling convention). `imports: resolved N, unimplemented M` shows overall progress.

## Shim checklist (every Windows function you add)
1. Get the exact signature: `rg -n 'FunctionName' /usr/x86_64-w64-mingw32/include | head`.
2. Read the Microsoft documentation for the real behavior: return values, last-error codes, size-query behavior (NULL buffer), A and W variants.
3. Write it with `__attribute__((ms_abi))`. Types: HANDLE and pointers 64-bit, DWORD/BOOL/LONG 32-bit, WCHAR 16-bit (`uint16_t`, never `wchar_t`).
4. On failure set the last error (existing helper) and return 0/NULL. Registry functions return their error code directly instead.
5. Add or extend a test program in `tests/` that checks success, failure and edge cases (NULL pointers, short buffers). Add it to `tests/run_all.sh`.
6. Register it in `lookup_shim()`.
7. Never return fake success for something you did not really implement. If a feature is only partly done, say so in a comment and in `docs/SESSION_STATE.md`.

## When to STOP and report to the owner
- A new kind of crash or a hang.
- Notepad++ asks to display its visible main window (give the owner the exact command to run it on the desktop and take a screenshot).
- The task needs a kernel change, dialog templates, or x64 exception unwinding (these go to a stronger model; mark the task in `docs/NEXT_TASKS.md`).
- Any test that passed before now fails and you cannot fix it in one attempt (revert your last change with `git checkout -- <file>` and report).

## Report format (keep it short)
1. What you changed (files, functions). 2. Commands you ran and their real output (trimmed). 3. Test result (PASS/FAIL list). 4. New Notepad++ stop and the "resolved/unimplemented" counts. 5. Commit hash. 6. Anything the owner must do.
