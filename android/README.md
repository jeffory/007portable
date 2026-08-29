# GE007 — Android build (arm64-v8a, GLES3)

Builds the whole port as `libmain.so` inside an SDL2 APK. Played on an
Anbernic RG Slide: frontend, stage load, combat and the mission debrief.

## Build

    android/fetch_sdl.sh                # SDL2 source + Java glue (once)
    cd android && gradle assembleDebug  # needs Android SDK + NDK, JDK 17-21

A JDK newer than 21 currently fails in `jlink` while transforming
`core-for-system-modules.jar`; point `JAVA_HOME` at a 17/21 install.

`local.properties` wants `sdk.dir=$HOME/Android/Sdk`. Output:
`app/build/outputs/apk/debug/app-debug.apk`.

## ROM

No assets ship. Put the US ROM (z64, sha1
`abe01e4aeb033b6c0836819f549c791b26cfde83`) where the app looks:

    adb push ge007.u.z64 /sdcard/Android/data/com.ge007.port/files/ge007.u.z64

(paths.c prefers SDL's Android external storage dir; a SAF picker is
future work.)

## Command line

An Android activity gets no argv and no environment, so `GEActivity`
takes SDL_main's command line from the launch intent — the only way to
reach the port's flags and its `PORT_*` debug knobs on a device:

    adb shell am force-stop com.ge007.port
    adb shell "am start -n com.ge007.port/.GEActivity \
        --es args '--stage dam --env PORT_POS_TRACE=60'"

`--es args "..."` is split on whitespace; `--esa args a,b,c` is taken
verbatim for an argument that contains one. `--env NAME=VALUE` (repeat
as needed) sets an environment variable before anything reads one, so
every knob in port/README.md works here too — including
`--env PORT_FULLSCREEN=0` to get the system bars back.

**The force-stop matters.** The activity is `singleInstance`: starting it
again while it runs delivers `onNewIntent`, never re-enters SDL_main, and
silently ignores the new arguments.

## Testing on a device

    port/tests/run_android.sh            # install, push the ROM, drive into the Dam
    port/tests/run_android.sh play 100   # ...then walk, turn and shoot

    ARGS='--stage dam' PRESSES=0 \
        port/tests/run_android.sh play 100   # skip the frontend entirely

`play` is the one that reaches the AI, and it reports the input that
killed the app plus how to symbolise the tombstone. Input note: `adb
shell input keyevent` sends the down and the up in the same instant and
the port samples the keyboard once per poll, so plain taps are invisible
— `--longpress` is required.

## Notes

- Fullscreen is immersive: the manifest's `Theme.NoTitleBar.Fullscreen`
  drops the title and status bars, port/src/video.c defaults the SDL
  window to fullscreen (which makes SDLActivity turn on immersive sticky
  and hide the navigation bar), and GEActivity lets the window into the
  display cutout. `PORT_FULLSCREEN=0` still forces a windowed surface,
  but nothing on Android can set an environment variable for an activity
  launched from the launcher or `am start`, so in practice fullscreen is
  unconditional there — the flag is for the desktop builds.
- Controls: SDL_GameController (pair a gamepad); touch controls are
  future work. The control style follows the device in use, so a pad gets
  the game's own setting (1.1 Honey by default) rather than the 1.2 that
  mouselook forces — see port/README.md.
- The GL context comes up as GLES3 via SDL; fast3d's `gl_es` path is the
  one exercised by port/tests on desktop llvmpipe and qemu-aarch64.
