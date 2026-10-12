// PShare PS4: drawing kit implementation. SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_kit.hpp"
#include "pshare/core/config.hpp"
#include "third_party/stb_image/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ps4ui {

// ---- images ----------------------------------------------------------------

bool decode_file(const std::string &path, Image *out, int max_pixels) {
    std::string encoded;
    if (!pshare::read_text_file(path, &encoded, 64u << 20)) return false;
    int w = 0, h = 0, channels = 0;
    const auto *bytes = reinterpret_cast<const unsigned char *>(encoded.data());
    if (!stbi_info_from_memory(bytes, static_cast<int>(encoded.size()), &w, &h, &channels) || w <= 0 || h <= 0 ||
        static_cast<long long>(w) * h > max_pixels)
        return false;
    unsigned char *pixels = stbi_load_from_memory(bytes, static_cast<int>(encoded.size()), &w, &h, &channels, 4);
    if (!pixels) return false;
    out->w = w; out->h = h;
    out->rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

Image resize(const Image &in, int w, int h) {
    Image out; out.w = w; out.h = h; out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    if (in.empty() || w <= 0 || h <= 0) return out;
    const float sx = static_cast<float>(in.w) / w, sy = static_cast<float>(in.h) / h;
    if (sx >= 1.0f && sy >= 1.0f) {
        // Area average.
        for (int y = 0; y < h; ++y) {
            const int y0 = static_cast<int>(y * sy), y1 = std::max(y0 + 1, std::min(in.h, static_cast<int>((y + 1) * sy)));
            for (int x = 0; x < w; ++x) {
                const int x0 = static_cast<int>(x * sx), x1 = std::max(x0 + 1, std::min(in.w, static_cast<int>((x + 1) * sx)));
                unsigned sum[4] = {0, 0, 0, 0}; unsigned n = 0;
                for (int yy = y0; yy < y1; ++yy) {
                    const std::uint8_t *p = &in.rgba[(static_cast<std::size_t>(yy) * in.w + x0) * 4];
                    for (int xx = x0; xx < x1; ++xx, p += 4) { sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3]; ++n; }
                }
                std::uint8_t *o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>(sum[c] / n);
            }
        }
        return out;
    }
    // Bilinear.
    for (int y = 0; y < h; ++y) {
        const float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.0f, static_cast<float>(in.h - 1));
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, in.h - 1); const float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            const float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.0f, static_cast<float>(in.w - 1));
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, in.w - 1); const float tx = fx - x0;
            const std::uint8_t *a = &in.rgba[(static_cast<std::size_t>(y0) * in.w + x0) * 4], *b = &in.rgba[(static_cast<std::size_t>(y0) * in.w + x1) * 4];
            const std::uint8_t *c = &in.rgba[(static_cast<std::size_t>(y1) * in.w + x0) * 4], *d = &in.rgba[(static_cast<std::size_t>(y1) * in.w + x1) * 4];
            std::uint8_t *o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            for (int k = 0; k < 4; ++k) {
                const float top = a[k] + (b[k] - a[k]) * tx, bottom = c[k] + (d[k] - c[k]) * tx;
                o[k] = static_cast<std::uint8_t>(top + (bottom - top) * ty + 0.5f);
            }
        }
    }
    return out;
}

Image cover(const Image &in, int w, int h) {
    if (in.empty()) return Image{};
    const float want = static_cast<float>(w) / h, have = static_cast<float>(in.w) / in.h;
    int cw = in.w, ch = in.h, cx = 0, cy = 0;
    if (have > want) { cw = std::max(1, static_cast<int>(in.h * want)); cx = (in.w - cw) / 2; }
    else { ch = std::max(1, static_cast<int>(in.w / want)); cy = (in.h - ch) / 2; }
    Image crop; crop.w = cw; crop.h = ch; crop.rgba.resize(static_cast<std::size_t>(cw) * ch * 4);
    for (int y = 0; y < ch; ++y)
        std::memcpy(&crop.rgba[static_cast<std::size_t>(y) * cw * 4], &in.rgba[(static_cast<std::size_t>(y + cy) * in.w + cx) * 4],
                    static_cast<std::size_t>(cw) * 4);
    return resize(crop, w, h);
}

