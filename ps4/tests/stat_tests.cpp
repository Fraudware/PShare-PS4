// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the PS4 kernel stat translation against a hand-built kernel buffer.
#include "ps4_stat.h"
#include <cassert>
#include <cstdio>
template <typename T> void put(unsigned char *raw, std::size_t at, T value) { std::memcpy(raw + at, &value, sizeof value); }
int main() {
    unsigned char raw[pshare::ps4::kKernelStatSize] = {};
    put<std::uint32_t>(raw, 0, 77); put<std::uint32_t>(raw, 4, 12345);
    put<std::uint16_t>(raw, 8, 0100644); put<std::uint16_t>(raw, 10, 1);
    put<std::uint32_t>(raw, 12, 1); put<std::uint32_t>(raw, 16, 1);
    put<std::int64_t>(raw, 40, 1760280000); put<std::int64_t>(raw, 48, 5);
    put<std::int64_t>(raw, 72, 524288);   // real size
    put<std::int64_t>(raw, 80, 1024);     // 512-byte blocks: what 0.2 showed as the size
    put<std::uint32_t>(raw, 88, 32768);
    struct stat out{};
    pshare::ps4::translate_kernel_stat(raw, &out);
    assert(out.st_size == 524288 && out.st_blocks == 1024 && out.st_blksize == 32768);
    assert(S_ISREG(out.st_mode) && !S_ISDIR(out.st_mode) && (out.st_mode & 0777) == 0644);
    assert(out.st_mtim.tv_sec == 1760280000 && out.st_ino == 12345 && out.st_uid == 1);
    put<std::uint16_t>(raw, 8, 040755);
    pshare::ps4::translate_kernel_stat(raw, &out);
    assert(S_ISDIR(out.st_mode));
    std::puts("PS4 stat layout translation tests passed");
}
