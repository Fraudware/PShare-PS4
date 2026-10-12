// PShare PS4 frontend: the PS5 app's look, drawn with SDL2.
// Copyright (C) 2026 PShare contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDL.h>
#undef main
#include "pshare/core/config.hpp"
#include "pshare/core/media.hpp"
#include "pshare/core/history.hpp"
#include "pshare/core/net.hpp"
#include "pshare/core/text.hpp"
#include "app/thread.hpp"
#include "ui_kit.hpp"
#include "thumbs.hpp"
#include "third_party/qrcodegen/qrcodegen.h"
#include <cctype>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#ifdef PSHARE_PS4
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>
#include <orbis/SystemService.h>
#include "libjbc.h"
#endif
extern "C" int pshare_service_main();
extern "C" void pshare_service_stop();
#ifndef PSHARE_BUILD
#define PSHARE_BUILD unknown
#endif
#define PSHARE_STR2(x) #x
#define PSHARE_STR(x) PSHARE_STR2(x)
namespace pshare {
#ifdef PSHARE_PS4
std::string upload_transport_report(); // ps4/upload_http.cpp
#else
std::string upload_transport_report() { return "host libcurl transport"; }
#endif
}
using namespace pshare;
namespace {
SDL_Renderer *renderer = nullptr;
constexpr const char *kVersionLabel = "PS4 1.0";

// Logical actions. The PS4 build reads raw joystick buttons because the
// OpenOrbis 0.5.4 SDL2 port reports a GUID with no game controller mapping, so
// SDL_GameController never opens there. Host builds keep GameController + keys.
enum Action { kNone=-1, kCross, kCircle, kSquare, kTriangle, kL1, kR1, kL2, kR2, kUp, kDown, kLeft, kRight,
              kTouch, kOptions, kRefresh, kQuit };
// OpenOrbis SDL2 PS4 joystick: 20 buttons, index -> ScePad bit (from the
// library's button table; matches the toolchain's SDL2 sample enum).
// The Share and PS buttons are reserved by the system and never reported.
[[maybe_unused]] Action ps4_button(int index) {
    switch (index) {
    case 0: return kCross;      case 1: return kCircle;    case 2: return kSquare;
    case 3: return kTriangle;   case 4: return kL1;        case 5: return kR1;
    case 9: return kOptions;
    case 13: return kUp;        case 14: return kDown;     case 15: return kLeft;
    case 16: return kRight;     case 17: return kTouch;
    case 18: return kL2;        case 19: return kR2;
    default: return kNone;
    }
}
[[maybe_unused]] Action controller_button(int button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return kCross;          case SDL_CONTROLLER_BUTTON_B: return kCircle;
    case SDL_CONTROLLER_BUTTON_X: return kSquare;         case SDL_CONTROLLER_BUTTON_Y: return kTriangle;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kL1;  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kR1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return kUp;       case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kLeft;   case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kRight;
    case SDL_CONTROLLER_BUTTON_BACK: return kTouch;       case SDL_CONTROLLER_BUTTON_START: return kOptions;
    default: return kNone;
    }
}

[[maybe_unused]] std::string hex_code(int code) { char b[16]; std::snprintf(b,sizeof b,"0x%08x",static_cast<unsigned>(code)); return b; }
std::string err_text(int code) {
    if (code == 0) return "ok";
    return std::to_string(code) + " (" + std::strerror(code) + ")";
}

// The PS4 firmware stores captures under per-game folders. When a folder
// carries no CUSA/NPXS ID (PS4 layouts vary), group by the first folder
// below the capture root so the game filter still works.
std::string game_key(const MediaItem &item, const Paths &paths) {
    if (!item.title_id.empty()) return item.title_id;
    for (const std::string *root : {&paths.photo_root, &paths.video_root}) {
        if (root->empty() || item.path.compare(0, root->size(), *root) != 0 || item.path.size() <= root->size() + 1) continue;
        const std::string rest = item.path.substr(root->size() + 1);
        const auto slash = rest.find('/');
        if (slash != std::string::npos) return "dir:" + rest.substr(0, slash);
    }
    return std::string();
}
std::string game_label(const std::string &key, TitleNames &names) {
    if (key.compare(0, 4, "dir:") == 0) return key.substr(4);
    return names.display(key);
}

#ifdef PSHARE_PS4
// GoldHEN provides kexec as syscall 11; libjbc needs it for every mount.
// Without it the syscall fails (SIGSYS is ignored for the probe) and the
// value passed in comes back unchanged.
asm(".text\n.globl pshare_kprobe\npshare_kprobe:\nmovabs $0x5053484152453432, %rax\nret\n");
extern "C" char pshare_kprobe[];
bool kernel_exec_available() {
    struct sigaction ignore{}, old{};
    ignore.sa_handler = SIG_IGN;
    sigaction(SIGSYS, &ignore, &old);
    const std::uint64_t got = jbc_krw_kcall(reinterpret_cast<std::uint64_t>(pshare_kprobe));
    sigaction(SIGSYS, &old, nullptr);
    return got == 0x5053484152453432ull;
}
// A previous run that crashed leaves its sandbox mount and folder behind,
// making mkdir fail with EEXIST. Unmount the stale one and retry once.
int mount_sandbox(const char *system, const char *name) {
    int rc = jbc_mount_in_sandbox(system, name);
    if (rc != 0) {
        jbc_unmount_in_sandbox(name);
        rc = jbc_mount_in_sandbox(system, name);
    }
    return rc;
}
#endif

struct Platform {
    std::string assets = "assets";
    bool user_mounted = false, data_mounted = false, usb_mounted[2] = {false,false};
    bool kexec = true;
    int user_rc = 0, data_rc = 0, usb_rc[2] = {0,0};
    std::vector<std::string> notes;
    std::string problem; // shown on screen when access is unavailable
    bool start(Paths &paths) {
#ifdef PSHARE_PS4
        assets = "/app0/assets";
        static const std::pair<const char *, OrbisSysModuleInternal> modules[] = {
            {"NET", ORBIS_SYSMODULE_INTERNAL_NET}, {"NETCTL", ORBIS_SYSMODULE_INTERNAL_NETCTL}, {"SYSTEM_SERVICE", ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE},
            {"SSL", ORBIS_SYSMODULE_INTERNAL_SSL}, {"HTTP", ORBIS_SYSMODULE_INTERNAL_HTTP}};
        for (const auto &module : modules)
            notes.push_back(std::string("sysmodule ") + module.first + ": " + hex_code(static_cast<int>(sceSysmoduleLoadModuleInternal(module.second))));
        notes.push_back("sceNetInit: " + hex_code(sceNetInit()));
        paths.photo_root = "/pshare-user/av_contents/photo";
        paths.video_root = "/pshare-user/av_contents/video";
        paths.appmeta_root = "/pshare-user/appmeta";
        paths.data_root = "/pshare-data/pshare-ps4";
        for (auto &usb : paths.usb_roots) usb.clear();
        kexec = kernel_exec_available();
        notes.push_back(std::string("GoldHEN kexec (syscall 11): ") + (kexec ? "available" : "NOT available"));
        if (!kexec) {
            problem = "GoldHEN kernel access is not available. Enable GoldHEN, then relaunch PShare.";
            return false;
        }
        // Mount only the required trees inside the app sandbox; keep /app0 accessible.
        user_rc = mount_sandbox("/user", "pshare-user"); user_mounted = user_rc == 0;
        data_rc = mount_sandbox("/data", "pshare-data"); data_mounted = data_rc == 0;
        for (int n = 0; n < 2; ++n) {
            const std::string name = "pshare-usb" + std::to_string(n);
            const std::string system = "/mnt/usb" + std::to_string(n);
            // /mnt/usbN is outside the sandbox, so it can't be stat'ed first;
            // the mount simply fails when no drive is connected.
            usb_rc[n] = mount_sandbox(system.c_str(), name.c_str());
            usb_mounted[n] = usb_rc[n] == 0;
            if (usb_mounted[n]) paths.usb_roots[n] = "/" + name;
        }
        notes.push_back("mount /user -> /pshare-user: " + err_text(user_rc));
        notes.push_back("mount /data -> /pshare-data: " + err_text(data_rc));
        for (int n = 0; n < 2; ++n)
            notes.push_back("mount /mnt/usb" + std::to_string(n) + " -> /pshare-usb" + std::to_string(n) + ": " +
                            (usb_mounted[n] ? std::string("ok") : "not mounted (" + err_text(usb_rc[n]) + ")"));
        // Don't start a writable service if persistence isn't available.
        if (!user_mounted || !data_mounted) {
            problem = "Folder mount failed: /user " + err_text(user_rc) + ", /data " + err_text(data_rc);
            return false;
        }
#else
        paths.data_root = "build/host-data";
        paths.photo_root = "build/host-captures/photo";
        paths.video_root = "build/host-captures/video";
        paths.appmeta_root = "build/host-captures/appmeta";
        if (const char *v = std::getenv("PSHARE_ASSETS")) assets = v;
        if (const char *v = std::getenv("PSHARE_DATA")) paths.data_root = v;
        if (const char *v = std::getenv("PSHARE_PHOTO")) paths.photo_root = v;
        if (const char *v = std::getenv("PSHARE_VIDEO")) paths.video_root = v;
        if (const char *v = std::getenv("PSHARE_APPMETA")) paths.appmeta_root = v;
        ensure_directory(paths.photo_root); ensure_directory(paths.video_root);
        notes.push_back("host build: no sandbox mounts");
#endif
        setenv("PSHARE_DATA", paths.data_root.c_str(),1);
        setenv("PSHARE_PHOTO", paths.photo_root.c_str(),1);
        setenv("PSHARE_VIDEO", paths.video_root.c_str(),1);
        setenv("PSHARE_APPMETA", paths.appmeta_root.c_str(),1);
        for (int n=0;n<4;++n) setenv(("PSHARE_USB"+std::to_string(n)).c_str(),paths.usb_roots[n].c_str(),1);
        if (!ensure_directory(paths.data_root)) {
            problem = "Cannot create " + paths.data_root + ": " + err_text(errno);
            notes.push_back(problem);
            return false;
        }
        return true;
    }
    ~Platform() {
#ifdef PSHARE_PS4
        for (int n = 1; n >= 0; --n)
            if (usb_mounted[n]) jbc_unmount_in_sandbox(("pshare-usb" + std::to_string(n)).c_str());
        if (data_mounted) jbc_unmount_in_sandbox("pshare-data");
        if (user_mounted) jbc_unmount_in_sandbox("pshare-user");
#endif
    }
};

// Folder/file names under a capture root, a few levels deep, so a first
// console run reveals the firmware's real layout. Names only; no contents.
void outline(const std::string &folder, int depth, std::string &out, int &lines) {
    if (depth > 3 || lines >= 120) return;
    DIR *dir = opendir(folder.c_str());
    if (!dir) { out += std::string(depth * 2, ' ') + "(cannot open: " + err_text(errno) + ")\n"; ++lines; return; }
    std::vector<std::string> folders, files;
    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat info{};
        const std::string path = folder + "/" + name;
        if (stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) folders.push_back(name); else files.push_back(name);
    }
    closedir(dir);
    std::sort(folders.begin(), folders.end()); std::sort(files.begin(), files.end());
    for (const auto &name : folders) {
        if (lines >= 120) break;
        out += std::string(depth * 2, ' ') + name + "/\n"; ++lines;
        outline(folder + "/" + name, depth + 1, out, lines);
    }
    const std::size_t shown = std::min<std::size_t>(files.size(), 3);
    for (std::size_t i = 0; i < shown && lines < 120; ++i) { out += std::string(depth * 2, ' ') + files[i] + "\n"; ++lines; }
    if (files.size() > shown && lines < 120) { out += std::string(depth * 2, ' ') + "... " + std::to_string(files.size() - shown) + " more files\n"; ++lines; }
}