void round_corners(Image &image, float radius) {
    for (int y = 0; y < image.h; ++y)
        for (int x = 0; x < image.w; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float qx = std::max(std::max(radius - px, px - (image.w - radius)), 0.0f);
            const float qy = std::max(std::max(radius - py, py - (image.h - radius)), 0.0f);
            if (qx <= 0.0f || qy <= 0.0f) continue;
            const float d = std::sqrt(qx * qx + qy * qy) - radius;
            const float cov = std::clamp(0.5f - d, 0.0f, 1.0f);
            std::uint8_t &a = image.rgba[(static_cast<std::size_t>(y) * image.w + x) * 4 + 3];
            a = static_cast<std::uint8_t>(a * cov);
        }
}

SDL_Texture *upload(SDL_Renderer *renderer, const Image &image, bool blend) {
    if (image.empty()) return nullptr;
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, image.rgba.data(), image.w * 4);
    SDL_SetTextureBlendMode(t, blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    return t;
}

// ---- canvas ----------------------------------------------------------------

namespace {
float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    const float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const float len = bax * bax + bay * bay;
    const float t = len > 0 ? std::clamp((pax * bax + pay * bay) / len, 0.0f, 1.0f) : 0.0f;
    const float dx = pax - bax * t, dy = pay - bay * t;
    return std::sqrt(dx * dx + dy * dy);
}
float rrect_sdf(float px, float py, float x, float y, float w, float h, float r) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float qx = std::fabs(px - cx) - (w * 0.5f - r), qy = std::fabs(py - cy) - (h * 0.5f - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}
float poly_sdf(float px, float py, const std::vector<float> &v) {
    const std::size_t n = v.size() / 2;
    float d = (px - v[0]) * (px - v[0]) + (py - v[1]) * (py - v[1]);
    float s = 1.0f;
    for (std::size_t i = 0, j = n - 1; i < n; j = i, ++i) {
        const float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1];
        const float wx = px - v[2 * i], wy = py - v[2 * i + 1];
        const float t = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c1 = py >= v[2 * i + 1], c2 = py < v[2 * j + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * std::sqrt(d);
}
}

template <typename Sdf> void Canvas::apply(Sdf sdf, bool cut, float x0, float y0, float x1, float y1) {
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0)) - 2), iy0 = std::max(0, static_cast<int>(std::floor(y0)) - 2);
    const int ix1 = std::min(w_, static_cast<int>(std::ceil(x1)) + 2), iy1 = std::min(h_, static_cast<int>(std::ceil(y1)) + 2);
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float c = std::clamp(0.5f - static_cast<float>(sdf(x + 0.5f, y + 0.5f)), 0.0f, 1.0f);
            float &cell = cov_[static_cast<std::size_t>(y) * w_ + x];
            cell = cut ? std::min(cell, 1.0f - c) : std::max(cell, c);
        }
}
void Canvas::circle(float cx, float cy, float r, bool cut) {
    apply([=](float px, float py) { return std::hypot(px - cx, py - cy) - r; }, cut, cx - r, cy - r, cx + r, cy + r);
}
void Canvas::ring(float cx, float cy, float r, float t, bool cut) {
    apply([=](float px, float py) { return std::fabs(std::hypot(px - cx, py - cy) - r) - t * 0.5f; }, cut, cx - r - t, cy - r - t, cx + r + t, cy + r + t);
}
void Canvas::capsule(float ax, float ay, float bx, float by, float t, bool cut) {
    apply([=](float px, float py) { return seg_dist(px, py, ax, ay, bx, by) - t * 0.5f; }, cut,
          std::min(ax, bx) - t, std::min(ay, by) - t, std::max(ax, bx) + t, std::max(ay, by) + t);
}
void Canvas::polygon(const std::vector<float> &v, bool cut) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) { x0 = std::min(x0, v[i]); x1 = std::max(x1, v[i]); y0 = std::min(y0, v[i + 1]); y1 = std::max(y1, v[i + 1]); }
    apply([&v](float px, float py) { return poly_sdf(px, py, v); }, cut, x0, y0, x1, y1);
}
void Canvas::polygon_stroke(const std::vector<float> &v, float t, bool cut) {
    const std::size_t n = v.size() / 2;
    for (std::size_t i = 0, j = n - 1; i < n; j = i, ++i) capsule(v[2 * j], v[2 * j + 1], v[2 * i], v[2 * i + 1], t, cut);
}
void Canvas::rrect(float x, float y, float w, float h, float r, bool cut) {
    apply([=](float px, float py) { return rrect_sdf(px, py, x, y, w, h, r); }, cut, x, y, x + w, y + h);
}
void Canvas::rrect_stroke(float x, float y, float w, float h, float r, float t, bool cut) {
    apply([=](float px, float py) { return std::fabs(rrect_sdf(px, py, x, y, w, h, r)) - t * 0.5f; }, cut, x - t, y - t, x + w + t, y + h + t);
}
void Canvas::arc(float cx, float cy, float r, float t, float from, float to) {
    const int steps = std::max(8, static_cast<int>(std::fabs(to - from) * r / 3.0f));
    for (int i = 0; i < steps; ++i) {
        const float a0 = from + (to - from) * i / steps, a1 = from + (to - from) * (i + 1) / steps;
        capsule(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, t);
    }
}
Image Canvas::image() const {
    Image out; out.w = w_; out.h = h_; out.rgba.resize(cov_.size() * 4);
    for (std::size_t i = 0; i < cov_.size(); ++i) {
        out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = 255;
        out.rgba[i * 4 + 3] = static_cast<std::uint8_t>(cov_[i] * 255.0f + 0.5f);
    }
    return out;
}

