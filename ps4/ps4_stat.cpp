// PShare PS4: stat wrappers for the kernel's struct layout (see ps4_stat.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#define PSHARE_PS4_STAT_IMPL 1
#include "ps4_stat.h"
// The build force-includes ps4_stat.h before this file, so its redirecting
// macros are already defined here; drop them so these wrappers reach libc.
#undef stat
#undef lstat
#undef fstat
namespace {
// Big enough for either layout; the kernel writes 120 bytes.
struct alignas(16) RawStat { unsigned char bytes[256]; };
template <typename Call> int converted(Call call, struct stat *out) {
    RawStat raw{};
    const int rc = call(reinterpret_cast<struct stat *>(raw.bytes));
    if (rc == 0 && out) pshare::ps4::translate_kernel_stat(raw.bytes, out);
    return rc;
}
}
extern "C" int pshare_ps4_stat(const char *path, struct stat *out) {
    return converted([path](struct stat *raw) { return ::stat(path, raw); }, out);
}
// OpenOrbis' lstat goes through an unimplemented fstatat (ENOSYS); captures
// are never symlinks, so follow links like stat does.
extern "C" int pshare_ps4_lstat(const char *path, struct stat *out) {
    return converted([path](struct stat *raw) { return ::stat(path, raw); }, out);
}
extern "C" int pshare_ps4_fstat(int fd, struct stat *out) {
    return converted([fd](struct stat *raw) { return ::fstat(fd, raw); }, out);
}
