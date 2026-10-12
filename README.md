# PShare for PS4 — 1.0

PShare sends your PS4 screenshots and clips where you want them — Discord,
Telegram, Catbox, Litterbox, your own web server or a USB drive — straight
from the console. It's a port of **PShare 1.0 for PS5 by StonedModder** to
jailbroken PS4 consoles, with the same look: a thumbnail gallery, a details
panel, automatic uploads, upload history and phone setup by QR code.

## Requirements

- A jailbroken PS4 with **GoldHEN** enabled. Tested on a PS4 Pro, firmware
  11.02, GoldHEN. Other firmware/jailbreak combinations are not verified.
- A phone or PC on the same network as the PS4, to enter webhooks and tokens.

## Install

1. Copy `PShare-PS4-1.0.pkg` to an exFAT/FAT32 USB drive.
2. Install it with GoldHEN's package installer (Debug Settings > Game >
   Package Installer). If an earlier test build is installed and the
   installer won't replace it, delete it first. Settings are kept in
   `/data/pshare-ps4` and survive.
3. Launch **PShare** from the home screen.

## First setup

1. Go to **Settings** (L1/R1 to switch sections) and scan the QR code with
   your phone.
2. On the phone page, add a destination: a Discord webhook, a Telegram bot,
   Catbox (no account needed), Litterbox, a custom HTTP endpoint, or a
   **Folder** with the path `usb` to copy to a USB drive.
3. Back on the PS4, pick it as the default under **Destinations**.

The phone pages have no password. Use them on your home network only and
never forward port 9078 on your router.

## Using it

| Button | What it does |
| --- | --- |
| L1 / R1 | Switch sections (Captures, Auto upload, Destinations, History, Settings) |
| D-pad | Move around (hold to scroll) |
| Cross | Open / choose |
| Circle | Back / close |
| Square | Send the selected capture to the default destination; in History, send again |
| Triangle | Send to... (pick a destination) |
| L2 / R2 | All captures / Screenshots / Clips |
| Touchpad | Filter by game, then flip the order |
| Options | Menu: Refresh, Save diagnostics, Close PShare |

- **Automatic uploads:** turn on *Send automatically* in Auto upload. Every
  new screenshot and clip is sent as soon as it is saved, while PShare is open.
- **History** lists every attempt. Cross shows a QR code for the upload link,
  or the reason it failed.
- **Clips** play on your phone or PC: open a clip and choose *Play on your
  phone*.

## Differences from the PS5 version

- Automatic uploads run while PShare is open; closing PShare stops them. (On
  PS5 a background service keeps going.)
- Clips don't play on the TV; they play in the phone/PC browser.
- SMB network shares aren't available.
- No interface sounds or animated background.
- HTTP uploads are limited to 256 MB per file; use a USB folder destination
  for bigger clips.

## Files and support

PShare keeps its files in `/data/pshare-ps4/` on the console:
`config.ini` (settings, including webhooks and tokens — keep it private),
`history.tsv`, `pshared.log`.

If something goes wrong, choose **Save diagnostics** (Settings, or the
Options menu) with a USB drive connected. It writes
`PShare-PS4-diagnostics.txt` to the drive: folder access, network status,
controller details and the capture folder layout — no settings, tokens or
webhooks.

## Building from source

Linux/WSL with the OpenOrbis PS4 Toolchain **v0.5.4**
(`toolchain-llvm-18.tar.gz`) and clang/lld 18. The bundled PkgTool needs
host libssl 1.1 (for example Ubuntu focal's `libssl1.1` package, extracted
and pointed to with `LD_LIBRARY_PATH`).

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
make            # dist/IV0000-PSHR00004_00-PSHAREPS40000001.pkg
```

PC checks: `bash ps4/build-host.sh`, `python3 ps4/tests/service_integration.py`,
`bash ps4/test-body.sh` (needs SDL2 and libcurl development files).
`build/ps4-host/pshare --shots DIR` renders every screen to images.

Technical notes on the port (OpenOrbis stat layout, SDL2 controller mapping,
launch screen, rendering) are in `ps4/VALIDATION.md` and `CHANGELOG-PS4.md`.

## Credits and license

PShare by **StonedModder** (GPL-3.0-or-later); this port keeps that license
and the original notices (`LICENSE`, `THIRD_PARTY_NOTICES.md`). Uses the
OpenOrbis toolchain, SDL2, stb, the Nayuki QR code generator, libjbc by
bucanero, and the Inter, Montserrat and DejaVu fonts, under their own licenses.