// ---- fonts -----------------------------------------------------------------

struct Fonts::FaceData {
    std::string bytes;
    stbtt_fontinfo info{};
};
Fonts::~Fonts() {
    for (auto &entry : cache_) SDL_DestroyTexture(entry.second.texture);
    for (auto &face : faces_) delete face.second;
}
bool Fonts::load(Face f, const std::string &path) {
    auto *data = new FaceData;
    if (!pshare::read_text_file(path, &data->bytes, 8u << 20) ||
        !stbtt_InitFont(&data->info, reinterpret_cast<const unsigned char *>(data->bytes.data()), 0)) {
        delete data;
        return false;
    }
    delete faces_[static_cast<int>(f)];
    faces_[static_cast<int>(f)] = data;
    return true;
}
bool Fonts::loaded(Face f) const { return faces_.count(static_cast<int>(f)) != 0; }
Fonts::FaceData *Fonts::face(Face f) {
    auto it = faces_.find(static_cast<int>(f));
    if (it != faces_.end()) return it->second;
    it = faces_.find(static_cast<int>(Face::regular));
    return it == faces_.end() ? nullptr : it->second;
}
namespace {
std::vector<unsigned> decode_utf8(const std::string &text) {
    std::vector<unsigned> out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = text[i++]; unsigned cp = lead; int extra = 0;
        if (lead >= 0xc0 && lead < 0xe0) { cp = lead & 31; extra = 1; }
        else if (lead >= 0xe0 && lead < 0xf0) { cp = lead & 15; extra = 2; }
        else if (lead >= 0xf0) { cp = lead & 7; extra = 3; }
        for (int k = 0; k < extra; ++k) {
            if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) { cp = '?'; break; }
            cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        out.push_back(cp);
    }
    return out;
}
}
int Fonts::measure(Face f, const std::string &text, int size) {
    FaceData *fd = face(f);
    if (!fd) return 0;
    const float scale = stbtt_ScaleForPixelHeight(&fd->info, static_cast<float>(size));
    const auto cps = decode_utf8(text);
    float pen = 0;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&fd->info, static_cast<int>(cps[i]), &advance, &bearing);
        pen += advance * scale;
        if (i + 1 < cps.size()) pen += stbtt_GetCodepointKernAdvance(&fd->info, static_cast<int>(cps[i]), static_cast<int>(cps[i + 1])) * scale;
    }
    return static_cast<int>(std::ceil(pen));
}
int Fonts::draw(SDL_Renderer *r, Face f, const std::string &text, int x, int y, int size, Color color) {
    if (text.empty()) return 0;
    FaceData *fd = face(f);
    if (!fd) return 0;
    const std::string key = std::to_string(static_cast<int>(f)) + ":" + std::to_string(size) + ":" + text;
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        const float scale = stbtt_ScaleForPixelHeight(&fd->info, static_cast<float>(size));
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&fd->info, &ascent, &descent, &gap);
        const int baseline = static_cast<int>(std::ceil(ascent * scale));
        const int h = baseline + static_cast<int>(std::ceil(-descent * scale)) + 2;
        const int w = std::max(1, measure(f, text, size) + 4);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0;
        const auto cps = decode_utf8(text);
        float pen = 1.0f;
        for (std::size_t i = 0; i < cps.size(); ++i) {
            const int cp = static_cast<int>(cps[i]);
            int bw = 0, bh = 0, bx = 0, by = 0;
            const float shift = pen - std::floor(pen);
            unsigned char *bitmap = stbtt_GetCodepointBitmapSubpixel(&fd->info, scale, scale, shift, 0, cp, &bw, &bh, &bx, &by);
            const int ox = static_cast<int>(std::floor(pen)) + bx, oy = baseline + by;
            for (int row = 0; row < bh; ++row)
                for (int col = 0; col < bw; ++col) {
                    const int px = ox + col, py = oy + row;
                    if (px < 0 || px >= w || py < 0 || py >= h) continue;
                    std::uint8_t &a = pixels[(static_cast<std::size_t>(py) * w + px) * 4 + 3];
                    a = std::max(a, bitmap[row * bw + col]);
                }
            stbtt_FreeBitmap(bitmap, nullptr);
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&fd->info, cp, &advance, &bearing);
            pen += advance * scale;
            if (i + 1 < cps.size()) pen += stbtt_GetCodepointKernAdvance(&fd->info, cp, static_cast<int>(cps[i + 1])) * scale;
        }
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) return 0;
        SDL_UpdateTexture(t, nullptr, pixels.data(), w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        found = cache_.emplace(key, Entry{t, w, h, 0}).first;
    }
    found->second.used = ++clock_;
    SDL_SetTextureColorMod(found->second.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(found->second.texture, color.a);
    SDL_Rect dest{x - 1, y, found->second.w, found->second.h};
    SDL_RenderCopy(r, found->second.texture, nullptr, &dest);
    return found->second.w - 4;
}
void Fonts::trim_cache() {
    if (cache_.size() < 700) return;
    std::vector<std::uint32_t> ages;
    for (auto &e : cache_) ages.push_back(e.second.used);
    std::nth_element(ages.begin(), ages.begin() + ages.size() / 2, ages.end());
    const std::uint32_t cut = ages[ages.size() / 2];
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.used < cut) { SDL_DestroyTexture(it->second.texture); it = cache_.erase(it); }
        else ++it;
    }
}
std::string Fonts::fit(Face f, const std::string &text, int size, int max_width) {
    if (measure(f, text, size) <= max_width) return text;
    const auto cps = decode_utf8(text);
    std::string best = "\xE2\x80\xA6";
    std::size_t lo = 0, hi = cps.size();
    auto encode = [&](std::size_t n) {
        std::string s;
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned c = cps[i];
            if (c < 0x80) s += static_cast<char>(c);
            else if (c < 0x800) { s += static_cast<char>(0xc0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 63)); }
            else if (c < 0x10000) { s += static_cast<char>(0xe0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
            else { s += static_cast<char>(0xf0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 63)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
        }
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s + "\xE2\x80\xA6";
    };
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        const std::string candidate = encode(mid);
        if (measure(f, candidate, size) <= max_width) { best = candidate; lo = mid; } else hi = mid - 1;
    }
    return best;
}
std::vector<std::string> Fonts::wrap(Face f, const std::string &text, int size, int max_width, int max_lines) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (measure(f, candidate, size) <= max_width || line.empty()) {
            // A single word wider than the line (a URL) is broken by characters.
            if (line.empty() && measure(f, word, size) > max_width) {
                std::string part;
                for (char c : word) {
                    if (measure(f, part + c, size) > max_width && !part.empty()) { lines.push_back(part); part.clear(); }
                    part += c;
                }
                line = part;
            } else line = candidate;
        } else { lines.push_back(line); line = word; }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == '\n') { flush_word(); if (c == '\n') { lines.push_back(line); line.clear(); } }
        else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    if (static_cast<int>(lines.size()) > max_lines) {
        lines.resize(max_lines);
        lines.back() = fit(f, lines.back() + " \xE2\x80\xA6", size, max_width);
    }
    return lines;
}

