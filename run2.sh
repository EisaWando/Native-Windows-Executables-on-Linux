#!/bin/bash
# Run with:  bash run2.sh      (works from fish too)
# Place in /home/eisa/Desktop/linux-7.1.6/
cd "$(dirname "$(readlink -f "$0")")" || exit 1

echo "== build bridge =="
gcc -fno-pic -no-pie -fno-stack-protector -static -nostdlib -ffreestanding \
    -Wl,-Ttext-segment=0x70000000 -Wl,-e,_start \
    -o win32_bridge.so win32_bridge.c
if [ $? -ne 0 ]; then echo "BRIDGE BUILD FAILED"; exit 1; fi
readelf -h win32_bridge.so | grep -E "Type|Entry"

echo "== build module =="
make LLVM=1 -C /lib/modules/$(uname -r)/build M=$PWD/fs modules
if [ $? -ne 0 ]; then echo "MODULE BUILD FAILED"; exit 1; fi

echo "== reload module =="
if lsmod | grep -q '^binfmt_pe'; then
    sudo rmmod binfmt_pe || { echo "RMMOD FAILED"; exit 1; }
fi
sudo insmod fs/binfmt_pe.ko || { echo "INSMOD FAILED"; sudo dmesg | tail -n 20; exit 1; }

echo "== run =="
./npp.8.9.8.portable.x64/notepad++.exe > /tmp/out.txt 2> /tmp/err.txt
echo "exit code: $?"
wc -c /tmp/out.txt /tmp/err.txt
echo "--- stderr (first 25 lines) ---"
head -n 25 /tmp/err.txt
echo "--- kernel log ---"
sudo dmesg | grep -E "BINFMT_PE|segfault|Oops|BUG|Call Trace" | tail -n 30
