// PShare PS4: thumbnail worker. SPDX-License-Identifier: GPL-3.0-or-later
#include "thumbs.hpp"
#include <algorithm>

namespace ps4ui {

void Thumbs::start() {
    quit_ = false;
    worker_.start([this] { run(); }, 2u << 20);
}
void Thumbs::stop() {
    quit_ = true;
    wake_.notify_all();
    worker_.join();
    for (auto &slot : slots_) if (slot.second.texture) SDL_DestroyTexture(slot.second.texture);
    slots_.clear();
}
void Thumbs::request(const std::string &path) {
    auto found = slots_.find(path);
    if (found != slots_.end()) { found->second.used = ++clock_; return; }
    std::lock_guard<std::mutex> lock(mutex_);
    if (queued_.count(path)) {
        // Move it to the front: what is on screen now comes first.
        auto it = std::find(queue_.begin(), queue_.end(), path);
        if (it != queue_.end()) { queue_.erase(it); queue_.push_front(path); }
        return;
    }
    queued_.insert(path);
    queue_.push_front(path);
    // Requests for tiles that scrolled away long ago are dropped.
    while (queue_.size() > 48) { queued_.erase(queue_.back()); queue_.pop_back(); }
    wake_.notify_one();
}
void Thumbs::run() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return quit_.load() || !queue_.empty(); });
            if (quit_) return;
            path = queue_.front();
            queue_.pop_front();
        }
        Image tile, full;
        if (decode_file(path, &full)) {
            tile = cover(full, kWidth, kHeight);
            round_corners(tile, kRadius);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(path);
        done_.emplace_back(path, std::move(tile));
    }
}
bool Thumbs::pump(SDL_Renderer *renderer) {
    std::deque<std::pair<std::string, Image>> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(done_);
    }
    for (auto &item : ready) {
        Slot slot;
        slot.used = ++clock_;
        slot.texture = item.second.empty() ? nullptr : upload(renderer, item.second);
        slot.failed = slot.texture == nullptr;
        auto old = slots_.find(item.first);
        if (old != slots_.end() && old->second.texture) SDL_DestroyTexture(old->second.texture);
        slots_[item.first] = slot;
    }
    // Keep about 80 tiles (240 KB each).
    while (slots_.size() > 80) {
        auto oldest = std::min_element(slots_.begin(), slots_.end(), [](const auto &a, const auto &b) { return a.second.used < b.second.used; });
        if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
        slots_.erase(oldest);
    }
    return !ready.empty();
}
SDL_Texture *Thumbs::get(const std::string &path) {
    auto found = slots_.find(path);
    if (found == slots_.end()) return nullptr;
    found->second.used = ++clock_;
    return found->second.texture;
}
bool Thumbs::failed(const std::string &path) {
    auto found = slots_.find(path);
    return found != slots_.end() && found->second.failed;
}

} // namespace ps4ui