// ---- painter ---------------------------------------------------------------

Painter::~Painter() { clear_cache(); if (backdrop_) SDL_DestroyTexture(backdrop_); }
void Painter::clear_cache() { for (auto &s : shapes_) SDL_DestroyTexture(s.second); shapes_.clear(); }

void Painter::fill(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(r_, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r_, &rect);
}
SDL_Texture *Painter::shape(const std::string &key, int w, int h, void (*paint)(Canvas &, const float *), const float *args) {
    auto found = shapes_.find(key);
    if (found != shapes_.end()) return found->second;
    Canvas canvas(w, h);
    paint(canvas, args);
    SDL_Texture *t = upload(r_, canvas.image());
    shapes_[key] = t;
    return t;
}
SDL_Texture *Painter::disc(int radius) {
    const float a[] = {static_cast<float>(radius)};
    return shape("disc" + std::to_string(radius), radius * 2, radius * 2, [](Canvas &c, const float *p) { c.circle(p[0], p[0], p[0]); }, a);
}
SDL_Texture *Painter::ring_tex(int radius, int thickness) {
    const float a[] = {static_cast<float>(radius), static_cast<float>(thickness)};
    return shape("ring" + std::to_string(radius) + ":" + std::to_string(thickness), radius * 2, radius * 2,
                 [](Canvas &c, const float *p) { c.ring(p[0], p[0], p[0] - p[1] * 0.5f, p[1]); }, a);
}
void Painter::quadrants(SDL_Texture *t, int radius, int x, int y, int w, int h, Color c) {
    if (!t) return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    const SDL_Rect src[4] = {{0, 0, radius, radius}, {radius, 0, radius, radius}, {0, radius, radius, radius}, {radius, radius, radius, radius}};
    const SDL_Rect dst[4] = {{x, y, radius, radius}, {x + w - radius, y, radius, radius}, {x, y + h - radius, radius, radius}, {x + w - radius, y + h - radius, radius, radius}};
    for (int i = 0; i < 4; ++i) SDL_RenderCopy(r_, t, &src[i], &dst[i]);
}
void Painter::rrect(int x, int y, int w, int h, int radius, Color c) {
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    if (radius == 0) { fill(x, y, w, h, c); return; }
    quadrants(disc(radius), radius, x, y, w, h, c);
    fill(x + radius, y, w - 2 * radius, radius, c);
    fill(x, y + radius, w, h - 2 * radius, c);
    fill(x + radius, y + h - radius, w - 2 * radius, radius, c);
}
void Painter::rrect_stroke(int x, int y, int w, int h, int radius, int t, Color c) {
    radius = std::max(t, std::min(radius, std::min(w, h) / 2));
    quadrants(ring_tex(radius, t), radius, x, y, w, h, c);
    fill(x + radius, y, w - 2 * radius, t, c);
    fill(x + radius, y + h - t, w - 2 * radius, t, c);
    fill(x, y + radius, t, h - 2 * radius, c);
    fill(x + w - t, y + radius, t, h - 2 * radius, c);
}
void Painter::circle(int cx, int cy, int radius, Color c) {
    SDL_Texture *t = disc(radius);
    if (!t) return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect dst{cx - radius, cy - radius, radius * 2, radius * 2};
    SDL_RenderCopy(r_, t, nullptr, &dst);
}
void Painter::texture(SDL_Texture *t, int x, int y, int w, int h, float alpha) {
    if (!t) return;
    SDL_SetTextureAlphaMod(t, static_cast<std::uint8_t>(alpha * 255));
    SDL_SetTextureColorMod(t, 255, 255, 255);
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(r_, t, nullptr, &dst);
}
void Painter::fit_texture(SDL_Texture *t, int tw, int th, SDL_Rect area) {
    if (!t || tw <= 0 || th <= 0) return;
    const float scale = std::min(static_cast<float>(area.w) / tw, static_cast<float>(area.h) / th);
    const int w = static_cast<int>(tw * scale), h = static_cast<int>(th * scale);
    texture(t, area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h);
}