int hide_system_splash();
std::string diagnostics(const Platform &platform, const Paths &paths, bool access, const ScanResult &scan,
                        SDL_Window *window, SDL_Joystick *pad, int last_raw_button) {
    std::string out = "PShare PS4 diagnostics\nbuild: " PSHARE_STR(PSHARE_BUILD) "\n";
    char when[64]; const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    out += std::string("time: ") + when + "\nuid: " + std::to_string(getuid()) + "  gid: " + std::to_string(getgid()) + "\n";
    out += std::string("access: ") + (access ? "ok" : "UNAVAILABLE - " + platform.problem) + "\n";
    for (const auto &note : platform.notes) out += note + "\n";
    out += "data root: " + paths.data_root + "\nphoto root: " + paths.photo_root + "\nvideo root: " + paths.video_root + "\n";
    out += "scan: " + std::to_string(scan.items.size()) + " captures, " + std::to_string(scan.folders) + " folders, photo root " +
           (scan.photo_root ? "readable" : "unreadable") + ", video root " + (scan.video_root ? "readable" : "unreadable") +
           ", first error " + err_text(scan.open_errno) + "\n";
    {
        // File sizes must match what can actually be read (0.2 read st_blocks as the size).
        const std::string probe = platform.assets + "/fonts/Inter-Regular.ttf";
        struct stat info{}; std::string bytes;
        const bool stat_ok = stat(probe.c_str(), &info) == 0;
        const bool read_ok = read_text_file(probe, &bytes, 16u << 20);
        out += "stat check: size " + (stat_ok ? std::to_string(static_cast<long long>(info.st_size)) : std::string("error")) +
               ", read " + (read_ok ? std::to_string(bytes.size()) : std::string("error")) +
               (stat_ok && read_ok && static_cast<std::uint64_t>(info.st_size) == bytes.size() ? " - ok" : " - MISMATCH") + "\n";
    }
    out += "hide system splash: " + hex_code(hide_system_splash()) + "\n";
    out += "local IP: " + local_ip_address() + "\n";
    out += "HTTP transport: " + upload_transport_report() + "\n";
    SDL_version linked; SDL_GetVersion(&linked);
    out += "SDL " + std::to_string(linked.major) + "." + std::to_string(linked.minor) + "." + std::to_string(linked.patch) + "\n";
    SDL_RendererInfo info{};
    if (renderer && SDL_GetRendererInfo(renderer, &info) == 0)
        out += std::string("renderer: ") + (info.name ? info.name : "?") + ((info.flags & SDL_RENDERER_ACCELERATED) ? " (accelerated)" : " (software)") + "\n";
    if (window) { int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h); out += "window: " + std::to_string(w) + "x" + std::to_string(h) + "\n"; }
    out += "joysticks: " + std::to_string(SDL_NumJoysticks()) + "\n";
    if (pad) {
        out += std::string("pad: ") + (SDL_JoystickName(pad) ? SDL_JoystickName(pad) : "?") + ", buttons " + std::to_string(SDL_JoystickNumButtons(pad)) +
               ", axes " + std::to_string(SDL_JoystickNumAxes(pad)) + ", hats " + std::to_string(SDL_JoystickNumHats(pad)) + "\n";
        char guid[64]; SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(pad), guid, sizeof guid);
        out += std::string("pad GUID: ") + guid + "\n";
    }
    out += "last raw button: " + std::to_string(last_raw_button) + "\n";
    if (access) {
        out += "\ncapture tree (" + paths.photo_root + "):\n"; int lines = 0; outline(paths.photo_root, 1, out, lines);
        out += "\ncapture tree (" + paths.video_root + "):\n"; lines = 0; outline(paths.video_root, 1, out, lines);
        const std::string av = paths.photo_root.substr(0, paths.photo_root.rfind('/'));
        out += "\n" + av + ":\n"; lines = 0; outline(av, 1, out, lines);
        out += "\nfirst captures:\n";
        for (std::size_t i = 0; i < scan.items.size() && i < 20; ++i)
            out += "  " + scan.items[i].path + "  [" + (scan.items[i].title_id.empty() ? "no title ID" : scan.items[i].title_id) + ", " +
                   std::to_string(scan.items[i].size) + " bytes]\n";
    }
    return out;
}
// Written to /data/pshare-ps4 and, when a USB drive is mounted, its root, so
// the file can be read on a PC. Returns where it went.
std::string save_diagnostics(const std::string &text, const Paths &paths, bool access) {
    std::string where;
    if (access && write_text_file_atomic(paths.data_root + "/diagnostics.txt", text)) where = paths.data_root + "/diagnostics.txt";
    for (int n = 0; n < 2; ++n) {
        if (paths.usb_roots[n].empty()) continue;
        FILE *file = std::fopen((paths.usb_roots[n] + "/PShare-PS4-diagnostics.txt").c_str(), "wb");
        if (!file) continue;
        const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
        if (std::fclose(file) == 0 && ok) where += std::string(where.empty() ? "" : " and ") + "USB" + std::to_string(n);
    }
    return where;
}


// The system's launch screen (sce_sys/pic1.png) stays on top of the app until
// the app hides it; the PS5 build does this after its first frame too.
int hide_system_splash() {
#ifdef PSHARE_PS4
    static int rc = 1;
    if (rc == 1) rc = sceSystemServiceHideSplashScreen();
    return rc;
#else
    return 0;
#endif
}

// ---- the app ---------------------------------------------------------------

using ps4ui::Color;
using ps4ui::Face;
using ps4ui::hex;
namespace pal = ps4ui::pal;

const Color kCream = hex(pal::cream), kMist = hex(pal::mist), kCoral = hex(pal::coral), kAqua = hex(pal::aqua),
            kSun = hex(pal::sun), kRed = hex(pal::red), kInk = hex(pal::ink);

Color destination_color(DestinationType type) {
    switch (type) {
    case DestinationType::discord: return hex(0x7c8cff);
    case DestinationType::telegram: return kAqua;
    case DestinationType::catbox: return kSun;
    case DestinationType::litterbox: return hex(0xffa45e);
    case DestinationType::folder: return hex(0x7fe0a8);
    case DestinationType::smb: return hex(0x8fd3ff);
    default: return hex(0xc9b8ff);
    }
}

std::string two(int v) { char b[8]; std::snprintf(b, sizeof b, "%02d", v); return b; }
std::string capture_date(const CaptureTime &t) {
    return std::to_string(t.year) + "-" + two(t.month) + "-" + two(t.day) + "  " + two(t.hour) + ":" + two(t.minute);
}
std::string when_text(std::int64_t seconds) {
    const LocalTime then = local_time(seconds), now = local_time(std::time(nullptr));
    const std::string clock = two(then.hour) + ":" + two(then.minute);
    if (then.year == now.year && then.yday == now.yday) return "Today " + clock;
    if ((then.year == now.year && then.yday + 1 == now.yday) ||
        (then.year + 1 == now.year && now.yday == 0 && then.month == 12 && then.day == 31)) return "Yesterday " + clock;
    return std::to_string(then.year) + "-" + two(then.month) + "-" + two(then.day) + " " + clock;
}
std::string masked_target(const Destination &d) {
    std::string text;
    switch (d.type) {
    case DestinationType::folder: return d.path.empty() || iequals(d.path, "usb") ? "First USB drive" : d.path;
    case DestinationType::telegram: return d.chat_id.empty() ? "Chat not set" : "Chat " + d.chat_id;
    case DestinationType::catbox: return d.token.empty() ? "catbox.moe (anonymous)" : "catbox.moe (your account)";
    case DestinationType::litterbox: return "litterbox.catbox.moe, " + d.litterbox_time;
    default: text = d.url; break;
    }
    if (text.empty()) return "Not set";
    const auto scheme = text.find("://");
    if (scheme != std::string::npos) text = text.substr(scheme + 3);
    const auto at = text.find('@');
    if (at != std::string::npos && at < text.find('/')) text = text.substr(at + 1); // no passwords on screen
    // Webhook tokens stay off the TV.
    return text.size() > 30 ? text.substr(0, 30) + "\xE2\x80\xA6" : text;
}

struct Toast { std::string title, sub; bool ok = true; std::uint32_t until = 0; };

enum class Overlay { none, details, picker, qr, message, fullscreen, menu };

struct RowSpec {
    enum Kind { header, toggle, action, info, link } kind = info;
    std::string label, value, sub;
    bool on = false;
    std::function<void()> act;
};

class Ui {
public:
    Ui(Platform &platform, Paths &paths, bool access, SDL_Window *window)
        : platform_(platform), paths_(paths), access_(access), window_(window), paint_(renderer),
          names_(paths.appmeta_root, {paths.photo_root, paths.video_root}), history_(paths.history()) {}
    bool init();
    void run(bool smoke, const char *shots_dir);
    ~Ui();
private:
    // state
    void refresh(bool rescan);
    void load_config();
    bool save_config(const std::function<void(Config &)> &change, const std::string &done);
    void poll_status(std::uint32_t now);
    void handle(Action a);
    void open_qr(const std::string &title, const std::string &url, const std::string &caption);
    void open_message(const std::string &title, const std::string &text);
    void open_details();
    void send(const std::string &path, const std::string &destination);
    void toast(const std::string &title, const std::string &sub, bool ok);
    std::string browser() const { return ip_.empty() ? std::string() : "http://" + ip_ + ":9078"; }
    std::vector<const Destination *> usable_destinations() const;
    std::vector<RowSpec> auto_rows();
    std::vector<RowSpec> destination_rows(const Destination &d);
    std::vector<RowSpec> settings_rows();
    static int next_selectable(const std::vector<RowSpec> &rows, int from, int step);
    // drawing
    void render();
    int text(Face f, const std::string &s, int x, int y, int size, Color c) { return fonts_.draw(renderer, f, s, x, y, size, c); }
    int text_right(Face f, const std::string &s, int right, int y, int size, Color c) {
        const int w = fonts_.measure(f, s, size); fonts_.draw(renderer, f, s, right - w, y, size, c); return w;
    }
    void draw_header();
    void draw_sidebar();
    void draw_footer();
    int hint(int x, const std::string &glyph, const std::string &label, bool right_align = false);
    void draw_captures();
    void draw_tile(const MediaItem &item, int x, int y, bool selected);
    void draw_auto();
    void draw_destinations();
    void draw_history();
    void draw_settings();
    void draw_rows(const std::vector<RowSpec> &rows, int x, int y, int w, int h, int selected, bool focused);
    void draw_toggle(int x, int y, bool on);
    void draw_details();
    void draw_picker();
    void draw_qr();
    void draw_message();
    void draw_menu();
    void draw_fullscreen();
    void draw_toasts();
    void draw_upload_card();
    void draw_qr_code(const std::string &url, int x, int y, int side);
    void dim(float alpha) { paint_.fill(0, 0, 1920, 1080, hex(0x07051a, alpha)); }

