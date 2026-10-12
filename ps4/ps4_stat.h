// PShare PS4: struct stat layout fix. SPDX-License-Identifier: GPL-3.0-or-later
//
// The PS4 kernel (FreeBSD 9 based) fills a 120-byte struct stat:
//   dev u32 @0, ino u32 @4, mode u16 @8, nlink u16 @10, uid @12, gid @16,
//   rdev @20, atim @24, mtim @40, ctim @56, size i64 @72, blocks i64 @80,
//   blksize @88, flags @92, gen @96, lspare @100, birthtim @104.
// OpenOrbis 0.5.4's <sys/stat.h> declares a different (newer FreeBSD) layout,
// so st_size read the kernel's st_blocks and st_mtim was misaligned. Found on
// a real PS4 0.2 alpha run: captures reported 960-2112 "bytes" (512-byte
// blocks) and every HTTP upload failed with "Capture changed or read failed".
//
// In the PS4 build this header is force-included into every C++ file, and
// stat/fstat/lstat are routed through wrappers that translate the layout.
#pragma once
#include <sys/stat.h>
#include <cstdint>
#include <cstring>

namespace pshare::ps4 {
constexpr std::size_t kKernelStatSize = 120;
// Translate a PS4 kernel stat buffer into the toolchain's struct stat.
inline void translate_kernel_stat(const unsigned char *raw, struct stat *out) {
    auto u16 = [raw](std::size_t at) { std::uint16_t v; std::memcpy(&v, raw + at, 2); return v; };
    auto u32 = [raw](std::size_t at) { std::uint32_t v; std::memcpy(&v, raw + at, 4); return v; };
    auto i64 = [raw](std::size_t at) { std::int64_t v; std::memcpy(&v, raw + at, 8); return v; };
    struct stat converted;
    std::memset(&converted, 0, sizeof converted);
    converted.st_dev = u32(0);
    converted.st_ino = u32(4);
    converted.st_mode = u16(8);
    converted.st_nlink = u16(10);
    converted.st_uid = u32(12);
    converted.st_gid = u32(16);
    converted.st_rdev = u32(20);
    converted.st_atim.tv_sec = i64(24); converted.st_atim.tv_nsec = i64(32);
    converted.st_mtim.tv_sec = i64(40); converted.st_mtim.tv_nsec = i64(48);
    converted.st_ctim.tv_sec = i64(56); converted.st_ctim.tv_nsec = i64(64);
    converted.st_size = i64(72);
    converted.st_blocks = i64(80);
    converted.st_blksize = u32(88);
    *out = converted;
}
}

#if defined(PSHARE_PS4) && !defined(PSHARE_PS4_STAT_IMPL)
extern "C" {
int pshare_ps4_stat(const char *path, struct stat *out);
int pshare_ps4_lstat(const char *path, struct stat *out);
int pshare_ps4_fstat(int fd, struct stat *out);
}
#define stat(path, out) pshare_ps4_stat(path, out)
#define lstat(path, out) pshare_ps4_lstat(path, out)
#define fstat(fd, out) pshare_ps4_fstat(fd, out)
#endif
