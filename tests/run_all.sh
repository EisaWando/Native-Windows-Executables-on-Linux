#!/usr/bin/env bash
# Build PEs with the handoff flags, then exercise the native binfmt handler.
set -u
set -o pipefail
cd "$(dirname "$(readlink -f "$0")")/.." || exit 1
work=$(mktemp -d /tmp/binfmt-pe-tests.XXXXXX) || exit 1
trap 'rm -rf "$work"' EXIT
bash gui/build.sh || exit 1
flags=(-O1 -fno-builtin -fno-tree-loop-distribute-patterns -nostdlib
       -ffreestanding -fno-stack-protector -Wl,--entry=mainCRTStartup
       -Wl,--subsystem,console)
# The kernel reserves exactly 1 MiB for all bridge PT_LOAD memory, including BSS.
headers=$(readelf -lW win32_bridge.so) || exit 1
bridge_end=$((0x70000000))
bridge_segments=0
while read -r kind offset address physical file_size memory_size rest; do
    [[ $kind == LOAD ]] || continue
    if ((address < 0x70000000 || memory_size > 0x100000 || address > 0x70100000-memory_size)); then
        echo 'FAIL bridge PT_LOAD exceeds the 1 MiB loader window'
        exit 1
    fi
    ((address+memory_size > bridge_end)) && bridge_end=$((address+memory_size))
    bridge_segments=$((bridge_segments+1))
done <<< "$headers"
if ((bridge_segments == 0)); then echo 'FAIL bridge has no PT_LOAD segments'; exit 1; fi
printf 'PASS bridge window (%s / 1048576 bytes)\n' "$((bridge_end-0x70000000))"
for name in hello heap utf exception_diag gdi_objects tls sync paths thread_smoke threads entry_return exit_process_thread thread_fault files calendar registry windows window_demo queues gui_failure crypto colors; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -o "tests/$name.exe" "tests/$name.c" -lkernel32 -lgdi32 -luser32 -lshlwapi -lshell32 -ladvapi32 -lbcrypt -ldwmapi -luxtheme || exit 1
done
x86_64-w64-mingw32-gcc "${flags[@]}" -o "$work/hello.exe" tests/hello42.c -lkernel32 || exit 1
failures=0
check_pe() {
    local label=$1 expected_status=$2 expected_text=$3 status
    shift 3
    "$@" > "$work/stdout" 2> "$work/stderr"
    status=$?
    if [[ $status == 126 ]]; then
        printf 'FAIL %s: exit 126; native PE handler unavailable. Stop and load binfmt_pe.\n' "$label"
        cat "$work/stderr"
        exit 1
    fi
    printf '%s\n' "$expected_text" > "$work/expected"
    tr -d '\r' < "$work/stdout" > "$work/actual"
    if [[ $status == "$expected_status" ]] && cmp -s "$work/expected" "$work/actual"; then
        printf 'PASS %s (exit %s)\n' "$label" "$status"
    else
        printf 'FAIL %s (exit %s, expected %s)\n' "$label" "$status" "$expected_status"
        cat "$work/stdout" "$work/stderr"
        failures=$((failures + 1))
    fi
}
check_pe tests/hello.exe 0 'Hello from PE!' timeout 15s tests/hello.exe
check_pe ./hello.exe 42 'Hello from a native PE on Linux!' bash -c 'cd "$1" && timeout 15s ./hello.exe' bash "$work"
check_pe tests/heap.exe 0 'heap ok' timeout 15s tests/heap.exe
check_pe tests/utf.exe 0 'utf ok' timeout 15s tests/utf.exe
check_pe tests/gdi_objects.exe 0 'gdi ok' timeout 15s tests/gdi_objects.exe
check_pe tests/tls.exe 0 'tls ok: 123 -7 42 0' timeout 15s tests/tls.exe
check_pe tests/sync.exe 0 'sync ok' timeout 15s tests/sync.exe
check_pe tests/queues.exe 0 'queues ok' timeout 15s tests/queues.exe
check_pe tests/paths.exe 0 'paths ok' timeout 15s tests/paths.exe
check_pe tests/files.exe 0 'files ok' timeout 15s tests/files.exe
# Display tests are explicit: the ordinary regression suite stays usable headless.
if [[ ${PE_GUI_TEST:-0} == 1 ]]; then
    check_pe tests/windows.exe 0 'windows ok' timeout 30s tests/windows.exe
    check_pe missing-GUI-helper 0 'gui failure ok' env BINFMT_PE_GUI_HELPER=/nonexistent/pe_gui_host timeout 15s tests/gui_failure.exe
    check_pe unavailable-display 0 'gui failure ok' env DISPLAY= timeout 15s tests/gui_failure.exe
    python3 tests/gui_display.py || failures=$((failures+1))
else
    echo 'SKIP visible windows (run PE_GUI_TEST=1 bash tests/run_all.sh on a display)'
fi
check_pe tests/colors.exe 0 'colors ok' timeout 15s tests/colors.exe
check_pe tests/crypto.exe 0 'crypto ok' timeout 15s tests/crypto.exe
check_pe tests/registry.exe 0 'registry ok' timeout 15s tests/registry.exe
check_pe tests/calendar.exe 0 'calendar ok' timeout 15s tests/calendar.exe
check_pe tests/thread_smoke.exe 0 'thread smoke ok' timeout 15s tests/thread_smoke.exe
check_pe tests/entry_return.exe 37 'entry return ok' timeout 15s tests/entry_return.exe
check_pe tests/exit_process_thread.exe 23 'exit group ok' timeout 15s tests/exit_process_thread.exe
for run in {1..20}; do
    check_pe "tests/threads.exe ($run/20)" 0 'threads ok' timeout 30s tests/threads.exe
done
timeout 15s tests/thread_fault.exe > "$work/fault.out" 2> "$work/fault.err"
status=$?
fault_tid=$(awk '/^fault tid [0-9]+$/ {print $3}' "$work/fault.out")
if [[ $status == 139 && -n $fault_tid ]] &&
    grep -q "^BRIDGE: crash tid=$fault_tid$" "$work/fault.err" &&
    grep -q 'BRIDGE: SIGSEGV signal=0x000000000000000b' "$work/fault.err"; then
    echo 'PASS thread crash dumper (guard-page RSP, private alt stack, correct TID, exit 139)'
else
    echo "FAIL thread crash dumper (exit $status)"
    cat "$work/fault.out" "$work/fault.err"
    failures=$((failures+1))
fi
timeout 15s tests/exception_diag.exe > "$work/diag.out" 2> "$work/diag.err"
status=$?
if [[ $status == 127 ]] &&
    grep -q 'type=".?AVdiagnostic_test@@"' "$work/diag.err" &&
    grep -q 'bt=0x0000000140' "$work/diag.err" &&
    grep -q 'bytes=00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f' "$work/diag.err"; then
    echo 'PASS exception diagnostics (intentional exit 127, synthetic metadata)'
else
    echo "FAIL exception diagnostics (exit $status)"
    cat "$work/diag.err"
    failures=$((failures + 1))
fi
timeout 30s ./npp.8.9.8.portable.x64/notepad++.exe > /tmp/out.txt 2> /tmp/err.txt
status=$?
last=$(grep -aE '\[BRIDGE\]|BRIDGE: (SIG|RIP=)' /tmp/err.txt | tail -n 1)
printf 'INFO Notepad++: exit %s, last stop: %s\n' "$status" "${last:-<none>}"
exit "$((failures != 0))"