    Platform &platform_;
    Paths &paths_;
    bool access_;
    SDL_Window *window_;
    ps4ui::Painter paint_;
    ps4ui::Fonts fonts_;
    ps4ui::Thumbs thumbs_;
    TitleNames names_;
    History history_;
    Config config_;
    IniDocument doc_;
    ScanResult scan_;
    std::vector<MediaItem> captures_;
    std::map<std::string, bool> last_outcome_; // path -> last attempt ok
    DaemonStatus status_;
    std::string ip_;
    app::Thread service_;
    std::atomic<bool> service_done_{false};
    bool started_ = false;
    SDL_Joystick *pad_ = nullptr;
    SDL_GameController *controller_ = nullptr;
    int last_raw_ = -1;
    SDL_Texture *logo_ = nullptr, *empty_art_ = nullptr;
    int empty_w_ = 0, empty_h_ = 0;
    // navigation
    int section_ = 0; // captures, auto, destinations, history, settings
    int kind_ = 0;    // all, screenshots, clips
    std::string game_;
    bool oldest_first_ = false;
    int grid_sel_ = 0;
    float scroll_ = 0, scroll_target_ = 0;
    int auto_sel_ = -1, dest_sel_ = 0, dest_col_ = 0, dest_row_ = -1, hist_sel_ = 0, settings_sel_ = -1;
    Overlay overlay_ = Overlay::none;
    int overlay_sel_ = 0;
    float panel_x_ = 1920;
    SDL_Texture *preview_ = nullptr;
    int preview_w_ = 0, preview_h_ = 0;
    std::string qr_title_, qr_url_, qr_caption_, msg_title_, msg_text_;
    Overlay qr_return_ = Overlay::none;
    std::vector<Toast> toasts_;
    std::size_t history_count_ = 0;
    std::int64_t opened_at_ = 0;
    bool quit_ = false, dirty_ = true;
    std::uint32_t next_status_ = 0, next_scan_ = 0, next_forced_ = 0;
    std::string status_key_;
    bool profile_ = std::getenv("PSHARE_PROFILE") != nullptr;
};

Ui::~Ui() {
    if (preview_) SDL_DestroyTexture(preview_);
    if (logo_) SDL_DestroyTexture(logo_);
    if (empty_art_) SDL_DestroyTexture(empty_art_);
    thumbs_.stop();
    if (started_) { pshare_service_stop(); service_.join(); }
    if (controller_) SDL_GameControllerClose(controller_);
    if (pad_) SDL_JoystickClose(pad_);
}

bool Ui::init() {
    const std::string fonts = platform_.assets + "/fonts/";
    if (!fonts_.load(Face::regular, fonts + "Inter-Regular.ttf")) return false;
    fonts_.load(Face::semibold, fonts + "Inter-SemiBold.ttf");
    fonts_.load(Face::display, fonts + "Montserrat-Medium.ttf");
    fonts_.load(Face::mono, fonts + "DejaVuSansMono.ttf");
    ps4ui::Image image;
    if (ps4ui::decode_file(platform_.assets + "/art/backdrop.jpg", &image)) paint_.set_backdrop(image);
    if (ps4ui::decode_file(platform_.assets + "/art/logo.png", &image)) logo_ = ps4ui::upload(renderer, ps4ui::resize(image, 72, 72));
    if (ps4ui::decode_file(platform_.assets + "/art/empty.png", &image)) {
        empty_w_ = 320; empty_h_ = image.h * 320 / std::max(1, image.w);
        empty_art_ = ps4ui::upload(renderer, ps4ui::resize(image, empty_w_, empty_h_));
    }
    opened_at_ = std::time(nullptr);
    started_ = access_ && service_.start([this] { pshare_service_main(); service_done_ = true; }, 4u << 20);
#ifdef PSHARE_PS4
    if (SDL_NumJoysticks() > 0) pad_ = SDL_JoystickOpen(0);
#else
    for (int i = 0; !controller_ && i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) controller_ = SDL_GameControllerOpen(i);
#endif
    thumbs_.start();
    load_config();
    refresh(true);
    history_count_ = history_.entries().size();
    ip_ = local_ip_address();
    // Release: diagnostics are written only when asked (Settings or Options menu),
    // except when access fails, so there is something to send for support.
    if (!access_) save_diagnostics(diagnostics(platform_, paths_, access_, scan_, window_, pad_, last_raw_), paths_, access_);
    if (!access_) toast("Capture access unavailable", platform_.problem, false);
    return true;
}

void Ui::load_config() {
    if (!access_) return;
    std::vector<std::string> warnings;
    doc_ = IniDocument();
    config_ = Config();
    load_config_file(paths_.config(), &doc_, &config_, &warnings);
}

bool Ui::save_config(const std::function<void(Config &)> &change, const std::string &done) {
    if (!access_) return false;
    load_config();
    change(config_);
    store_config(config_, &doc_);
    if (!write_text_file_atomic(paths_.config(), doc_.write())) { toast("Couldn't save settings", paths_.config(), false); return false; }
    daemon_command("RELOAD", nullptr);
    if (!done.empty()) toast(done, "", true);
    return true;
}

void Ui::refresh(bool rescan) {
    if (rescan && access_) scan_ = scan_media(paths_, {});
    history_.refresh();
    last_outcome_.clear();
    for (const auto &entry : history_.entries()) last_outcome_[entry.path] = entry.ok;
    captures_.clear();
    for (const auto &item : scan_.items)
        if ((kind_ == 0 || (kind_ == 1 && !item.video()) || (kind_ == 2 && item.video())) && (game_.empty() || game_ == game_key(item, paths_)))
            captures_.push_back(item);
    if (oldest_first_) std::reverse(captures_.begin(), captures_.end());
    grid_sel_ = std::clamp(grid_sel_, 0, std::max(0, static_cast<int>(captures_.size()) - 1));
    // A new history line written by the uploader becomes a toast.
    const auto &entries = history_.entries();
    if (entries.size() > history_count_ && !entries.empty()) {
        const HistoryEntry &e = entries.back();
        if (e.time >= opened_at_ - 5) {
            const std::string kind = ends_with_ci(e.path, ".mp4") || ends_with_ci(e.path, ".webm") ? "Clip" : "Screenshot";
            const Destination *d = config_.find(e.destination);
            const std::string where = d ? d->display_name() : e.destination;
            toast(e.ok ? kind + " sent to " + where : kind + " not sent to " + where, e.detail, e.ok);
        }
    }
    history_count_ = entries.size();
    dirty_ = true;
}

void Ui::poll_status(std::uint32_t now) {
    if (now < next_status_) return;
    next_status_ = now + 1000;
    if (started_) status_ = daemon_status(0, true);
    if (!status_.ip.empty()) ip_ = status_.ip;
    else if (ip_.empty()) ip_ = local_ip_address();
    const std::string key = status_.state + "|" + std::to_string(status_.uploaded) + "|" + std::to_string(status_.failed) + "|" +
                            std::to_string(status_.queued) + "|" + std::to_string(status_.sent) + "|" + status_.last_link + "|" + ip_;
    if (key != status_key_) {
        // Something was sent or failed: history and badges change too.
        if (!status_key_.empty() && (status_.uploaded + status_.failed) > 0) refresh(false);
        status_key_ = key;
        dirty_ = true;
    }
}

void Ui::toast(const std::string &title, const std::string &sub, bool ok) {
    toasts_.push_back(Toast{title, sub, ok, SDL_GetTicks() + 4500});
    if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
    dirty_ = true;
}

std::vector<const Destination *> Ui::usable_destinations() const {
    std::vector<const Destination *> out;
    for (const auto &d : config_.destinations) if (d.enabled && d.configured()) out.push_back(&d);
    return out;
}

void Ui::send(const std::string &path, const std::string &destination) {
    std::string answer;
    if (!started_) { toast("The uploader isn't running", platform_.problem, false); return; }
    if (daemon_command("QUEUE " + destination + "\t" + path, &answer, 1000)) {
        const std::string reply(trim(answer));
        if (starts_with(reply, "ok")) {
            const Destination *d = destination.empty() ? config_.destination_for(title_id_from_path(path)) : config_.find(destination);
            toast("Sending to " + (d ? d->display_name() : std::string("your destination")), "The History tab shows the result", true);
        } else toast("Couldn't queue the upload", reply, false);
    } else toast("Uploader unavailable", "Try again in a moment", false);
}

void Ui::open_qr(const std::string &title, const std::string &url, const std::string &caption) {
    if (url.empty()) { toast("No network address yet", "Connect the PS4 to your network", false); return; }
    qr_return_ = overlay_;
    qr_title_ = title; qr_url_ = url; qr_caption_ = caption;
    overlay_ = Overlay::qr;
}
void Ui::open_message(const std::string &title, const std::string &body) {
    msg_title_ = title; msg_text_ = body; overlay_ = Overlay::message;
}

void Ui::open_details() {
    if (captures_.empty()) return;
    if (preview_) { SDL_DestroyTexture(preview_); preview_ = nullptr; }
    const MediaItem &item = captures_[grid_sel_];
    if (!item.video()) {
        ps4ui::Image image;
        if (ps4ui::decode_file(item.path, &image)) {
            ps4ui::Image small = ps4ui::resize(image, 1280, std::max(1, image.h * 1280 / std::max(1, image.w)));
            preview_ = ps4ui::upload(renderer, small, false);
            preview_w_ = small.w; preview_h_ = small.h;
        }
    }
    overlay_ = Overlay::details;
    overlay_sel_ = 0;
    panel_x_ = 1920;
}

int Ui::next_selectable(const std::vector<RowSpec> &rows, int from, int step) {
    for (int i = from + step; i >= 0 && i < static_cast<int>(rows.size()); i += step)
        if (rows[i].kind == RowSpec::toggle || rows[i].kind == RowSpec::action || rows[i].kind == RowSpec::link) return i;
    return from;
}

std::vector<RowSpec> Ui::auto_rows() {
    std::vector<RowSpec> rows;
    const bool paused = status_.state == "paused";
    rows.push_back({RowSpec::header, "UPLOADER"});
    rows.push_back({RowSpec::action, paused ? "Resume" : "Pause", "", "", false, [this, paused] {
        if (!started_) { toast("The uploader isn't running", "", false); return; }
        daemon_command(paused ? "RESUME" : "PAUSE", nullptr); next_status_ = 0; }});
    rows.push_back({RowSpec::toggle, "Send automatically", "", "", config_.auto_upload, [this] {
        const bool on = !config_.auto_upload; save_config([on](Config &c) { c.auto_upload = on; }, on ? "Automatic uploads on" : "Automatic uploads off"); }});
    rows.push_back({RowSpec::toggle, "Send screenshots", "", "", config_.upload_screenshots, [this] {
        const bool on = !config_.upload_screenshots; save_config([on](Config &c) { c.upload_screenshots = on; }, ""); }});
    rows.push_back({RowSpec::toggle, "Send clips", "", "", config_.upload_videos, [this] {
        const bool on = !config_.upload_videos; save_config([on](Config &c) { c.upload_videos = on; }, ""); }});
    rows.push_back({RowSpec::toggle, "Notifications", "", "", config_.notify, [this] {
        const bool on = !config_.notify; save_config([on](Config &c) { c.notify = on; }, ""); }});
    rows.push_back({RowSpec::header, "PHONE AND PC"});
    rows.push_back({RowSpec::link, "Set up from your phone", "", "", false, [this] {
        open_qr("Set up from your phone", browser(), "Scan it with your phone's camera. Add webhooks, tokens and folders there."); }});
    rows.push_back({RowSpec::link, "Captures on your phone", "", "", false, [this] {
        open_qr("Captures on your phone", browser().empty() ? std::string() : browser() + "/gallery", "Browse, play and download your captures on the same Wi-Fi."); }});
    rows.push_back({RowSpec::header, "ABOUT"});
    rows.push_back({RowSpec::info, "pshared", (status_.version.empty() ? std::string("1.0.0") : status_.version) + " on port 9077"});
    rows.push_back({RowSpec::info, "This build", std::string(kVersionLabel)});
    return rows;
}

