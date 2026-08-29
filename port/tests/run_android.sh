#!/usr/bin/env bash
# Android on-device smoke test: install, then drive the frontend all the way
# into a mission with adb alone, and report what happened.
#
#   port/tests/run_android.sh              install the debug APK and drive
#   port/tests/run_android.sh drive        drive whatever is installed
#   port/tests/run_android.sh play [n]     drive, then PLAY: walk, turn and
#                                          shoot for n inputs (default 60)
#
# Env: SERIAL (adb device, default: the only one attached), APK, PRESSES,
#      ROM (default data/ge007.u.z64), ARGS (command line for SDL_main).
#
# ARGS goes to the activity as an intent extra, which is the only way to
# reach the port's flags on Android - an activity gets no argv and no
# environment. Booting straight into a stage skips the frontend entirely:
#
#   ARGS='--stage dam --env PORT_POS_TRACE=60' PRESSES=0 \
#       port/tests/run_android.sh play 60
#
# `play` is the one that reaches the AI: firing brings guards, and a guard
# deciding to attack is a different code path from anything the frontend
# or a stage load touches (it killed the port on arm64 for a while - see
# chrlvInitActAttack). It reports the input that killed the app.
#
# THE THING TO KNOW ABOUT INPUT: `adb shell input keyevent <code>` sends the
# down and the up in the same instant. The port samples SDL_GetKeyboardState
# once per poll, so a press that begins and ends between two polls is never
# seen and the game ignores it entirely. `--longpress` spans several polls
# and registers every time. The same applies to xdotool on the desktop: send
# `keydown`, sleep ~0.3s, `keyup` - `xdotool key` is usually too quick.
#
# START alone walks the whole frontend: legal screen -> logos -> gunbarrel ->
# cast -> file select -> mode -> mission -> briefing -> mission load. Seven
# long presses is enough for the Dam; a stage further down the list needs
# more, or cursor moves (KEYCODE_DPAD_* long-pressed) to pick it.
set -u
cd "$(dirname "$0")/../.."

PKG=com.ge007.port
ACT=$PKG/.GEActivity
FILES=/sdcard/Android/data/$PKG/files
APK=${APK:-android/app/build/outputs/apk/debug/app-debug.apk}
ROM=${ROM:-data/ge007.u.z64}
PRESSES=${PRESSES:-7}
ARGS=${ARGS:-}
MODE=${1:-install}
PLAYFOR=${2:-60}

if [ -n "${SERIAL:-}" ]; then
    ADB="adb -s $SERIAL"
else
    ADB="adb"
fi

$ADB get-state >/dev/null 2>&1 || { echo "no device (adb get-state failed)"; exit 2; }

if [ "$MODE" = install ]; then
    [ -f "$APK" ] || { echo "no APK at $APK (build it: cd android && gradle assembleDebug)"; exit 2; }
    echo "== installing $APK"
    $ADB install -r "$APK" >/dev/null || {
        # a locally built APK and a CI-built one are signed with different
        # debug keys; the install is refused until the old one is gone
        echo "   signature mismatch, reinstalling"
        $ADB uninstall $PKG >/dev/null
        $ADB install "$APK" >/dev/null || { echo "!! install failed"; exit 1; }
    }
    if [ -f "$ROM" ]; then
        $ADB shell mkdir -p $FILES
        $ADB shell "[ -f $FILES/ge007.u.z64 ]" || { echo "== pushing ROM"; $ADB push "$ROM" $FILES/ge007.u.z64 >/dev/null; }
    fi
fi

echo "== launching"
$ADB shell am force-stop $PKG
$ADB logcat -c
if [ -n "$ARGS" ]; then
    echo "   args: $ARGS"
    $ADB shell "am start -n $ACT --es args '$ARGS'" >/dev/null 2>&1
else
    $ADB shell am start -n $ACT >/dev/null 2>&1
fi
sleep 25

[ "$PRESSES" -gt 0 ] && echo "== driving the frontend ($PRESSES x START)"
for i in $(seq 1 "$PRESSES"); do
    $ADB shell input keyevent --longpress 66 >/dev/null 2>&1
    sleep 1.5
done
sleep 10

PID=$($ADB shell "pidof $PKG || echo GONE" | tr -d '\r')
if [ "$PID" = GONE ]; then
    echo "!! the game died"
    $ADB logcat -d -b crash -v brief | grep -E "signal|fault addr|#0[0-5] pc" | tail -8
    echo "   symbolise with: llvm-addr2line -f -C -e android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmain.so <offset>"
    echo "   (the APK's copy is stripped; that one is not)"
    exit 1
fi

echo "== still running (pid $PID)"
$ADB shell "tail -3 $FILES/stderr.log" 2>/dev/null | sed 's/^/   /'

if [ "$MODE" = play ]; then
    echo "== playing ($PLAYFOR inputs: W walks, SPACE fires, LEFT turns)"
    $ADB logcat -c
    i=0
    while [ "$i" -lt "$PLAYFOR" ]; do
        i=$((i + 1))
        case $((i % 5)) in
            0) K=21 ;;   # DPAD_LEFT -> stick left
            2|4) K=62 ;; # SPACE     -> Z, fire
            *) K=51 ;;   # W         -> move
        esac
        $ADB shell input keyevent --longpress $K >/dev/null 2>&1
        if [ "$($ADB shell "pidof $PKG || echo GONE" | tr -d '\r')" = GONE ]; then
            echo "!! the game died after $i inputs (last keycode $K)"
            $ADB logcat -d -b crash -v brief | grep -E "signal|fault addr|#0[0-9] pc" | tail -12
            echo "   symbolise with: llvm-addr2line -f -C -e android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmain.so <offset>"
            exit 1
        fi
    done
    echo "   survived $PLAYFOR inputs"
fi

echo "ANDROID: PASS"
