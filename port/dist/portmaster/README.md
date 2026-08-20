# GoldenEye 007 — PortMaster package (work in progress)

A native port of the GoldenEye 007 decompilation for aarch64 Linux
handhelds (RG35xxSP-class and up, muOS / Knulli / stock CFWs with
PortMaster).

**No game assets are included.** You must provide your own US ROM:

    ports/ge007/data/ge007.u.z64

(z64 byte order, sha1 `abe01e4aeb033b6c0836819f549c791b26cfde83` — the
port verifies it and refuses anything else.)

## Status

- The binary builds and runs correct under emulation (see
  `port/tests/run_arm64.sh`); **it has not been tested on real handheld
  hardware yet.** Expect to iterate on GL context flags (GLES vs desktop
  GL — the current fast3d backend needs GL 3.0 / a GLES port, see the
  roadmap) and performance.
- Controls: native SDL_GameController (left stick = move, right stick =
  C buttons, RT = fire, LT = aim). The PortMaster hotkey combo quits.
- Saves land in `ports/ge007/data/eeprom.bin`.

## Packaging

`build_package.sh` assembles `GE007.zip` from a cross build:

    port/tests/run_arm64.sh build     # or a device-sysroot build
    port/dist/portmaster/build_package.sh