std::vector<RowSpec> Ui::destination_rows(const Destination &d) {
    std::vector<RowSpec> rows;
    std::string header = type_label(d.type);
    for (auto &c : header) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    rows.push_back({RowSpec::header, header});
    const std::string id = d.id;
    rows.push_back({RowSpec::toggle, "Enabled", "", "", d.enabled, [this, id] {
        Destination *found = config_.find(id); if (!found) return;
        const bool on = !found->enabled;
        save_config([id, on](Config &c) { if (Destination *x = c.find(id)) x->enabled = on; }, on ? "Destination on" : "Destination off"); }});
    rows.push_back({RowSpec::toggle, "Use by default", "", "", config_.default_destination == d.id, [this, id] {
        save_config([id](Config &c) { c.default_destination = id; }, "Default destination changed"); }});
    rows.push_back({RowSpec::info, "Name", d.display_name()});
    rows.push_back({RowSpec::info, d.type == DestinationType::folder ? "Folder" : "Address", masked_target(d)});
    const std::uint64_t limit = d.size_limit();
    rows.push_back({RowSpec::info, "Size limit", limit ? human_size(limit) : std::string("None")});
    if (!d.fallback.empty()) { const Destination *f = config_.find(d.fallback); rows.push_back({RowSpec::info, "Big files go to", f ? f->display_name() : d.fallback}); }
    rows.push_back({RowSpec::link, "Edit on your phone", "", "Typing a webhook with the controller is slow; paste it on your phone instead.", false, [this] {
        open_qr("Edit on your phone", browser(), "Scan it with your phone's camera to change names, webhooks and tokens."); }});
    return rows;
}

std::vector<RowSpec> Ui::settings_rows() {
    std::vector<RowSpec> rows;
    rows.push_back({RowSpec::header, "CONSOLE"});
    rows.push_back({RowSpec::info, "Address", ip_.empty() ? std::string("Not connected") : ip_});
    rows.push_back({RowSpec::info, "Captures found", std::to_string(scan_.items.size())});
    rows.push_back({RowSpec::info, "Uploader", started_ ? (status_.reachable ? status_.state : std::string("starting")) : std::string("off")});
    rows.push_back({RowSpec::action, "Refresh captures", "", "", false, [this] { refresh(true); toast("Captures refreshed", std::to_string(scan_.items.size()) + " found", true); }});
    rows.push_back({RowSpec::action, "Save diagnostics", "", "", false, [this] {
        const std::string where = save_diagnostics(diagnostics(platform_, paths_, access_, scan_, window_, pad_, last_raw_), paths_, access_);
        toast(where.empty() ? "Couldn't save diagnostics" : "Diagnostics saved", where.empty() ? "Connect a USB drive" : "to " + where, !where.empty()); }});
    rows.push_back({RowSpec::header, "ON PS4"});
    rows.push_back({RowSpec::info, "Uploads", "While PShare is open"});
    rows.push_back({RowSpec::info, "Clips", "Play on your phone"});
    rows.push_back({RowSpec::info, "SMB shares", "Not yet"});
    rows.push_back({RowSpec::header, "ABOUT"});
    rows.push_back({RowSpec::info, "Version", std::string(kVersionLabel) + " (" PSHARE_STR(PSHARE_BUILD) ")"});
    rows.push_back({RowSpec::info, "PShare", "by StonedModder"});
    return rows;
}

void Ui::handle(Action a) {
    if (a == kNone) return;
    dirty_ = true;
    if (a == kQuit) { quit_ = true; return; }
    if (a == kRefresh) { refresh(true); return; }
    // Overlays first.
    switch (overlay_) {
    case Overlay::fullscreen:
        if (a == kCircle || a == kCross) overlay_ = Overlay::details;
        return;
    case Overlay::qr:
        if (a == kCircle || a == kCross) overlay_ = qr_return_;
        return;
    case Overlay::message:
        if (a == kCircle || a == kCross) overlay_ = Overlay::none;
        return;
    case Overlay::menu: {
        if (a == kCircle || a == kOptions) { overlay_ = Overlay::none; return; }
        if (a == kUp) overlay_sel_ = std::max(0, overlay_sel_ - 1);
        if (a == kDown) overlay_sel_ = std::min(2, overlay_sel_ + 1);
        if (a == kCross) {
            overlay_ = Overlay::none;
            if (overlay_sel_ == 0) { refresh(true); toast("Captures refreshed", std::to_string(scan_.items.size()) + " found", true); }
            if (overlay_sel_ == 1) { for (auto &row : settings_rows()) if (row.label == "Save diagnostics" && row.act) row.act(); }
            if (overlay_sel_ == 2) quit_ = true;
        }
        return;
    }
    case Overlay::picker: {
        const auto list = usable_destinations();
        if (a == kCircle) { overlay_ = captures_.empty() ? Overlay::none : Overlay::details; overlay_sel_ = 1; return; }
        if (a == kUp) overlay_sel_ = std::max(0, overlay_sel_ - 1);
        if (a == kDown) overlay_sel_ = std::min(static_cast<int>(list.size()) - 1, overlay_sel_ + 1);
        if (a == kCross && !list.empty() && !captures_.empty()) {
            send(captures_[grid_sel_].path, list[std::clamp(overlay_sel_, 0, static_cast<int>(list.size()) - 1)]->id);
            overlay_ = Overlay::none;
        }
        return;
    }
    case Overlay::details: {
        if (captures_.empty()) { overlay_ = Overlay::none; return; }
        const MediaItem &item = captures_[grid_sel_];
        if (a == kCircle) { overlay_ = Overlay::none; return; }
        if (a == kUp) overlay_sel_ = std::max(0, overlay_sel_ - 1);
        if (a == kDown) overlay_sel_ = std::min(3, overlay_sel_ + 1);
        if (a == kSquare) { send(item.path, ""); overlay_ = Overlay::none; }
        if (a == kTriangle) { overlay_ = Overlay::picker; overlay_sel_ = 0; }
        if (a == kCross) {
            const std::string view = browser().empty() ? std::string() : browser() + "/view?p=" + url_encode(item.path);
            switch (overlay_sel_) {
            case 0:
                if (usable_destinations().empty()) open_qr("Set up a destination", browser(), "Scan it with your phone's camera to add Discord, Telegram, Catbox or a USB folder.");
                else { send(item.path, ""); overlay_ = Overlay::none; }
                break;
            case 1: overlay_ = Overlay::picker; overlay_sel_ = 0; break;
            case 2:
                if (!item.video() && preview_) overlay_ = Overlay::fullscreen;
                else open_qr(item.video() ? "Play on your phone" : names_.display(item.title_id), view, "Scan it with your phone's camera to open this capture.");
                break;
            case 3: open_qr("Open in a browser", view, "On your phone or PC, on the same network as the PS4."); break;
            }
        }
        return;
    }
    case Overlay::none: break;
    }
    if (a == kOptions) { overlay_ = Overlay::menu; overlay_sel_ = 0; return; }
    if (a == kL1 || a == kR1) { section_ = (section_ + (a == kL1 ? 4 : 1)) % 5; return; }
    switch (section_) {
    case 0: {
        const int n = static_cast<int>(captures_.size());
        if (a == kLeft) grid_sel_ = std::max(0, grid_sel_ - 1);
        if (a == kRight) grid_sel_ = std::min(std::max(0, n - 1), grid_sel_ + 1);
        if (a == kUp && grid_sel_ >= 4) grid_sel_ -= 4;
        if (a == kDown && grid_sel_ + 4 < n) grid_sel_ += 4;
        else if (a == kDown && grid_sel_ / 4 < (n - 1) / 4) grid_sel_ = n - 1;
        if (a == kL2 || a == kR2) { kind_ = (kind_ + (a == kL2 ? 2 : 1)) % 3; grid_sel_ = 0; scroll_target_ = 0; refresh(false); }
        if (a == kTouch) {
            // Cycles all games, then each game; a full cycle also flips the order.
            std::vector<std::string> games{std::string()};
            for (const auto &item : scan_.items) { const std::string k = game_key(item, paths_); if (std::find(games.begin(), games.end(), k) == games.end()) games.push_back(k); }
            auto it = std::find(games.begin(), games.end(), game_);
            std::size_t at = it == games.end() ? 0 : static_cast<std::size_t>(it - games.begin());
            if (at + 1 >= games.size()) { oldest_first_ = !oldest_first_; at = 0; } else ++at;
            game_ = games[at]; grid_sel_ = 0; scroll_target_ = 0; refresh(false);
        }
        if (n == 0) return;
        if (a == kCross) open_details();
        if (a == kSquare) send(captures_[grid_sel_].path, "");
        if (a == kTriangle) { overlay_ = Overlay::picker; overlay_sel_ = 0; }
        return;
    }
    case 1: {
        auto rows = auto_rows();
        if (auto_sel_ < 0) auto_sel_ = next_selectable(rows, -1, 1);
        if (a == kUp) auto_sel_ = next_selectable(rows, auto_sel_, -1);
        if (a == kDown) auto_sel_ = next_selectable(rows, auto_sel_, 1);
        if (a == kCross && auto_sel_ >= 0 && auto_sel_ < static_cast<int>(rows.size()) && rows[auto_sel_].act) rows[auto_sel_].act();
        return;
    }
    case 2: {
        const int n = static_cast<int>(config_.destinations.size());
        if (n == 0) { if (a == kCross) open_qr("Set up from your phone", browser(), "Scan it with your phone's camera to add a destination."); return; }
        dest_sel_ = std::clamp(dest_sel_, 0, n - 1);
        if (a == kRight && dest_col_ == 0) { dest_col_ = 1; dest_row_ = -1; }
        else if ((a == kLeft || a == kCircle) && dest_col_ == 1) dest_col_ = 0;
        auto rows = destination_rows(config_.destinations[dest_sel_]);
        if (dest_col_ == 0) {
            if (a == kUp) dest_sel_ = std::max(0, dest_sel_ - 1);
            if (a == kDown) dest_sel_ = std::min(n - 1, dest_sel_ + 1);
            if (a == kCross) { dest_col_ = 1; dest_row_ = -1; }
        } else {
            if (dest_row_ < 0) dest_row_ = next_selectable(rows, -1, 1);
            if (a == kUp) dest_row_ = next_selectable(rows, dest_row_, -1);
            if (a == kDown) dest_row_ = next_selectable(rows, dest_row_, 1);
            if (a == kCross && dest_row_ >= 0 && dest_row_ < static_cast<int>(rows.size()) && rows[dest_row_].act) rows[dest_row_].act();
        }
        return;
    }
    case 3: {
        const auto &entries = history_.entries();
        const int n = static_cast<int>(entries.size());
        if (n == 0) return;
        if (a == kUp) hist_sel_ = std::max(0, hist_sel_ - 1);
        if (a == kDown) hist_sel_ = std::min(n - 1, hist_sel_ + 1);
        hist_sel_ = std::clamp(hist_sel_, 0, n - 1);
        const HistoryEntry &e = entries[n - 1 - hist_sel_];
        const std::string game = names_.display(title_id_from_path(e.path));
        if (a == kCross) {
            if (e.ok && !e.detail.empty()) open_qr(game, e.detail, "Scan it with your phone's camera to open the upload.");
            else open_message(e.ok ? "Sent" : "Not sent", e.detail.empty() ? "No details were recorded." : e.detail);
        }
        if (a == kSquare) send(e.path, e.destination);
        return;
    }
    case 4: {
        auto rows = settings_rows();
        if (settings_sel_ < 0) settings_sel_ = next_selectable(rows, -1, 1);
        if (a == kUp) settings_sel_ = next_selectable(rows, settings_sel_, -1);
        if (a == kDown) settings_sel_ = next_selectable(rows, settings_sel_, 1);
        if (a == kCross && settings_sel_ >= 0 && settings_sel_ < static_cast<int>(rows.size()) && rows[settings_sel_].act) rows[settings_sel_].act();
        return;
    }
    }
}

// ---- drawing ---------------------------------------------------------------

