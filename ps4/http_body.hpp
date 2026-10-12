// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pshare/core/upload.hpp"
#include <cstdio>
#include <sys/stat.h>
namespace pshare::ps4 {
constexpr std::size_t kBodyLimit = 256u << 20;
inline std::string quoted(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == '\0') continue;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}
inline bool append_checked(std::string &body, std::string_view data, std::string &error) {
    if (data.size() > kBodyLimit - body.size()) {
        error = "PS4 HTTP upload exceeds 256 MiB; use USB/folder for larger clips";
        return false;
    }
    body.append(data);
    return true;
}
inline bool append_file(std::string &body, const std::string &path, std::string &error,
                        UploadProgress *progress) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
        error = "Cannot read capture"; return false;
    }
    if (static_cast<std::uint64_t>(info.st_size) > kBodyLimit - body.size()) {
        error = "PS4 HTTP upload exceeds 256 MiB; use USB/folder for larger clips"; return false;
    }
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) { error = "Cannot open capture"; return false; }
    char buffer[65536];
    bool ok = true;
    std::uint64_t total = 0;
    for (;;) {
        if (progress && progress->cancel.load()) { error = "Cancelled"; ok = false; break; }
        const auto got = std::fread(buffer, 1, sizeof(buffer), file);
        if (got && !append_checked(body, std::string_view(buffer, got), error)) { ok = false; break; }
        total += got;
        if (got < sizeof(buffer)) {
            if (std::ferror(file) || total != static_cast<std::uint64_t>(info.st_size)) {
                error = "Capture changed or read failed"; ok = false;
            }
            break;
        }
    }
    std::fclose(file);
    return ok;
}
inline std::string part_head(const FormPart &part, std::string_view boundary) {
    std::string head = "--" + std::string(boundary) + "\r\nContent-Disposition: form-data; name=\"" + quoted(part.name) + "\"";
    if (part.is_file()) head += "; filename=\"" + quoted(part.file_name) + "\"\r\nContent-Type: " + part.content_type;
    head += "\r\n\r\n";
    return head;
}
inline std::uint64_t file_size_or_zero(const std::string &path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
}
// The whole body's size, so it is allocated once: growing a string by
// doubling would briefly need up to twice the upload in memory.
inline std::uint64_t planned_size(const HttpRequest &request, std::string_view boundary) {
    if (request.body == BodyKind::none) return 0;
    if (request.body == BodyKind::raw_file) return file_size_or_zero(request.raw_path);
    std::uint64_t total = 0;
    for (const auto &part : request.parts)
        total += part_head(part, boundary).size() + (part.is_file() ? file_size_or_zero(part.file_path) : part.value.size()) + 2;
    return total + boundary.size() + 6;
}
inline bool make_body(const HttpRequest &request, std::string_view boundary, std::string &body,
                      std::string &content_type, std::string &error, UploadProgress *progress) {
    body.clear(); content_type.clear();
    if (request.body == BodyKind::none) return true;
    const std::uint64_t planned = planned_size(request, boundary);
    if (planned > kBodyLimit) { error = "PS4 HTTP upload exceeds 256 MiB; use USB/folder for larger clips"; return false; }
    body.reserve(static_cast<std::size_t>(planned));
    if (request.body == BodyKind::raw_file) {
        content_type = request.raw_type;
        return append_file(body, request.raw_path, error, progress);
    }
    content_type = "multipart/form-data; boundary=" + std::string(boundary);
    for (const auto &part : request.parts) {
        const std::string head = part_head(part, boundary);
        if (!append_checked(body, head, error)) return false;
        if (part.is_file()) { if (!append_file(body, part.file_path, error, progress)) return false; }
        else if (!append_checked(body, part.value, error)) return false;
        if (!append_checked(body, "\r\n", error)) return false;
    }
    return append_checked(body, "--" + std::string(boundary) + "--\r\n", error);
}
}
