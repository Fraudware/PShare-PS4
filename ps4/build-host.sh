#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/ps4-host
cxx=${CXX:-g++}
cc=${CC:-gcc}
sdl_include=${SDL_INCLUDE:-/usr/include/SDL2}
"$cc" -O2 -Isrc -c src/third_party/stb_image/stb_image.c -o build/ps4-host/stb.o
"$cc" -O2 -Isrc -c src/third_party/qrcodegen/qrcodegen.c -o build/ps4-host/qr.o
"$cxx" -std=c++20 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -pthread -DPSHARE_EMBEDDED=1 -DPSHARE_BUILD=ps4host01 \
 -I"$sdl_include" -Isrc -Ips4 ps4/main.cpp ps4/ui_kit.cpp ps4/thumbs.cpp daemon/main.cpp src/pshare/core/*.cpp \
 build/ps4-host/stb.o build/ps4-host/qr.o -lcurl -Wl,-l:libSDL2-2.0.so.0 -o build/ps4-host/pshare
