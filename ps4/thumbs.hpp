// PShare PS4: gallery thumbnails decoded off the UI thread.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui_kit.hpp"
#include "app/thread.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace ps4ui {

class Thumbs {
public:
    static constexpr int kWidth = 328, kHeight = 184, kRadius = 14;
    ~Thumbs() { stop(); }
    void start();
    void stop();
    // Asks for a tile; newest requests are decoded first.
    void request(const std::string &path);
    // Uploads finished tiles; true when any arrived (the frame should redraw).
    bool pump(SDL_Renderer *renderer);
    // The tile, or null while decoding / when it can't be decoded.
    SDL_Texture *get(const std::string &path);
    bool failed(const std::string &path);
private:
    void run();
    struct Slot { SDL_Texture *texture = nullptr; std::uint32_t used = 0; bool failed = false; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> queue_;
    std::set<std::string> queued_;
    std::deque<std::pair<std::string, Image>> done_;
    std::map<std::string, Slot> slots_;
    std::uint32_t clock_ = 0;
    std::atomic<bool> quit_{false};
    pshare::app::Thread worker_;
};

} // namespace ps4ui
