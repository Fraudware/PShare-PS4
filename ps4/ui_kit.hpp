// PShare PS4: a small drawing kit for the PS5 look on SDL2's renderer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PS5 build draws with OpenGL 4.6 shaders; the PS4's GPU library (Piglet)
// only offers OpenGL ES 2 with offline-compiled shaders, so this port draws
// the same design with SDL2's renderer instead: shapes are rasterised once on
// the CPU (signed distance fields, anti-aliased) into cached textures, the
// "glass" panels are cut from a pre-blurred copy of the backdrop, and frames
// are only redrawn when something changes.
#pragma once
#include <SDL.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps4ui {

struct Color {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};
inline Color hex(std::uint32_t v, float alpha = 1.0f) {
    return Color{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v),
                 static_cast<std::uint8_t>(alpha * 255.0f + 0.5f)};
}
inline Color with_alpha(Color c, float alpha) { c.a = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f); return c; }

// The PS5 palette (src/app/style.hpp).
namespace pal {
constexpr std::uint32_t night = 0x15123a, night2 = 0x211c55, ink = 0x0e0b28, violet = 0x6b4ce6, plum = 0x963cbe,
                        coral = 0xff5c8a, aqua = 0x3ee6e0, sun = 0xffd25e, cream = 0xfff4e6, mist = 0xb3acdf,
                        red = 0xff5d6c;
}

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    bool empty() const { return w <= 0 || h <= 0; }
};
bool decode_file(const std::string &path, Image *out, int max_pixels = 40000000);
Image resize(const Image &in, int w, int h);           // area-average down, bilinear up
Image cover(const Image &in, int w, int h);            // centre-crop to the aspect, then resize
void round_corners(Image &image, float radius);        // alpha mask
SDL_Texture *upload(SDL_Renderer *renderer, const Image &image, bool blend = true);

// Anti-aliased coverage canvas: shapes are unions (or cut-outs) of SDFs.
class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), cov_(static_cast<std::size_t>(w) * h, 0.0f) {}
    void circle(float cx, float cy, float r, bool cut = false);
    void ring(float cx, float cy, float r, float thickness, bool cut = false);
    void capsule(float x0, float y0, float x1, float y1, float thickness, bool cut = false);
    void polygon(const std::vector<float> &xy, bool cut = false);
    void polygon_stroke(const std::vector<float> &xy, float thickness, bool cut = false);
    void rrect(float x, float y, float w, float h, float r, bool cut = false);
    void rrect_stroke(float x, float y, float w, float h, float r, float thickness, bool cut = false);
    void arc(float cx, float cy, float r, float thickness, float from, float to); // radians, clockwise from +x
    Image image() const; // white, alpha = coverage
    int width() const { return w_; }
    int height() const { return h_; }
private:
    template <typename Sdf> void apply(Sdf sdf, bool cut, float x0, float y0, float x1, float y1);
    int w_, h_;
    std::vector<float> cov_;
};

enum class Face { regular, semibold, display, mono };

class Fonts {
public:
    ~Fonts();
    bool load(Face face, const std::string &path);
    int measure(Face face, const std::string &text, int size);
    // Draws with the top-left of the line box at (x, y); returns the width.
    int draw(SDL_Renderer *r, Face face, const std::string &text, int x, int y, int size, Color color);
    // Shortens with an ellipsis to fit max_width.
    std::string fit(Face face, const std::string &text, int size, int max_width);
    std::vector<std::string> wrap(Face face, const std::string &text, int size, int max_width, int max_lines = 8);
    void trim_cache();
    bool loaded(Face face) const;
private:
    struct Entry { SDL_Texture *texture = nullptr; int w = 0, h = 0; std::uint32_t used = 0; };
    struct FaceData;
    FaceData *face(Face f);
    std::map<int, FaceData *> faces_;
    std::map<std::string, Entry> cache_;
    std::uint32_t clock_ = 0;
};

// Shapes and icons as cached textures.
class Painter {
public:
    explicit Painter(SDL_Renderer *r) : r_(r) {}
    ~Painter();
    SDL_Renderer *renderer() const { return r_; }
    void fill(int x, int y, int w, int h, Color c);
    void rrect(int x, int y, int w, int h, int radius, Color c);
    void rrect_stroke(int x, int y, int w, int h, int radius, int thickness, Color c);
    void circle(int cx, int cy, int radius, Color c);
    void icon(const std::string &name, int x, int y, int size, Color c);
    void texture(SDL_Texture *t, int x, int y, int w, int h, float alpha = 1.0f);
    void fit_texture(SDL_Texture *t, int tw, int th, SDL_Rect area);
    // Frosted panel: the blurred backdrop under the rect, tinted, rounded.
    void set_backdrop(const Image &sharp);
    void backdrop(); // the sharp backdrop over the whole frame
    void glass(int x, int y, int w, int h, int radius, Color tint, bool border = true);
    void clear_cache();
private:
    SDL_Texture *shape(const std::string &key, int w, int h, void (*paint)(Canvas &, const float *), const float *args);
    SDL_Texture *disc(int radius);
    SDL_Texture *ring_tex(int radius, int thickness);
    void quadrants(SDL_Texture *t, int radius, int x, int y, int w, int h, Color c);
    SDL_Renderer *r_;
    std::map<std::string, SDL_Texture *> shapes_;
    Image blurred_;
    SDL_Texture *backdrop_ = nullptr;
};

} // namespace ps4ui