namespace {
Image box_blur(const Image &in, int radius) {
    Image tmp = in, out = in;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) {
                unsigned s[3] = {0, 0, 0}; int n = 0;
                for (int k = -radius; k <= radius; ++k) {
                    const int xx = std::clamp(x + k, 0, in.w - 1);
                    const std::uint8_t *p = &out.rgba[(static_cast<std::size_t>(y) * in.w + xx) * 4];
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; ++n;
                }
                std::uint8_t *o = &tmp.rgba[(static_cast<std::size_t>(y) * in.w + x) * 4];
                o[0] = s[0] / n; o[1] = s[1] / n; o[2] = s[2] / n; o[3] = 255;
            }
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) {
                unsigned s[3] = {0, 0, 0}; int n = 0;
                for (int k = -radius; k <= radius; ++k) {
                    const int yy = std::clamp(y + k, 0, in.h - 1);
                    const std::uint8_t *p = &tmp.rgba[(static_cast<std::size_t>(yy) * in.w + x) * 4];
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; ++n;
                }
                std::uint8_t *o = &out.rgba[(static_cast<std::size_t>(y) * in.w + x) * 4];
                o[0] = s[0] / n; o[1] = s[1] / n; o[2] = s[2] / n; o[3] = 255;
            }
    }
    return out;
}
}

