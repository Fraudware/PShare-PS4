#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/ps4-host
${CXX:-g++} -std=c++20 -O1 -g -fsanitize=address,undefined -Ips4 -Isrc \
 ps4/tests/body_tests.cpp src/pshare/core/media.cpp src/pshare/core/config.cpp \
 src/pshare/core/ini.cpp src/pshare/core/text.cpp -o build/ps4-host/body_tests
# Some container runtimes cannot provide LeakSanitizer's /proc task inventory.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} build/ps4-host/body_tests
g++ -std=c++20 -fsanitize=address,undefined -Ips4 ps4/tests/stat_tests.cpp -o build/ps4-host/stat_tests && ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} build/ps4-host/stat_tests