void Ui::draw_qr_code(const std::string &url, int x, int y, int side) {
    unsigned char temp[qrcodegen_BUFFER_LEN_MAX], code[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(url.c_str(), temp, code, qrcodegen_Ecc_LOW, 1, 20, qrcodegen_Mask_AUTO, true)) return;
    const int count = qrcodegen_getSize(code), cell = std::max(1, side / (count + 4));
    const int size = cell * (count + 4), ox = x + (side - size) / 2, oy = y + (side - size) / 2;
    paint_.rrect(x, y, side, side, 22, hex(0xffffff));
    for (int row = 0; row < count; ++row)
        for (int col = 0; col < count; ++col)
            if (qrcodegen_getModule(code, col, row)) paint_.fill(ox + (col + 2) * cell, oy + (row + 2) * cell, cell, cell, hex(pal::ink));
}

void Ui::draw_header() {
    if (logo_) paint_.texture(logo_, 86, 44, 72, 72);
    text(Face::display, "P", 176, 50, 50, kCoral);
    const int p = fonts_.measure(Face::display, "P", 50);
    text(Face::display, "Share", 176 + p, 50, 50, kCream);
    const int share_w = p + fonts_.measure(Face::display, "Share", 50);
    text(Face::regular, "Made by StonedModder", 176 + share_w + 22, 66, 25, kMist);
    // Clock, uploader pill, address: right to left.
    const LocalTime now = local_time(std::time(nullptr));
    const int clock_w = text_right(Face::semibold, two(now.hour) + ":" + two(now.minute), 1826, 56, 34, kCream);
    std::string label = "Uploader starting"; Color dot = kSun;
    if (!access_) { label = "Access needed"; dot = kRed; }
    else if (!started_ || service_done_) { label = "Uploader off"; dot = kMist; }
    else if (!status_.reachable) { label = "Uploader starting"; dot = kSun; }
    else if (status_.state == "paused") { label = "Uploader paused"; dot = kSun; }
    else if (status_.state == "uploading") { label = "Sending\xE2\x80\xA6"; dot = kCoral; }
    else if (!config_.auto_upload) { label = "Auto upload off"; dot = kMist; }
    else { label = "Uploader on"; dot = kAqua; }
    const int lw = fonts_.measure(Face::semibold, label, 25);
    const int pill_w = lw + 74, pill_x = 1826 - clock_w - 36 - pill_w;
    paint_.glass(pill_x, 50, pill_w, 56, 28, hex(pal::night2, 0.55f));
    paint_.circle(pill_x + 30, 78, 8, dot);
    text(Face::semibold, label, pill_x + 50, 61, 25, kCream);
    if (!ip_.empty()) text_right(Face::regular, ip_, pill_x - 26, 63, 24, kMist);
}

void Ui::draw_sidebar() {
    paint_.glass(74, 172, 298, 800, 26, hex(pal::night2, 0.55f));
    static const char *labels[] = {"Captures", "Auto upload", "Destinations", "History", "Settings"};
    static const char *icons[] = {"captures", "auto", "dest", "history", "settings"};
    for (int i = 0; i < 5; ++i) {
        const int y = i < 4 ? 186 + i * 72 : 892;
        const bool on = section_ == i;
        if (on) {
            paint_.rrect(86, y, 274, 62, 14, hex(0xffffff, 0.10f));
            paint_.rrect(76, y + 13, 5, 36, 2, kCoral);
        }
        paint_.icon(icons[i], 104, y + 15, 32, on ? kCream : kMist);
        text(on ? Face::semibold : Face::regular, labels[i], 150, y + 14, 27, on ? kCream : hex(0xdcd6f6));
    }
    paint_.fill(96, 876, 254, 2, hex(0xffffff, 0.10f));
}

int Ui::hint(int x, const std::string &glyph, const std::string &label, bool right_align) {
    // Returns the width used; with right_align, x is the right edge.
    std::vector<std::string> pills;
    int glyph_w = 0;
    if (glyph == "L1R1" || glyph == "L2R2") { pills = {glyph.substr(0, 2), glyph.substr(2, 2)}; glyph_w = 2 * 54 + 6; }
    else if (glyph == "touchpad") glyph_w = 64;
    else glyph_w = 34;
    const int label_w = fonts_.measure(Face::regular, label, 24);
    const int total = glyph_w + 12 + label_w;
    if (right_align) x -= total;
    const int y = 1022;
    if (!pills.empty()) {
        for (int i = 0; i < 2; ++i) {
            const int px = x + i * 60;
            paint_.rrect_stroke(px, y + 2, 54, 30, 9, 2, hex(0xffffff, 0.45f));
            const int tw = fonts_.measure(Face::semibold, pills[i], 17);
            text(Face::semibold, pills[i], px + (54 - tw) / 2, y + 7, 17, kCream);
        }
    } else if (glyph == "touchpad") paint_.icon("touchpad", x, y - 15, 64, hex(0xffffff, 0.75f));
    else if (glyph == "options") paint_.icon("options", x, y, 34, kCream);
    else {
        Color c = hex(0x7eb5f4);
        if (glyph == "btn_circle") c = hex(0xff6b6b);
        if (glyph == "btn_square") c = hex(0xf4a3d4);
        if (glyph == "btn_triangle") c = hex(0x41e0b5);
        paint_.icon(glyph, x, y, 34, c);
    }
    text(Face::regular, label, x + glyph_w + 12, y + 3, 24, hex(0xdcd6f6));
    return total;
}

void Ui::draw_footer() {
    int x = 96;
    auto left = [&](const std::string &g, const std::string &l) { x += hint(x, g, l) + 38; };
    switch (overlay_) {
    case Overlay::details: case Overlay::picker: case Overlay::menu: left("btn_cross", "Choose"); left("btn_circle", "Close"); return;
    case Overlay::qr: case Overlay::message: case Overlay::fullscreen: left("btn_circle", "Close"); return;
    case Overlay::none: break;
    }
    switch (section_) {
    case 0:
        left("btn_cross", "Details"); left("btn_square", "Send"); left("btn_triangle", "Send to\xE2\x80\xA6");
        left("L2R2", "Type"); left("touchpad", "Game & order"); break;
    case 2: left("btn_cross", dest_col_ == 0 ? "Open" : "Change"); if (dest_col_ == 1) left("btn_circle", "Back"); break;
    case 3: left("btn_cross", "Open"); left("btn_square", "Send again"); break;
    default: left("btn_cross", "Select"); break;
    }
    int right = 1826;
    right -= hint(right, "options", "Menu", true) + 36;
    hint(right, "L1R1", "Sections", true);
}

void Ui::draw_tile(const MediaItem &item, int x, int y, bool selected) {
    const int w = ps4ui::Thumbs::kWidth, h = ps4ui::Thumbs::kHeight;
    SDL_Texture *thumb = nullptr;
    if (!item.video()) { thumbs_.request(item.path); thumb = thumbs_.get(item.path); }
    if (thumb) paint_.texture(thumb, x, y, w, h);
    else {
        paint_.rrect(x, y, w, h, 14, hex(0x2a2468, 0.92f));
        paint_.rrect_stroke(x, y, w, h, 14, 2, hex(0xffffff, 0.08f));
        if (item.video()) paint_.icon("play", x + w / 2 - 34, y + h / 2 - 34, 68, hex(0xffffff, 0.35f));
        else paint_.icon("image", x + w / 2 - 34, y + h / 2 - 34, 68, hex(0xffffff, thumbs_.failed(item.path) ? 0.25f : 0.15f));
    }
    if (item.video()) {
        const int cw = fonts_.measure(Face::semibold, "CLIP", 17) + 22;
        paint_.rrect(x + 12, y + 12, cw, 28, 14, kAqua);
        text(Face::semibold, "CLIP", x + 23, y + 16, 17, kInk);
        paint_.circle(x + w - 36, y + h - 36, 24, hex(0x0e0b28, 0.75f));
        paint_.icon("play", x + w - 50, y + h - 50, 28, kCream);
    }
    auto outcome = last_outcome_.find(item.path);
    if (outcome != last_outcome_.end()) {
        if (outcome->second) { paint_.circle(x + w - 26, y + 26, 15, kAqua); paint_.icon("check", x + w - 39, y + 13, 26, kInk); }
        else { paint_.circle(x + w - 26, y + 26, 15, kRed); paint_.icon("bang", x + w - 39, y + 13, 26, kCream); }
    }
    if (selected) paint_.rrect_stroke(x - 7, y - 7, w + 14, h + 14, 20, 5, kSun);
    const std::string game = game_label(game_key(item, paths_), names_);
    text(Face::semibold, fonts_.fit(Face::semibold, game, 25, w - 4), x + 2, y + h + 12, 25, kCream);
    text(Face::regular, capture_date(item.captured), x + 2, y + h + 44, 21, kMist);
}