void Painter::set_backdrop(const Image &sharp) {
    if (backdrop_) SDL_DestroyTexture(backdrop_);
    Image full = sharp.w == 1920 && sharp.h == 1080 ? sharp : cover(sharp, 1920, 1080);
    backdrop_ = upload(r_, full, false);
    // Blur at quarter size; panels sample it back up.
    blurred_ = box_blur(resize(full, 480, 270), 5);
}
void Painter::backdrop() {
    if (backdrop_) { SDL_RenderCopy(r_, backdrop_, nullptr, nullptr); return; }
    fill(0, 0, 1920, 1080, hex(pal::night));
}
void Painter::glass(int x, int y, int w, int h, int radius, Color tint, bool border) {
    const std::string key = "glass" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h) + "," +
                            std::to_string(radius) + "," + std::to_string(tint.r) + "," + std::to_string(tint.g) + "," + std::to_string(tint.b) + "," +
                            std::to_string(tint.a);
    if (shapes_.size() > 400) clear_cache(); // panels at many positions (toasts, chips)
    auto found = shapes_.find(key);
    SDL_Texture *t = found == shapes_.end() ? nullptr : found->second;
    if (!t) {
        Image panel; panel.w = w; panel.h = h; panel.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        const float mix = tint.a / 255.0f;
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                std::uint8_t base[3] = {static_cast<std::uint8_t>(0x15), static_cast<std::uint8_t>(0x12), static_cast<std::uint8_t>(0x3a)};
                if (!blurred_.empty()) {
                    // Bilinear sample of the quarter-size blur.
                    const float fx = std::clamp((x + px) / 4.0f - 0.5f, 0.0f, blurred_.w - 1.0f), fy = std::clamp((y + py) / 4.0f - 0.5f, 0.0f, blurred_.h - 1.0f);
                    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy), x1 = std::min(x0 + 1, blurred_.w - 1), y1 = std::min(y0 + 1, blurred_.h - 1);
                    const float tx = fx - x0, ty = fy - y0;
                    for (int k = 0; k < 3; ++k) {
                        const float a = blurred_.rgba[(static_cast<std::size_t>(y0) * blurred_.w + x0) * 4 + k], b = blurred_.rgba[(static_cast<std::size_t>(y0) * blurred_.w + x1) * 4 + k];
                        const float c = blurred_.rgba[(static_cast<std::size_t>(y1) * blurred_.w + x0) * 4 + k], d = blurred_.rgba[(static_cast<std::size_t>(y1) * blurred_.w + x1) * 4 + k];
                        const float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
                        base[k] = static_cast<std::uint8_t>(top + (bottom - top) * ty);
                    }
                }
                const std::uint8_t tc[3] = {tint.r, tint.g, tint.b};
                std::uint8_t *o = &panel.rgba[(static_cast<std::size_t>(py) * w + px) * 4];
                // A faint top-to-bottom sheen, like the PS5 acrylic.
                const float sheen = 1.0f + 0.06f * (1.0f - static_cast<float>(py) / h);
                for (int k = 0; k < 3; ++k) o[k] = static_cast<std::uint8_t>(std::min(255.0f, (base[k] * (1.0f - mix) + tc[k] * mix) * sheen));
                o[3] = 255;
            }
        round_corners(panel, static_cast<float>(radius));
        t = upload(r_, panel);
        shapes_[key] = t;
    }
    texture(t, x, y, w, h);
    if (border) rrect_stroke(x, y, w, h, radius, 2, hex(0xffffff, 0.10f));
}

