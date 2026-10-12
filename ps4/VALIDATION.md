# Validation for 0.4 alpha

- New PS5-style UI: compiled for PS4 (OpenOrbis 0.5.4) and host; host
  screenshots of every page compared against docs/screens; host smoke,
  service integration, body and stat tests pass; PKG validates and its
  eboot.bin matches the build. Not yet run on a console; software rendering
  speed on the PS4 CPU is unmeasured.

# Validation for 0.3 alpha

## Console-tested (user's PS4, 0.2 alpha, 2026-10-12)
- Launches; GoldHEN kexec available; /user, /data, /mnt/usb0, /mnt/usb1
  mounts all OK; sysmodules and sceNetInit OK; IP found; native HTTP/SSL
  initialised. Pad: "Dualshock4", 20 buttons, 6 axes, 1 hat, GUID
  01800000000000000000000000000000 (as predicted). Buttons work; screenshots
  open on the TV and match the PS4 Capture Gallery.
- Capture layout: av_contents/photo/NPXS20001/CUSAxxxxx/<3 hex>/YYYYMMDD_HHMMSS_xxxxxxxx.jpg
- Every send failed "Capture changed or read failed": st_size was the
  kernel's st_blocks (see below). Fixed in 0.3; not yet re-tested on console.

## 0.3 changes, compile/host-tested
- OpenOrbis struct stat offsets (computed): size @0x50, mtim @0x30, mode is
  32-bit @8. PS4 kernel (FreeBSD 9) layout: size @72, blocks @80, mtim @40,
  mode 16-bit @8. ps4/ps4_stat.h is force-included into all PS4 C++ files and
  routes stat/fstat/lstat through translating wrappers (ps4/ps4_stat.cpp).
  Verified no other object references stat/fstat directly.
- libkernel provides `stat` and `_fstat` (kernel layout); OpenOrbis libc's
  `lstat` goes to an fstatat stub that returns ENOSYS.
- ps4/tests/stat_tests.cpp (ASan/UBSan) checks the translation.
- Host smoke, service integration and body tests pass; PKG validates; packaged
  eboot.bin matches the build.

# Earlier: validation for 0.2 alpha

Rebuilt from source in a fresh container on 2026-10-12.

## Compile-tested
- OpenOrbis PS4 Toolchain v0.5.4 (`toolchain-llvm-18.tar.gz`, SHA-256
  3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526) with
  clang/lld 18. Native compile and link succeeded with no errors.
- create-fself produced eboot.bin; PkgTool.Core built the PKG (host libssl 1.1
  from Ubuntu focal `libssl1.1_1.1.1f-1ubuntu2`, via LD_LIBRARY_PATH).
- pkg_validate: every hash and signature reported OK.
- pkg_extract: the packaged eboot.bin is byte-identical to the build output.
- New imports resolve against the toolchain stubs: sigaction,
  sceHttpsSetSslCallback, sceNetCtlInit/GetInfo, sceKernelSendNotificationRequest.

## Host-tested (Linux, SDL dummy video driver)
- Startup, render, diagnostics file, shutdown smoke: passed.
- `ps4/tests/service_integration.py`: phone setup, gallery, viewer, byte
  range, queued folder copy, history, shutdown: passed.
- `ps4/test-body.sh` (ASan/UBSan): multipart/raw bodies, exact pre-sized
  buffer, quoting, cancellation, 256 MiB refusal, CUSA/MP4: passed.
  LeakSanitizer disabled (container limitation).

## Found by inspecting the OpenOrbis SDL2 library (not console-tested)
- `PS4_JoystickGetDeviceGUID` writes each field to the same offset, so the
  GUID is 01800000000000000000000000000000 and matches no controller mapping;
  0.1's GameController input could never open on a PS4.
- The PS4 joystick reports 20 buttons, 6 axes, 1 hat. Button table:
  0 Cross, 1 Circle, 2 Square, 3 Triangle, 4 L1, 5 R1, 9 Options, 11 L3,
  12 R3, 13-16 D-pad up/down/left/right, 17 touchpad, 18 L2, 19 R2.
  Share/PS are not reported.

## NOT tested
- Nothing has run on a physical PS4. Launch, GoldHEN kexec probe, libjbc
  mounts, controller input, capture access, TLS/HTTP uploads, notifications
  and USB copies are all unverified on hardware and on firmware 11.02.
- The TLS callback assumes a negative return rejects the certificate (the
  OpenOrbis sample returns a positive value to accept); verify on console.

Package SHA-256: see PACKAGE-SHA256.txt