void Ui::draw_captures() {
    int all = 0, shots = 0, clips = 0, sent = 0;
    for (const auto &item : scan_.items) {
        if (!game_.empty() && game_ != game_key(item, paths_)) continue;
        ++all; (item.video() ? clips : shots)++;
    }
    for (const auto &item : captures_) { auto it = last_outcome_.find(item.path); if (it != last_outcome_.end() && it->second) ++sent; }
    // Type segments.
    const char *names[] = {"All", "Screenshots", "Clips"};
    const int counts[] = {all, shots, clips};
    int widths[3], total = 12;
    for (int i = 0; i < 3; ++i) {
        widths[i] = fonts_.measure(Face::semibold, names[i], 24) + fonts_.measure(Face::semibold, std::to_string(counts[i]), 17) + 66;
        total += widths[i] + 6;
    }
    paint_.glass(424, 176, total, 58, 16, hex(pal::night2, 0.6f));
    int sx = 430;
    for (int i = 0; i < 3; ++i) {
        const bool on = kind_ == i;
        if (on) paint_.rrect(sx, 182, widths[i], 46, 12, kCoral);
        const int tw = text(Face::semibold, names[i], sx + 18, 190, 24, on ? hex(0xffffff) : hex(0xdcd6f6));
        const std::string count = std::to_string(counts[i]);
        const int bw = std::max(30, fonts_.measure(Face::semibold, count, 17) + 16);
        paint_.rrect(sx + 18 + tw + 10, 192, bw, 26, 13, on ? hex(pal::ink) : kAqua);
        text(Face::semibold, count, sx + 18 + tw + 10 + (bw - fonts_.measure(Face::semibold, count, 17)) / 2, 195, 17, on ? kCream : kInk);
        sx += widths[i] + 6;
    }
    // Game and order chips.
    auto chip = [&](int x, const std::string &icon, const std::string &label) {
        const int w = fonts_.measure(Face::semibold, label, 24) + 96;
        paint_.glass(x, 176, w, 58, 16, hex(pal::night2, 0.6f));
        paint_.icon(icon, x + 16, 191, 28, kCream);
        text(Face::semibold, label, x + 52, 190, 24, kCream);
        paint_.icon("chevron_down", x + w - 38, 193, 24, kMist);
        return w;
    };
    int cx = 424 + total + 20;
    cx += chip(cx, "filter", fonts_.fit(Face::semibold, game_.empty() ? "All games" : game_label(game_, names_), 24, 300)) + 16;
    chip(cx, "sort", oldest_first_ ? "Oldest first" : "Newest first");
    text_right(Face::regular, std::to_string(captures_.size()) + " captures \xC2\xB7 " + std::to_string(sent) + " sent", 1826, 192, 24, kMist);

    if (captures_.empty()) {
        if (empty_art_) paint_.texture(empty_art_, 1124 - empty_w_ / 2, 360, empty_w_, empty_h_);
        std::string title = "No captures yet", sub = "Take a screenshot with the SHARE button; it shows up here in a few seconds.";
        if (!access_) { title = "Capture access unavailable"; sub = platform_.problem; }
        else if (scan_.open_errno) { title = "Can't read the capture folders"; sub = err_text(scan_.open_errno) + ". Save diagnostics from the Settings tab."; }
        else if (!scan_.items.empty()) { title = "Nothing matches"; sub = "Press L2/R2 or the touchpad to change the filter."; }
        const int tw = fonts_.measure(Face::semibold, title, 34);
        text(Face::semibold, title, 1124 - tw / 2, 400 + empty_h_, 34, kCream);
        const auto lines = fonts_.wrap(Face::regular, sub, 25, 900, 3);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const int lw = fonts_.measure(Face::regular, lines[i], 25);
            text(Face::regular, lines[i], 1124 - lw / 2, 452 + empty_h_ + static_cast<int>(i) * 36, 25, kMist);
        }
        return;
    }
    // Grid: 4 columns, scrolled so the selected row stays in view.
    constexpr int kTop = 266, kPitch = 272, kView = 706;
    const int row = grid_sel_ / 4;
    const float row_y = static_cast<float>(row * kPitch);
    if (row_y < scroll_target_) scroll_target_ = row_y;
    if (row_y + kPitch > scroll_target_ + kView) scroll_target_ = row_y + kPitch - kView;
    SDL_Rect clip{400, 250, 1460, 724};
    SDL_RenderSetClipRect(renderer, &clip);
    const int first = std::max(0, static_cast<int>(scroll_ / kPitch) * 4);
    const int last = std::min(static_cast<int>(captures_.size()), first + 16);
    // Prefetch the next rows' thumbnails.
    for (int i = last; i < std::min(static_cast<int>(captures_.size()), last + 8); ++i)
        if (!captures_[i].video()) thumbs_.request(captures_[i].path);
    for (int i = first; i < last; ++i) {
        const int x = 424 + (i % 4) * 352, y = kTop + (i / 4) * kPitch - static_cast<int>(scroll_);
        if (y > 980 || y + kPitch < 240) continue;
        if (i != grid_sel_) draw_tile(captures_[i], x, y, false);
    }
    // The selected tile last, so its ring sits on top.
    if (grid_sel_ >= first && grid_sel_ < last)
        draw_tile(captures_[grid_sel_], 424 + (grid_sel_ % 4) * 352, kTop + row * kPitch - static_cast<int>(scroll_), true);
    SDL_RenderSetClipRect(renderer, nullptr);
    // Scroll bar.
    const int rows = (static_cast<int>(captures_.size()) + 3) / 4;
    if (rows * kPitch > kView) {
        const int track = 700, bar = std::max(60, track * kView / (rows * kPitch));
        const int pos = static_cast<int>((track - bar) * scroll_ / std::max(1, rows * kPitch - kView));
        paint_.rrect(1838, 262, 6, track, 3, hex(0xffffff, 0.08f));
        paint_.rrect(1838, 262 + pos, 6, bar, 3, hex(0xffffff, 0.35f));
    }
}

void Ui::draw_toggle(int x, int y, bool on) {
    paint_.rrect(x, y, 76, 42, 21, on ? kAqua : hex(0xffffff, 0.18f));
    paint_.circle(on ? x + 55 : x + 21, y + 21, 16, hex(0xffffff));
}

void Ui::draw_rows(const std::vector<RowSpec> &rows, int x, int y, int w, int h, int selected, bool focused) {
    SDL_Rect clip{x, y, w, h};
    // Scroll so the selected row is visible.
    int offset = 0, cursor = 0;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const int rh = rows[i].kind == RowSpec::header ? 56 : (rows[i].sub.empty() ? 76 : 98);
        if (i == selected && cursor + rh > h - 20) offset = cursor + rh - (h - 20);
        cursor += rh;
    }
    SDL_RenderSetClipRect(renderer, &clip);
    int cy = y + 12 - offset;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const RowSpec &row = rows[i];
        if (row.kind == RowSpec::header) {
            text(Face::semibold, row.label, x + 22, cy + 20, 19, kMist);
            const int lw = fonts_.measure(Face::semibold, row.label, 19);
            paint_.fill(x + 22 + lw + 16, cy + 32, w - 44 - lw - 16, 2, hex(0xffffff, 0.12f));
            cy += 56;
            continue;
        }
        const int rh = row.sub.empty() ? 76 : 98;
        const bool on = focused && i == selected;
        if (on) paint_.rrect(x + 10, cy + 4, w - 20, rh - 8, 14, kCoral);
        const Color label = row.kind == RowSpec::info ? hex(0xdcd6f6) : kCream;
        text(row.kind == RowSpec::info ? Face::regular : Face::semibold, row.label, x + 30, cy + 22, 27, on ? hex(0xffffff) : label);
        if (!row.sub.empty()) {
            const auto lines = fonts_.wrap(Face::regular, row.sub, 19, w - 140, 1);
            if (!lines.empty()) text(Face::regular, lines[0], x + 30, cy + 58, 19, on ? hex(0xffffff, 0.85f) : kMist);
        }
        const int right = x + w - 30;
        if (row.kind == RowSpec::toggle) {
            draw_toggle(right - 76, cy + 17, row.on);
            text_right(Face::regular, row.on ? "On" : "Off", right - 96, cy + 23, 24, on ? hex(0xffffff) : kCream);
        } else if (row.kind == RowSpec::link) {
            paint_.icon("chevron", right - 30, cy + 22, 32, on ? hex(0xffffff) : kMist);
        } else if (!row.value.empty()) {
            const std::string v = fonts_.fit(Face::regular, row.value, 24, w / 2);
            text_right(Face::regular, v, right, cy + 24, 24, on ? hex(0xffffff) : kMist);
        }
        cy += rh;
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}

void Ui::draw_auto() {
    paint_.glass(424, 172, 700, 330, 24, hex(pal::night2, 0.55f));
    const bool paused = status_.state == "paused";
    const Color ring = !started_ ? kMist : paused ? kSun : kAqua;
    // Ring with a bolt, like the PS5 page.
    {
        const int cx = 562, cy = 336;
        paint_.circle(cx, cy, 94, ring);
        paint_.circle(cx, cy, 84, hex(pal::night2, 1.0f));
        paint_.circle(cx, cy, 58, hex(0xffffff, 0.06f));
        paint_.icon("bolt", cx - 36, cy - 36, 72, ring);
    }
    std::string state = !access_ ? "No access" : !started_ ? "Off" : !status_.reachable ? "Starting" : paused ? "Paused" : status_.state == "uploading" ? "Sending" : "Watching";
    text(Face::display, state, 700, 220, 56, kCream);
    std::string since = "Version " + (status_.version.empty() ? std::string("1.0.0") : status_.version);
    if (status_.started > 0) {
        const std::int64_t mins = std::max<std::int64_t>(0, (std::time(nullptr) - status_.started) / 60);
        since = "For " + (mins >= 60 ? std::to_string(mins / 60) + " h " : std::string()) + std::to_string(mins % 60) + " min, " + since;
    }
    text(Face::regular, since, 702, 292, 24, kMist);
    const std::string about = config_.auto_upload
        ? "Sends each new screenshot and clip as soon as it is saved. On PS4 it runs while PShare is open; closing PShare stops it."
        : "Automatic uploads are off. You can still send captures by hand from the Captures tab.";
    const auto lines = fonts_.wrap(Face::regular, about, 23, 390, 5);
    for (std::size_t i = 0; i < lines.size(); ++i) text(Face::regular, lines[i], 702, 340 + static_cast<int>(i) * 32, 23, hex(0xdcd6f6));
    // Counters.
    const char *labels[] = {"Sent", "Failed", "Waiting"};
    const int values[] = {status_.uploaded, status_.failed, status_.queued};
    for (int i = 0; i < 3; ++i) {
        const int x = 424 + i * 240;
        paint_.glass(x, 526, 220, 150, 22, hex(pal::night2, 0.55f));
        text(Face::regular, labels[i], x + 24, 548, 23, kMist);
        text(Face::display, std::to_string(values[i]), x + 24, 584, 54, kCream);
    }
    if (!status_.last_link.empty()) {
        paint_.icon("link", 428, 712, 26, kAqua);
        text(Face::mono, fonts_.fit(Face::mono, status_.last_link, 22, 640), 466, 712, 22, kAqua);
    }
    if (!status_.last_error.empty()) {
        const auto err = fonts_.wrap(Face::regular, "Last error: " + status_.last_error, 22, 690, 3);
        for (std::size_t i = 0; i < err.size(); ++i) text(Face::regular, err[i], 428, 760 + static_cast<int>(i) * 30, 22, hex(0xff9aa6));
    }
    paint_.glass(1164, 172, 662, 800, 24, hex(pal::night2, 0.55f));
    auto rows = auto_rows();
    if (auto_sel_ < 0) auto_sel_ = next_selectable(rows, -1, 1);
    draw_rows(rows, 1164, 172, 662, 800, auto_sel_, true);
}

void Ui::draw_destinations() {
    paint_.glass(424, 172, 520, 800, 24, hex(pal::night2, 0.55f));
    paint_.glass(984, 172, 842, 800, 24, hex(pal::night2, 0.55f));
    const int n = static_cast<int>(config_.destinations.size());
    if (n == 0) {
        text(Face::semibold, "No destinations yet", 460, 220, 30, kCream);
        const auto lines = fonts_.wrap(Face::regular, "Add Discord, Telegram, Catbox, Litterbox, a web server or a USB folder from your phone. Scan the code on the right.", 24, 450, 6);
        for (std::size_t i = 0; i < lines.size(); ++i) text(Face::regular, lines[i], 460, 274 + static_cast<int>(i) * 34, 24, kMist);
        if (!browser().empty()) { draw_qr_code(browser(), 1225, 260, 360); text(Face::mono, browser(), 1225, 650, 24, kAqua); }
        else text(Face::regular, "Waiting for a network address\xE2\x80\xA6", 1040, 260, 26, kMist);
        return;
    }
    dest_sel_ = std::clamp(dest_sel_, 0, n - 1);
    const int pitch = 88, view = 776;
    const int offset = std::max(0, (dest_sel_ + 1) * pitch - view);
    SDL_Rect clip{424, 180, 520, 784};
    SDL_RenderSetClipRect(renderer, &clip);
    for (int i = 0; i < n; ++i) {
        const Destination &d = config_.destinations[i];
        const int y = 184 + i * pitch - offset;
        if (y < 120 || y > 1000) continue;
        const bool sel = i == dest_sel_;
        if (sel) paint_.rrect(436, y, 496, pitch - 8, 14, dest_col_ == 0 ? kCoral : hex(0xffffff, 0.12f));
        paint_.circle(466, y + 30, 7, destination_color(d.type));
        text(Face::semibold, fonts_.fit(Face::semibold, d.display_name(), 27, 230), 490, y + 12, 27, kCream);
        text(Face::regular, fonts_.fit(Face::regular, type_label(d.type), 20, 230), 490, y + 48, 20, sel && dest_col_ == 0 ? hex(0xffffff, 0.85f) : kMist);
        const std::string state = !d.enabled ? "Off" : d.configured() ? "Ready" : "Set up";
        const int sw = text_right(Face::regular, state, 912, y + 26, 22, kCream);
        if (d.id == config_.default_destination) {
            const int cw = fonts_.measure(Face::semibold, "DEFAULT", 16) + 22;
            paint_.rrect(912 - sw - 16 - cw, y + 24, cw, 28, 14, kAqua);
            text(Face::semibold, "DEFAULT", 912 - sw - 16 - cw + 11, y + 28, 16, kInk);
        }
    }
    SDL_RenderSetClipRect(renderer, nullptr);
    auto rows = destination_rows(config_.destinations[dest_sel_]);
    draw_rows(rows, 984, 172, 842, 800, dest_row_, dest_col_ == 1);
}

