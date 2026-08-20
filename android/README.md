# GE007 — Android build (arm64-v8a, GLES3)

Skeleton per the portable roadmap phase 5. Builds the whole port as
`libmain.so` inside an SDL2 APK. **Untested on real devices.**

## Build

    android/fetch_sdl.sh                # SDL2 source + Java glue (once)
    cd android && gradle assembleDebug  # needs Android SDK + NDK, JDK 17+

`local.properties` wants `sdk.dir=$HOME/Android/Sdk`. Output:
`app/build/outputs/apk/debug/app-debug.apk`.

## ROM

No assets ship. Put the US ROM (z64, sha1
`abe01e4aeb033b6c0836819f549c791b26cfde83`) where the app looks:

    adb push ge007.u.z64 /sdcard/Android/data/com.ge007.port/files/ge007.u.z64

(paths.c prefers SDL's Android external storage dir; a SAF picker is
future work.)

## Notes

- Controls: SDL_GameController (pair a gamepad); touch controls are
  future work.
- The GL context comes up as GLES3 via SDL; fast3d's `gl_es` path is the
  one exercised by port/tests on desktop llvmpipe and qemu-aarch64.
