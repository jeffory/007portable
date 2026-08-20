# GoldenEye 007 — portable PC port

A native PC port of the GoldenEye 007 decompilation, built for **portability
first**. This is not an attempt at the "ultimate" feature-rich PC port — the
goal is one clean, device-agnostic codebase that runs the game faithfully on
as many targets as possible: Linux (x86-64 and aarch64), RG35xxSP-class
handhelds via PortMaster, Android, and Windows. Enhancements (widescreen,
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

The game is playable end to end on Linux: full intro, menus, missions with
AI, audio, saves, mouse/keyboard and gamepad input. Current work follows the
portability roadmap:

| Phase | Scope | Status |
|---|---|---|
| 0 | Golden regression suite (preprocess CRCs, reference frames, PCM check) | done |
| 1 | Remove all N64-only code, default `-O2`, UBSan-clean | done |
| 2 | Full regression net + CI matrix (deterministic replay, x86-64 / aarch64 / NDK) | in progress |
| 3 | Native 64-bit memory model — PIE-safe, no low-4GB tricks; unlocks ARM/Android | next |
| 4 | GLES2/3 renderer path, audio thread, portable file paths, asset cache | planned |
| 5 | Packaging: AppImage, PortMaster zip, Android APK, Windows build | planned |
| 6 | Profiling and enhancements | planned |

## N64 support

This tree no longer builds N64 ROMs. The last byte-match-verified decomp
build (IDO toolchain, `make`-based, matching all three regions) is preserved
at the [`n64-final`](../../tree/n64-final) tag — the setup and structure
guides in [`docs/`](./docs/) describe that build and are kept for reference.
The [Style Guide](./docs/StyleGuide.md) still applies to game code.

## Building

Requires CMake, Ninja, GCC, and SDL2 development headers.

```sh
# 32-bit build (currently the fully-featured one; needs multilib + 32-bit SDL2)
cmake -B build-port -G Ninja --toolchain port/cmake/i686-toolchain.cmake
ninja -C build-port

# native x86-64 build (menus + world render; prop/audio expansion in progress)
cmake -B build-64 -G Ninja
ninja -C build-64
```

Place the ROM at `data/ge007.u.z64` and run `./build-port/ge007-port`.
`--selftest` verifies the asset pipeline without opening a window.

See [`port/README.md`](./port/README.md) for controls, configuration
(`~/.config/ge007/input.ini`), and the debug environment variables.
