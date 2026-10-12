# PShare for PS4 — changelog

## 1.0

- First release. Same features as 0.5.2, with the release name "PShare",
  version 01.00, and diagnostics written only on request (or automatically
  when folder access fails).

## 0.5.2

- Fixed: after 0.5 added a launch background (sce_sys/pic1.png), the PS4's
  launch screen stayed on top of PShare and hid it. The system keeps that
  screen up until the app calls sceSystemServiceHideSplashScreen(); the PS5
  build does this after its first frame, the PS4 port never did. 0.5.2 calls
  it as soon as PShare draws (its own splash, then the first frame). The
  result is listed in the diagnostics file ("hide system splash").

## 0.5.1

- Removed sce_sys/pic0.png. On PS4 that image is drawn as a layer *over*
  the home screen, so an opaque picture there hid the app's menu. Only
  pic1.png (the background behind the menu) is kept.

## 0.5

- New app icon (sce_sys/icon0.png) and home-screen/launch background
  (sce_sys/pic1.png), from the user's artwork.
- The same background is shown as a splash inside PShare while it starts
  (assets/art/splash.jpg), for at least 1.5 seconds.

## 0.4

- **The PS5 app's look.** The TV interface was rebuilt to match PShare 1.0
  on PS5: backdrop art, frosted sidebar with Captures / Auto upload /
  Destinations / History / Settings, a 4-column thumbnail gallery with clip,
  sent and failed badges, type tabs with counts, game and order chips, the
  slide-in details panel, Send to... picker, QR cards, toasts, an upload
  progress card, the uploader status pill and clock.
- The PS5 interface code itself needs desktop OpenGL 4.6; the PS4's GPU
  library (Piglet) is OpenGL ES 2 with offline-compiled shaders only, so the
  same design is drawn with SDL2 instead (ps4/ui_kit.cpp). Frames are only
  redrawn when something changes, and thumbnails decode on a worker thread.
- Controls now follow the PS5 app: D-pad moves through the grid, L2/R2 change
  the type, the touchpad cycles games (and the order after the last game),
  Options opens a menu (Refresh, Save diagnostics, Close PShare).
- Auto upload and Destinations can be changed from the controller: pause,
  automatic sending, screenshots/clips, notifications, enable a destination,
  make it the default. Webhooks and tokens are still entered on the phone.
- Console local time is used for the clock and history.
- Not ported: interface sounds, animated backdrop, TV clip playback.

## 0.3

- **Uploads fixed (file sizes were wrong).** First console run showed every
  upload failing with "Capture changed or read failed". Cause: OpenOrbis
  0.5.4's `struct stat` doesn't match the PS4 kernel's layout, so the app read
  the file's block count as its size (a 512 KB screenshot looked like 1024
  bytes). 0.3 translates the kernel's layout for every stat/fstat call. This
  also fixes the MiB shown on the TV, phone downloads (which were cut short)
  and file times. The diagnostics file has a new "stat check" line.
- OpenOrbis' `lstat` always fails (ENOSYS); the port uses `stat` instead.

## 0.2

- **Controller input fixed.** 0.1 used SDL's GameController API, but the
  OpenOrbis 0.5.4 SDL2 port reports a joystick GUID that has no controller
  mapping, so on a PS4 no button would have done anything. 0.2 reads the raw
  PS4 pad buttons (the same indices the OpenOrbis SDL2 sample uses).
- The game filter moved off the Share button (the system reserves Share, so
  apps never see it) to **L2 / R2 / touchpad click**. D-pad up/down repeats
  when held.
- GoldHEN check: if kernel access (syscall 11) isn't there, the app says so
  instead of attempting mounts.
- Stale sandbox mounts left by a crashed run are unmounted and retried.
- The "access unavailable" screen shows each mount's error code.
- A **diagnostics file** is written at every launch (and on Triangle in the
  Auto upload tab) to `/data/pshare-ps4/diagnostics.txt` and to the root of any
  connected USB drive as `PShare-PS4-diagnostics.txt`. It lists the mount
  results, network/HTTP status, controller details and the capture folder
  layout (folder and file names only, no settings, tokens or webhooks).
- Captures in folders without a CUSA/NPXS ID are grouped by folder name.
- The console IP falls back to the PS4 network-control library.
- Upload notifications use the PS4 notification popup.
- HTTP: the upload body is allocated once at its exact size (no 2x peak), an
  out-of-memory case is reported instead of crashing, the console's proxy
  setting is honoured, and certificate rejections are reported with their
  flags. TLS verification remains on.