void Ui::draw_history() {
    text(Face::display, "History", 424, 176, 42, kCream);
    const auto &entries = history_.entries();
    int ok = 0, failed = 0, automatic = 0;
    for (const auto &e : entries) { (e.ok ? ok : failed)++; if (e.automatic) ++automatic; }
    text_right(Face::regular, std::to_string(ok) + " sent \xC2\xB7 " + std::to_string(failed) + " failed \xC2\xB7 " + std::to_string(automatic) + " by the uploader", 1826, 192, 24, kMist);
    paint_.glass(424, 244, 1402, 728, 24, hex(pal::night2, 0.55f));
    const int n = static_cast<int>(entries.size());
    if (n == 0) {
        text(Face::semibold, "Nothing sent yet", 470, 300, 30, kCream);
        text(Face::regular, "Press Square on a capture to send it. Every attempt is listed here.", 470, 350, 24, kMist);
        return;
    }
    hist_sel_ = std::clamp(hist_sel_, 0, n - 1);
    const int pitch = 90, view = 712;
    const int offset = std::max(0, (hist_sel_ + 1) * pitch - view);
    SDL_Rect clip{424, 252, 1402, 712};
    SDL_RenderSetClipRect(renderer, &clip);
    for (int i = 0; i < n; ++i) {
        const int y = 256 + i * pitch - offset;
        if (y < 180) continue;
        if (y > 980) break;
        const HistoryEntry &e = entries[n - 1 - i];
        const bool sel = i == hist_sel_;
        if (sel) paint_.rrect(436, y, 1378, pitch - 8, 14, kCoral);
        paint_.circle(495, y + 41, 20, sel ? hex(0xffffff, 0.25f) : (e.ok ? hex(pal::aqua, 0.18f) : hex(pal::red, 0.2f)));
        paint_.icon(e.ok ? "check" : "cross", 479, y + 25, 32, e.ok ? kAqua : kRed);
        const bool clip_kind = ends_with_ci(e.path, ".mp4") || ends_with_ci(e.path, ".webm");
        const std::string game = names_.display(title_id_from_path(e.path));
        text(Face::semibold, fonts_.fit(Face::semibold, std::string(clip_kind ? "Clip" : "Screenshot") + " \xC2\xB7 " + game, 27, 1000), 546, y + 12, 27, kCream);
        const Destination *d = config_.find(e.destination);
        std::string sub = (d ? d->display_name() : e.destination) + " \xC2\xB7 " + when_text(e.time) + " \xC2\xB7 " + (e.automatic ? "automatic" : "by hand");
        if (!e.ok && !e.detail.empty()) sub += " \xC2\xB7 " + e.detail;
        text(Face::regular, fonts_.fit(Face::regular, sub, 21, 1050), 546, y + 50, 21, sel ? hex(0xffffff, 0.85f) : kMist);
        text_right(Face::regular, e.ok ? "Link" : "Failed", 1754, y + 28, 23, kCream);
        paint_.icon("chevron", 1764, y + 25, 32, sel ? hex(0xffffff) : kMist);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}

void Ui::draw_settings() {
    paint_.glass(424, 172, 700, 800, 24, hex(pal::night2, 0.55f));
    text(Face::display, "Set up from your phone", 466, 210, 36, kCream);
    const auto lines = fonts_.wrap(Face::regular, "Scan the code with your phone's camera, on the same Wi-Fi as the PS4. Add Discord webhooks, Telegram bots, Catbox and folders there, then pick them here.", 24, 616, 4);
    for (std::size_t i = 0; i < lines.size(); ++i) text(Face::regular, lines[i], 466, 270 + static_cast<int>(i) * 34, 24, kMist);
    if (!browser().empty()) {
        draw_qr_code(browser(), 594, 430, 360);
        const int tw = fonts_.measure(Face::mono, browser(), 24);
        text(Face::mono, browser(), 774 - tw / 2, 816, 24, kAqua);
        text(Face::regular, "The phone pages have no password: use them only on your home network.", 466, 890, 20, kMist);
    } else text(Face::regular, "Waiting for a network address\xE2\x80\xA6", 466, 480, 26, kMist);
    paint_.glass(1164, 172, 662, 800, 24, hex(pal::night2, 0.55f));
    auto rows = settings_rows();
    if (settings_sel_ < 0) settings_sel_ = next_selectable(rows, -1, 1);
    draw_rows(rows, 1164, 172, 662, 800, settings_sel_, true);
}

void Ui::draw_details() {
    if (captures_.empty()) return;
    const MediaItem &item = captures_[grid_sel_];
    dim(0.45f);
    const int x = static_cast<int>(panel_x_);
    paint_.fill(x, 0, 1920 - x, 1080, hex(0x231d5c, 0.97f));
    paint_.fill(x, 0, 2, 1080, hex(0xffffff, 0.10f));
    text(Face::semibold, item.kind_label(), x + 36, 34, 34, kCream);
    paint_.fill(x + 36, 86, 588, 2, hex(0xffffff, 0.15f));
    const SDL_Rect area{x + 36, 104, 588, 330};
    if (preview_) {
        paint_.rrect(area.x, area.y, area.w, area.h, 16, hex(0x000000, 0.35f));
        paint_.fit_texture(preview_, preview_w_, preview_h_, area);
    } else {
        paint_.rrect(area.x, area.y, area.w, area.h, 16, hex(0x2a2468));
        paint_.icon(item.video() ? "play" : "image", area.x + area.w / 2 - 50, area.y + area.h / 2 - 50, 100, hex(0xffffff, 0.35f));
        if (item.video()) text(Face::regular, "Clips play on your phone or PC", area.x + 150, area.y + area.h - 52, 22, kMist);
    }
    const std::string game = game_label(game_key(item, paths_), names_);
    text(Face::semibold, fonts_.fit(Face::semibold, game, 34, 588), x + 36, 452, 34, kCream);
    std::string meta = capture_date(item.captured) + "  \xC2\xB7  " + human_size(item.size);
    if (!item.title_id.empty()) meta += "  \xC2\xB7  " + item.title_id;
    text(Face::regular, meta, x + 36, 500, 22, kMist);
    std::string sent = "Not sent yet"; Color sc = kMist; const char *icon = "clock";
    if (const HistoryEntry *ok = history_.last_success(item.path)) {
        const Destination *d = config_.find(ok->destination);
        sent = "Sent to " + (d ? d->display_name() : ok->destination) + ", " + when_text(ok->time); sc = kAqua; icon = "check";
    } else if (last_outcome_.count(item.path)) { sent = "The last try failed (see History)"; sc = kRed; icon = "bang"; }
    paint_.icon(icon, x + 36, 538, 28, sc);
    text(Face::regular, fonts_.fit(Face::regular, sent, 23, 540), x + 74, 539, 23, (sc.r == kMist.r && sc.g == kMist.g && sc.b == kMist.b) ? hex(0xdcd6f6) : sc);
    // Actions.
    const auto list = usable_destinations();
    const Destination *def = config_.destination_for(item.title_id);
    struct Item { std::string label, sub; bool chevron; };
    Item items[4] = {
        {def && def->enabled && def->configured() ? "Send to " + def->display_name() : (list.empty() ? "Set up a destination" : "Send"),
         def ? type_label(def->type) : (list.empty() ? "Scan a code with your phone" : ""), list.empty()},
        {"Send to\xE2\x80\xA6", "", true},
        {item.video() ? "Play on your phone" : "View full screen", "", item.video()},
        {"Open in a browser", "On your phone or PC, on the same network", true},
    };
    int y = 612;
    for (int i = 0; i < 4; ++i) {
        const int h = items[i].sub.empty() ? 70 : 86;
        const bool sel = overlay_ == Overlay::details && overlay_sel_ == i;
        if (sel) paint_.rrect(x + 22, y, 616, h - 6, 14, kCoral);
        text(Face::semibold, items[i].label, x + 46, y + (items[i].sub.empty() ? 18 : 12), 26, sel ? hex(0xffffff) : kCream);
        if (!items[i].sub.empty()) text(Face::regular, items[i].sub, x + 46, y + 48, 19, sel ? hex(0xffffff, 0.85f) : kMist);
        if (items[i].chevron) paint_.icon("chevron", x + 590, y + (h - 6) / 2 - 16, 32, sel ? hex(0xffffff) : kMist);
        y += h;
    }
}

void Ui::draw_picker() {
    dim(0.5f);
    const auto list = usable_destinations();
    const int h = std::min(760, 150 + std::max(1, static_cast<int>(list.size())) * 84);
    const int x = 560, y = (1080 - h) / 2;
    paint_.glass(x, y, 800, h, 26, hex(pal::night2, 0.88f));
    text(Face::display, "Send to\xE2\x80\xA6", x + 40, y + 34, 38, kCream);
    if (list.empty()) { text(Face::regular, "No destinations are set up. Use Settings > Set up from your phone.", x + 40, y + 104, 23, kMist); return; }
    overlay_sel_ = std::clamp(overlay_sel_, 0, static_cast<int>(list.size()) - 1);
    const int offset = std::max(0, (overlay_sel_ + 1) * 84 - (h - 120));
    SDL_Rect clip{x, y + 100, 800, h - 110};
    SDL_RenderSetClipRect(renderer, &clip);
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const int ry = y + 104 + i * 84 - offset;
        const bool sel = i == overlay_sel_;
        if (sel) paint_.rrect(x + 20, ry, 760, 76, 14, kCoral);
        paint_.circle(x + 54, ry + 38, 8, destination_color(list[i]->type));
        text(Face::semibold, list[i]->display_name(), x + 80, ry + 10, 27, kCream);
        text(Face::regular, type_label(list[i]->type), x + 80, ry + 44, 19, sel ? hex(0xffffff, 0.85f) : kMist);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}

void Ui::draw_qr() {
    dim(0.5f);
    const int x = 440, y = 272, w = 1040, h = 536;
    paint_.glass(x, y, w, h, 28, hex(pal::night2, 0.9f));
    draw_qr_code(qr_url_, x + 48, y + 58, 420);
    text(Face::display, fonts_.fit(Face::display, qr_title_, 40, 470), x + 516, y + 66, 40, kCream);
    auto url = fonts_.wrap(Face::mono, qr_url_, 24, 470, 4);
    int ty = y + 140;
    for (const auto &line : url) { text(Face::mono, line, x + 516, ty, 24, kAqua); ty += 34; }
    ty += 24;
    for (const auto &line : fonts_.wrap(Face::regular, qr_caption_, 23, 470, 5)) { text(Face::regular, line, x + 516, ty, 23, hex(0xdcd6f6)); ty += 33; }
}

void Ui::draw_message() {
    dim(0.5f);
    const auto lines = fonts_.wrap(Face::regular, msg_text_, 25, 800, 10);
    const int h = 170 + static_cast<int>(lines.size()) * 36;
    const int x = 520, y = (1080 - h) / 2;
    paint_.glass(x, y, 880, h, 26, hex(pal::night2, 0.9f));
    text(Face::display, msg_title_, x + 40, y + 36, 38, kCream);
    for (std::size_t i = 0; i < lines.size(); ++i) text(Face::regular, lines[i], x + 40, y + 108 + static_cast<int>(i) * 36, 25, hex(0xdcd6f6));
}

void Ui::draw_menu() {
    dim(0.5f);
    const char *items[] = {"Refresh captures", "Save diagnostics", "Close PShare"};
    const int x = 660, y = 360, w = 600, h = 360;
    paint_.glass(x, y, w, h, 26, hex(pal::night2, 0.9f));
    text(Face::display, "Menu", x + 40, y + 30, 38, kCream);
    for (int i = 0; i < 3; ++i) {
        const int ry = y + 100 + i * 80;
        if (overlay_sel_ == i) paint_.rrect(x + 20, ry, w - 40, 70, 14, kCoral);
        text(Face::semibold, items[i], x + 46, ry + 19, 27, kCream);
    }
    text(Face::regular, "On PS4, closing PShare also stops its uploader.", x + 40, y + h + 18, 22, kMist);
}

void Ui::draw_fullscreen() {
    paint_.fill(0, 0, 1920, 1080, hex(0x000000));
    paint_.fit_texture(preview_, preview_w_, preview_h_, SDL_Rect{0, 0, 1920, 1080});
}

void Ui::draw_toasts() {
    const std::uint32_t now = SDL_GetTicks();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [now](const Toast &t) { return now >= t.until; }), toasts_.end());
    int y = 960;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const int h = it->sub.empty() ? 84 : 112;
        y -= h;
        const int x = 1316, w = 560;
        paint_.glass(x, y, w, h, 20, hex(pal::night2, 0.92f));
        paint_.circle(x + 44, y + h / 2, 20, it->ok ? kAqua : kRed);
        paint_.icon(it->ok ? "check" : "bang", x + 28, y + h / 2 - 16, 32, kInk);
        text(Face::semibold, fonts_.fit(Face::semibold, it->title, 25, w - 110), x + 84, y + (it->sub.empty() ? 27 : 18), 25, kCream);
        if (!it->sub.empty()) text(Face::regular, fonts_.fit(Face::regular, it->sub, 20, w - 110), x + 84, y + 56, 20, kMist);
        // Time left.
        const float left = std::clamp((it->until - now) / 4500.0f, 0.0f, 1.0f);
        paint_.rrect(x + 84, y + h - 16, static_cast<int>((w - 120) * left), 5, 2, it->ok ? kAqua : kRed);
        y -= 14;
    }
}

