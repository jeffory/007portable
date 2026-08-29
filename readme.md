# GoldenEye 007 — portable PC port

A native PC port of the GoldenEye 007 decompilation, built for **portability
first**. This is not an attempt at the "ultimate" feature-rich PC port — the
goal is one clean, device-agnostic codebase that runs the game faithfully on
as many targets as possible: Linux (x86-64 and aarch64), RG35xxSP-class
handhelds via PortMaster, Android, Windows, and macOS. Enhancements (widescreen,
>60 fps interpolation, etc.) come last, and only where they don't compromise
that goal.

The approach follows the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark):
the original game logic compiles as-is, the N64 plumbing (threads, TLB pager,
VI/PI, scheduler, RSP) is replaced by a small platform layer in [`port/`](./port/),
rendering goes through the fast3d renderer, and audio through a software
implementation of the RSP audio microcode.

**This repository ships no game assets.** A GoldenEye 007 (U) ROM is required
at runtime; it is verified by SHA-1 before loading.

## Status

The game is playable end to end: full intro, menus, missions with AI, audio,
saves, mouse/keyboard and gamepad input. It builds and runs on Linux
(i686, x86-64, aarch64 — the latter qemu-verified bit-identical), Windows
(Wine-verified), and Android (GLES3, played on an Anbernic RG Slide:
frontend, stage load, combat and the mission debrief, driven over adb by
`port/tests/run_android.sh play`); macOS (Apple Silicon) builds in CI but
has not yet been run on real hardware. Every push builds all six targets and uploads the
binaries as artifacts — grab them from any green run on the
[Actions](../../actions) page.

| Phase | Scope | Status |
|---|---|---|
| 0 | Golden regression suite (preprocess CRCs, reference frames, PCM check) | done |
| 1 | Remove all N64-only code, default `-O2`, UBSan-clean | done |
| 2 | Full regression net + CI matrix (deterministic replay + exact goldens; 6-target CI) | done |
| 3 | Native 64-bit memory model — PIE-safe; unlocked ARM, Android and macOS | done |
| 4 | GLES3 renderer path, audio thread, portable file paths, sky renderer | done¹ |
| 5 | Packaging: AppImage, PortMaster zip, Android APK, Windows build, macOS build, CI artifacts | done² |
| 6 | Profiling and enhancements (LTO, widescreen, >60 fps interpolation) | planned |

¹ the converted-asset disk cache was dropped (load times don't need it).
The two cosmetic items once tracked here are closed: fast3d now models
`ALPHA_CVG_SEL`, so the RSP fog shade-alpha overwrite is accurate and on
by default, and the aim sight measures pixel-exact at every window scale
(the "oversized rings" note predated the texture fixes).
² machine-side complete; the APK is validated on real hardware (RG Slide),
the PortMaster zip still awaits a handheld, and the macOS binary a Mac.

## N64 support

This tree no longer builds N64 ROMs. The last byte-match-verified decomp
build (IDO toolchain, `make`-based, matching all three regions) is preserved
at the [`n64-final`](../../tree/n64-final) tag — the setup and structure
guides in [`docs/`](./docs/) describe that build and are kept for reference.
The [Style Guide](./docs/StyleGuide.md) still applies to game code.

## Building

Requires CMake, Ninja, a C/C++ toolchain, and SDL2 development headers.

```sh
# native build (Linux x86-64 / aarch64, macOS — the primary configuration)
cmake -B build -G Ninja
ninja -C build

# 32-bit x86 build (needs multilib + 32-bit SDL2)
cmake -B build-port -G Ninja --toolchain port/cmake/i686-toolchain.cmake
ninja -C build-port
```

Cross targets: `port/cmake/aarch64-toolchain.cmake` and
`port/cmake/mingw64-toolchain.cmake` (Windows), with container-based
build+smoke scripts in `port/tests/` (`run_arm64.sh`, `run_win64.sh`).
The Android APK lives in [`android/`](./android/) (`fetch_sdl.sh`, then
`gradle assembleDebug`), and the PortMaster zip assembles via
`port/dist/portmaster/build_package.sh`. On macOS, `brew install ninja
sdl2 gnu-sed` first.

Place the ROM at `data/ge007.u.z64` and run `./build/ge007-port`
(without a local `data/` folder the port uses per-user directories —
`~/.local/share/ge007/`, `%AppData%\ge007\`, or the Android app's
external-files dir). `--selftest` verifies the asset pipeline without
opening a window. The ROM-gated regression suites are
`port/tests/run_goldens.sh` (bit-exact frames/PCM/state hashes) plus the
cross-target smoke scripts above.

See [`port/README.md`](./port/README.md) for controls, configuration
(`~/.config/ge007/input.ini`), and the debug environment variables.
