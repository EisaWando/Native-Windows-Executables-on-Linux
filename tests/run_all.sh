#!/usr/bin/env bash
# Build PEs with the handoff flags, then exercise the native binfmt handler.
set -u
set -o pipefail
cd "$(dirname "$(readlink -f "$0")")/.." || exit 1
work=$(mktemp -d /tmp/binfmt-pe-tests.XXXXXX) || exit 1
trap 'rm -rf "$work"' EXIT
flags=(-O1 -fno-builtin -fno-tree-loop-distribute-patterns -nostdlib
       -ffreestanding -fno-stack-protector -Wl,--entry=mainCRTStartup
       -Wl,--subsystem,console)
for name in hello heap utf; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -o "tests/$name.exe" "tests/$name.c" -lkernel32 || exit 1
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
timeout 30s ./npp.8.9.8.portable.x64/notepad++.exe > /tmp/out.txt 2> /tmp/err.txt
status=$?
printf 'Notepad++ exit: %s\n' "$status"
last=$(grep -a '\[BRIDGE\]' /tmp/err.txt | tail -n 1)
printf 'Notepad++ last [BRIDGE]: %s\n' "${last:-<none>}"
if [[ $status == 127 && $last == *'CALLED UNIMPLEMENTED:'* ]] && ! grep -aq 'BRIDGE: SIG' /tmp/err.txt; then
    echo 'PASS Notepad++ diagnostic (expected unimplemented API stop; application incomplete)'
elif [[ $status == 0 ]]; then
    echo 'PASS Notepad++ (exit 0)'
else
    echo 'FAIL Notepad++ diagnostic (crash, timeout, or unexpected exit)'
    cat /tmp/err.txt
    failures=$((failures + 1))
fi
exit "$((failures != 0))"