void Ui::draw_upload_card() {
    if (status_.state != "uploading") return;
    const int x = 620, y = 38, w = 600, h = 84;
    paint_.glass(x, y, w, h, 18, hex(pal::night2, 0.75f));
    paint_.icon("dest", x + 20, y + 22, 40, kCoral);
    const Destination *d = config_.find(status_.destination);
    text(Face::semibold, fonts_.fit(Face::semibold, "Sending to " + (d ? d->display_name() : status_.destination.empty() ? std::string("your destination") : status_.destination), 23, 420), x + 78, y + 14, 23, kCream);
    if (status_.total > 0) {
        const float f = std::clamp(static_cast<float>(status_.sent) / status_.total, 0.0f, 1.0f);
        text_right(Face::regular, std::to_string(static_cast<int>(f * 100)) + "%", x + w - 22, y + 14, 21, kAqua);
        text(Face::regular, human_size(status_.total), x + 78, y + 48, 19, kMist);
        paint_.rrect(x + 300, y + 54, 278, 8, 4, hex(0xffffff, 0.15f));
        paint_.rrect(x + 300, y + 54, std::max(8, static_cast<int>(278 * f)), 8, 4, kAqua);
    } else text(Face::regular, "Preparing\xE2\x80\xA6", x + 78, y + 48, 19, kMist);
}

void Ui::render() {
    paint_.backdrop();
    if (overlay_ == Overlay::fullscreen && preview_) { draw_fullscreen(); draw_footer(); return; }
    draw_header();
    draw_sidebar();
    switch (section_) {
    case 0: draw_captures(); break;
    case 1: draw_auto(); break;
    case 2: draw_destinations(); break;
    case 3: draw_history(); break;
    case 4: draw_settings(); break;
    }
    draw_upload_card();
    switch (overlay_) {
    case Overlay::details: draw_details(); break;
    case Overlay::picker: draw_picker(); break;
    case Overlay::qr: draw_qr(); break;
    case Overlay::message: draw_message(); break;
    case Overlay::menu: draw_menu(); break;
    default: break;
    }
    draw_footer();
    draw_toasts();
    fonts_.trim_cache();
}

void Ui::run(bool smoke, const char *shots_dir) {
    // Scripted screenshots for the PC preview: (name, actions before it).
    struct Shot { const char *name; std::vector<Action> actions; };
    std::vector<Shot> shots;
    if (shots_dir) {
        shots = {{"captures", {}}, {"capture-details", {kRight, kCross}}, {"send-to", {kDown, kCross}},
                 {"browser-qr", {kCircle, kCircle, kCross, kDown, kDown, kDown, kCross}}, {"auto-upload", {kCircle, kCircle, kR1}},
                 {"destinations", {kR1}}, {"destination-detail", {kRight}}, {"history", {kCircle, kR1}},
                 {"history-qr", {kCross}}, {"settings", {kCircle, kR1}}, {"menu", {kOptions}}, {"clips-filter", {kCircle, kR1, kR2, kR2}}};
    }
    std::size_t shot = 0;
    int frames = 0, settle = 0;
    Action held = kNone; std::uint32_t next_repeat = 0;
    auto press = [&](Action a, std::uint32_t now) {
        if (a == kUp || a == kDown || a == kLeft || a == kRight) { held = a; next_repeat = now + 380; }
        handle(a);
    };
    auto release = [&](Action a) { if (a == held) held = kNone; };
    while (!quit_) {
        const std::uint32_t now = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit_ = true;
#ifdef PSHARE_PS4
            if (event.type == SDL_JOYDEVICEADDED && !pad_) pad_ = SDL_JoystickOpen(event.jdevice.which);
            if (event.type == SDL_JOYDEVICEREMOVED && pad_ && SDL_JoystickInstanceID(pad_) == event.jdevice.which) { SDL_JoystickClose(pad_); pad_ = nullptr; held = kNone; }
            if (event.type == SDL_JOYBUTTONDOWN) { last_raw_ = event.jbutton.button; press(ps4_button(event.jbutton.button), now); }
            if (event.type == SDL_JOYBUTTONUP) release(ps4_button(event.jbutton.button));
#else
            if (event.type == SDL_CONTROLLERDEVICEADDED && !controller_) controller_ = SDL_GameControllerOpen(event.cdevice.which);
            if (event.type == SDL_CONTROLLERBUTTONDOWN) { last_raw_ = event.cbutton.button; press(controller_button(event.cbutton.button), now); }
            if (event.type == SDL_CONTROLLERBUTTONUP) release(controller_button(event.cbutton.button));
#endif
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                Action a = kNone;
                switch (event.key.keysym.sym) {
                case SDLK_RETURN: a = kCross; break;   case SDLK_ESCAPE: a = kCircle; break;
                case SDLK_s: a = kSquare; break;       case SDLK_t: a = kTriangle; break;
                case SDLK_UP: a = kUp; break;          case SDLK_DOWN: a = kDown; break;
                case SDLK_LEFT: a = kLeft; break;      case SDLK_RIGHT: a = kRight; break;
                case SDLK_q: a = kL1; break;           case SDLK_e: a = kR1; break;
                case SDLK_z: a = kL2; break;           case SDLK_c: a = kR2; break;
                case SDLK_f: a = kTouch; break;        case SDLK_m: a = kOptions; break;
                case SDLK_r: a = kRefresh; break;      case SDLK_END: a = kQuit; break;
                }
                press(a, now);
            }
            if (event.type == SDL_KEYUP) {
                const auto k = event.key.keysym.sym;
                if (k == SDLK_UP) release(kUp);
                if (k == SDLK_DOWN) release(kDown);
                if (k == SDLK_LEFT) release(kLeft);
                if (k == SDLK_RIGHT) release(kRight);
            }
        }
        if (held != kNone && now >= next_repeat) { handle(held); next_repeat = now + 70; }
        poll_status(now);
        if (overlay_ == Overlay::none && now >= next_scan_) { refresh(true); next_scan_ = now + 10000; }
        if (thumbs_.pump(renderer)) dirty_ = true;
        // Animations: grid scroll and the details panel slide.
        if (std::fabs(scroll_target_ - scroll_) > 0.5f) { scroll_ += (scroll_target_ - scroll_) * 0.3f; dirty_ = true; }
        else if (scroll_ != scroll_target_) { scroll_ = scroll_target_; dirty_ = true; }
        const float panel_target = overlay_ == Overlay::details ? 1260.0f : 1920.0f;
        if (overlay_ == Overlay::details && std::fabs(panel_x_ - panel_target) > 0.5f) { panel_x_ += (panel_target - panel_x_) * 0.35f; dirty_ = true; }
        if (!toasts_.empty()) dirty_ = true;
        if (shots_dir && settle == 0 && shot < shots.size()) { for (Action a : shots[shot].actions) handle(a); settle = 30; panel_x_ = 1260; }
        if (dirty_ || now >= next_forced_ || smoke || shots_dir) {
            const std::uint64_t t0 = SDL_GetPerformanceCounter();
            render();
            if (smoke && frames == 4) {
                SDL_Surface *snapshot = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_RGBA32);
                if (snapshot) { SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, snapshot->pixels, snapshot->pitch); SDL_SaveBMP(snapshot, "build/ps4-host/smoke.bmp"); SDL_FreeSurface(snapshot); }
            }
            if (shots_dir && settle > 0 && --settle == 0) {
                SDL_Surface *snapshot = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_RGBA32);
                if (snapshot) {
                    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, snapshot->pixels, snapshot->pitch);
                    SDL_SaveBMP(snapshot, (std::string(shots_dir) + "/" + shots[shot].name + ".bmp").c_str());
                    SDL_FreeSurface(snapshot);
                }
                if (++shot >= shots.size()) quit_ = true;
                // Toasts from earlier steps would cover later shots.
                toasts_.clear();
            }
            SDL_RenderPresent(renderer);
            hide_system_splash(); // once; no-op afterwards
            if (profile_) std::fprintf(stderr, "frame %.2f ms\n", (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
            dirty_ = false;
            next_forced_ = now + 1000;
            if (smoke && ++frames >= 5) quit_ = true;
        } else SDL_Delay(10);
    }
}

} // namespace

int main(int argc, char **argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const char *shots = argc > 2 && std::string(argv[1]) == "--shots" ? argv[2] : nullptr;
    Paths paths; Platform platform; const bool access = platform.start(paths);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    SDL_Window *window = SDL_CreateWindow("PShare PS4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080, 0);
    if (!window) { SDL_Quit(); return 1; }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_RenderSetLogicalSize(renderer, 1920, 1080);
    // Splash while fonts, art and the uploader start (assets/art/splash.jpg).
    const std::uint32_t splash_at = SDL_GetTicks();
    bool splash_shown = false;
    {
        ps4ui::Image splash;
        if (ps4ui::decode_file(platform.assets + "/art/splash.jpg", &splash)) {
            if (SDL_Texture *t = ps4ui::upload(renderer, splash.w == 1920 && splash.h == 1080 ? splash : ps4ui::cover(splash, 1920, 1080), false)) {
                SDL_RenderCopy(renderer, t, nullptr, nullptr);
                SDL_RenderPresent(renderer);
                SDL_DestroyTexture(t);
                splash_shown = true;
                hide_system_splash();
            }
        }
    }
    int result = 0;
    {
        Ui ui(platform, paths, access, window);
        if (!ui.init()) { std::fprintf(stderr, "PShare: font missing\n"); result = 1; }
        else {
            // Keep the splash up briefly so it doesn't just flash.
            const std::uint32_t shown = SDL_GetTicks() - splash_at;
            if (splash_shown && !smoke && !shots && shown < 1500) SDL_Delay(1500 - shown);
            ui.run(smoke, shots);
        }
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return result;
}
