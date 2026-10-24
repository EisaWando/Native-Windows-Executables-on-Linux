#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
# Generate only a C string literal, avoiding shell interpolation into source.
python3 - <<'PY'
import json
from pathlib import Path
Path('gui/host_path.h').write_text('#define PE_GUI_HOST_PATH '+json.dumps(str(Path('pe_gui_host').resolve()))+'\n')
PY
cc -O2 -Wall -Wextra -Werror $(pkg-config --cflags x11 freetype2 fontconfig) -o pe_gui_host gui/host.c $(pkg-config --libs x11 freetype2 fontconfig)
gcc -fno-pic -no-pie -fno-stack-protector -static -nostdlib -ffreestanding -Wl,-Ttext-segment=0x70000000 -Wl,-e,_start -o win32_bridge.so win32_bridge.c