// ---- icons -----------------------------------------------------------------

namespace {
using Paint = void (*)(Canvas &, const float *);
struct IconDef { const char *name; Paint paint; };
// Icons are drawn on a 0..1 square scaled by s = p[0].
const IconDef kIcons[] = {
    {"captures", [](Canvas &c, const float *p) { const float s = p[0];
        c.rrect_stroke(s * .12f, s * .2f, s * .76f, s * .6f, s * .1f, s * .08f);
        c.polygon({s * .2f, s * .72f, s * .42f, s * .45f, s * .58f, s * .62f, s * .66f, s * .54f, s * .82f, s * .72f});
        c.circle(s * .66f, s * .36f, s * .07f); }},
    {"auto", [](Canvas &c, const float *p) { const float s = p[0];
        c.arc(s * .5f, s * .5f, s * .34f, s * .08f, -1.2f, 4.2f);
        c.polygon({s * .74f, s * .06f, s * .86f, s * .26f, s * .64f, s * .3f});
        c.polygon({s * .54f, s * .26f, s * .38f, s * .54f, s * .5f, s * .54f, s * .44f, s * .76f, s * .64f, s * .46f, s * .52f, s * .46f}); }},
    {"dest", [](Canvas &c, const float *p) { const float s = p[0];
        c.polygon({s * .1f, s * .46f, s * .9f, s * .14f, s * .58f, s * .9f, s * .46f, s * .58f}); }},
    {"history", [](Canvas &c, const float *p) { const float s = p[0];
        c.ring(s * .5f, s * .5f, s * .36f, s * .08f);
        c.capsule(s * .5f, s * .5f, s * .5f, s * .28f, s * .08f);
        c.capsule(s * .5f, s * .5f, s * .66f, s * .6f, s * .08f); }},
    {"settings", [](Canvas &c, const float *p) { const float s = p[0];
        for (int i = 0; i < 8; ++i) { const float a = i * 3.14159265f / 4; c.capsule(s * .5f, s * .5f, s * (.5f + .36f * std::cos(a)), s * (.5f + .36f * std::sin(a)), s * .14f); }
        c.circle(s * .5f, s * .5f, s * .28f); c.circle(s * .5f, s * .5f, s * .12f, true); }},
    {"check", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .22f, s * .52f, s * .42f, s * .72f, s * .12f); c.capsule(s * .42f, s * .72f, s * .78f, s * .3f, s * .12f); }},
    {"cross", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .26f, s * .26f, s * .74f, s * .74f, s * .12f); c.capsule(s * .74f, s * .26f, s * .26f, s * .74f, s * .12f); }},
    {"play", [](Canvas &c, const float *p) { const float s = p[0]; c.polygon({s * .3f, s * .18f, s * .84f, s * .5f, s * .3f, s * .82f}); }},
    {"chevron", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .38f, s * .2f, s * .66f, s * .5f, s * .1f); c.capsule(s * .66f, s * .5f, s * .38f, s * .8f, s * .1f); }},
    {"chevron_down", [](Canvas &c, const float *p) { const float s = p[0];
        c.polygon({s * .25f, s * .36f, s * .75f, s * .36f, s * .5f, s * .66f}); }},
    {"bang", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .5f, s * .22f, s * .5f, s * .58f, s * .14f); c.circle(s * .5f, s * .78f, s * .08f); }},
    {"filter", [](Canvas &c, const float *p) { const float s = p[0];
        c.polygon({s * .14f, s * .2f, s * .86f, s * .2f, s * .58f, s * .54f, s * .58f, s * .84f, s * .42f, s * .74f, s * .42f, s * .54f}); }},
    {"sort", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .32f, s * .16f, s * .32f, s * .84f, s * .09f); c.polygon({s * .14f, s * .64f, s * .5f, s * .64f, s * .32f, s * .88f});
        c.capsule(s * .6f, s * .3f, s * .9f, s * .3f, s * .09f); c.capsule(s * .6f, s * .5f, s * .82f, s * .5f, s * .09f); c.capsule(s * .6f, s * .7f, s * .72f, s * .7f, s * .09f); }},
    {"link", [](Canvas &c, const float *p) { const float s = p[0];
        c.ring(s * .36f, s * .64f, s * .17f, s * .09f); c.ring(s * .64f, s * .36f, s * .17f, s * .09f); c.capsule(s * .42f, s * .58f, s * .58f, s * .42f, s * .09f); }},
    {"phone", [](Canvas &c, const float *p) { const float s = p[0];
        c.rrect_stroke(s * .28f, s * .1f, s * .44f, s * .8f, s * .08f, s * .08f); c.capsule(s * .44f, s * .78f, s * .56f, s * .78f, s * .06f); }},
    {"image", [](Canvas &c, const float *p) { const float s = p[0];
        c.rrect_stroke(s * .1f, s * .2f, s * .8f, s * .6f, s * .08f, s * .06f);
        c.polygon({s * .18f, s * .72f, s * .4f, s * .46f, s * .56f, s * .62f, s * .64f, s * .54f, s * .82f, s * .72f}); c.circle(s * .66f, s * .36f, s * .07f); }},
    {"clock", [](Canvas &c, const float *p) { const float s = p[0];
        c.ring(s * .5f, s * .5f, s * .36f, s * .09f); c.capsule(s * .5f, s * .5f, s * .5f, s * .28f, s * .09f); c.capsule(s * .5f, s * .5f, s * .66f, s * .58f, s * .09f); }},
    {"bolt", [](Canvas &c, const float *p) { const float s = p[0];
        c.polygon({s * .58f, s * .06f, s * .22f, s * .56f, s * .46f, s * .56f, s * .38f, s * .94f, s * .78f, s * .42f, s * .54f, s * .42f}); }},
    {"options", [](Canvas &c, const float *p) { const float s = p[0];
        for (int i = 0; i < 3; ++i) c.capsule(s * .26f, s * (.32f + i * .18f), s * .74f, s * (.32f + i * .18f), s * .08f); }},
    // PlayStation button symbols.
    {"btn_cross", [](Canvas &c, const float *p) { const float s = p[0];
        c.capsule(s * .24f, s * .24f, s * .76f, s * .76f, s * .1f); c.capsule(s * .76f, s * .24f, s * .24f, s * .76f, s * .1f); }},
    {"btn_circle", [](Canvas &c, const float *p) { const float s = p[0]; c.ring(s * .5f, s * .5f, s * .3f, s * .1f); }},
    {"btn_square", [](Canvas &c, const float *p) { const float s = p[0]; c.rrect_stroke(s * .22f, s * .22f, s * .56f, s * .56f, s * .04f, s * .1f); }},
    {"btn_triangle", [](Canvas &c, const float *p) { const float s = p[0]; c.polygon_stroke({s * .5f, s * .18f, s * .82f, s * .76f, s * .18f, s * .76f}, s * .1f); }},
    {"touchpad", [](Canvas &c, const float *p) { const float s = p[0]; c.rrect_stroke(s * .06f, s * .26f, s * .88f, s * .48f, s * .1f, s * .07f); }},
};
}

void Painter::icon(const std::string &name, int x, int y, int size, Color c) {
    const std::string key = "icon:" + name + ":" + std::to_string(size);
    SDL_Texture *t = nullptr;
    auto found = shapes_.find(key);
    if (found != shapes_.end()) t = found->second;
    else {
        for (const auto &def : kIcons)
            if (name == def.name) {
                const float a[] = {static_cast<float>(size)};
                t = shape(key, size, size, def.paint, a);
                break;
            }
    }
    if (!t) return;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect dst{x, y, size, size};
    SDL_RenderCopy(r_, t, nullptr, &dst);
}

} // namespace ps4ui
