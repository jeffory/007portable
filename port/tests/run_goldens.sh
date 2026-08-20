#!/usr/bin/env bash
# Golden regression suite (Portable Roadmap phases 0+2).
#
#   port/tests/run_goldens.sh            check against pinned goldens
#   port/tests/run_goldens.sh capture    (re)write the goldens
#
# Needs: the ROM at data/ge007.u.z64, a built binary (BIN, default
# build-port/ge007-port), Xvfb + llvmpipe for headless rendering.
#
# Everything runs under PORT_DETERMINISTIC=1 (virtual clock: one retrace
# per sched pump), so runs are BIT-REPRODUCIBLE and flat out — the whole
# suite takes seconds. All comparisons are exact:
#
#   selftest  ROM sha1, romCopy, 1172 inflate, 18 compiled-in AI array CRCs
#   boot      natural boot: every preprocess CRC, three exact frames
#             (legal screen, logo anim x2), exact PCM of the boot music
#   dam       --stage dam + PORT_AUTOSTART (cinema skip): every preprocess
#             CRC (lazy model loads included), one exact in-game frame,
#             gameplay STATEHASHes at ticks 600/1200/1800
#
# Frame pixels depend on this machine's Mesa/llvmpipe (goldens/CAPTURED_WITH
# records it); CRC streams, PCM and STATEHASHes are renderer-independent.
# On a frame mismatch the script reports the histogram distance (imgdiff
# --hist) to separate rasterizer drift (~0.00x) from real breakage.
set -u
cd "$(dirname "$0")/../.."

BIN=${BIN:-build-port/ge007-port}
GOLD=${GOLD:-port/tests/goldens}   # 64-bit: GOLD=port/tests/goldens64 BIN=build-64/ge007-port
MODE=${1:-check}
T=$(mktemp -d)
trap 'rm -rf "$T"; [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null' EXIT

FAIL=0
note() { echo "== $*"; }
bad()  { echo "!! $*"; FAIL=1; }

[ -x "$BIN" ] || { echo "no binary at $BIN (set BIN=...)"; exit 2; }
[ -f data/ge007.u.z64 ] || { echo "no ROM at data/ge007.u.z64"; exit 2; }

if [ -z "${DISPLAY:-}" ]; then
    # unique display per invocation: back-to-back runs (e.g. capture of two
    # golden sets) race on a fixed :98 — the second Xvfb loses the lock and
    # every game run silently fails
    XVFB_D=$((90 + $$ % 60))
    Xvfb ":$XVFB_D" -screen 0 1280x1024x24 &>/dev/null &
    XVFB_PID=$!
    export DISPLAY=":$XVFB_D"
    sleep 1
fi
export LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11
export PORT_NO_GAMEPAD=1 PORT_DETERMINISTIC=1

# Hermetic save state: game settings live in the EEPROM and leak into CRCs,
# STATEHASHes and frames. Run against a throwaway copy of the pinned
# fixture, never the player's real data/eeprom.bin.
cp port/tests/fixtures/eeprom.bin "$T/eeprom.bin"
export PORT_EEPROM="$T/eeprom.bin"

run() { # run <timeout> <args...>  (golden env set by the caller)
    local secs=$1; shift
    timeout "$secs" "$BIN" "$@" >"$T/stdout.log" 2>"$T/stderr.log"
}

cmpfile() { # cmpfile <golden> <got> <label> [ppm]
    if ! cmp -s "$1" "$2"; then
        bad "$3 drifted"
        if [ "${4:-}" = ppm ] && [ -f "$1" ] && [ -f "$2" ]; then
            python3 port/tests/imgdiff.py --hist "$1" "$2" 0 2>/dev/null | sed 's/^/   /'
        fi
    fi
}

# ---- selftest ---------------------------------------------------------------
note "selftest"
PORT_CRC_TRACE=$T/self.txt run 120 --selftest
grep -q "selftest: PASS" "$T/stderr.log" || bad "selftest did not PASS"
grep "^CRCTRACE ai:" "$T/self.txt" > "$T/self_ai.txt" || true

# ---- natural boot to the GE logo -------------------------------------------
note "boot (deterministic)"
PORT_CRC_TRACE=$T/boot.txt PORT_AUDIO_DUMP=$T/boot.pcm \
PORT_FRAME_DUMP=$T PORT_FRAME_DUMP_AT=120,400,500 PORT_FRAME_DUMP_EXIT=1 \
    run 180
[ -f "$T/frame_00500.ppm" ] || bad "boot: frame dump did not complete"
md5sum < "$T/boot.pcm" | awk '{print $1}' > "$T/boot_pcm.md5"

# ---- dam stage ---------------------------------------------------------------
note "dam (deterministic, autostart)"
mkdir -p "$T/dam_"
PORT_CRC_TRACE=$T/dam.txt PORT_AUTOSTART=1 \
PORT_STATE_HASH=600,1200,1800 \
PORT_FRAME_DUMP=$T/dam_ PORT_FRAME_DUMP_AT=900 PORT_FRAME_DUMP_EXIT=1 \
    run 240 --stage dam
[ -f "$T/dam_/frame_00900.ppm" ] || bad "dam: frame dump did not complete"
grep "^STATEHASH" "$T/stderr.log" > "$T/dam_statehash.txt" || true

if [ "$MODE" = capture ]; then
    mkdir -p "$GOLD"
    cp "$T/self_ai.txt"          "$GOLD/selftest_crc.txt"
    cp "$T/boot.txt"             "$GOLD/boot_crc.txt"
    cp "$T/dam.txt"              "$GOLD/dam_crc.txt"
    cp "$T/boot_pcm.md5"         "$GOLD/boot_pcm.md5"
    cp "$T/dam_statehash.txt"    "$GOLD/dam_statehash.txt"
    cp "$T/frame_00120.ppm"      "$GOLD/boot_frame_00120.ppm"
    cp "$T/frame_00400.ppm"      "$GOLD/boot_frame_00400.ppm"
    cp "$T/frame_00500.ppm"      "$GOLD/boot_frame_00500.ppm"
    cp "$T/dam_/frame_00900.ppm" "$GOLD/dam_frame_00900.ppm"
    python3 port/tests/pcmcheck.py --capture "$T/boot.pcm" "$GOLD/audio_profile.txt"
    { glxinfo -display "$DISPLAY" 2>/dev/null | grep -m1 "OpenGL version"; date -I; } \
        > "$GOLD/CAPTURED_WITH.txt" || true
    echo "goldens written to $GOLD"
    exit 0
fi

# ---- compare (all exact) -----------------------------------------------------
note "compare"
diff -u "$GOLD/selftest_crc.txt"  "$T/self_ai.txt"       || bad "selftest AI CRCs drifted"
diff -u "$GOLD/boot_crc.txt"      "$T/boot.txt"          || bad "boot preprocess CRCs drifted"
diff -u "$GOLD/dam_crc.txt"       "$T/dam.txt"           || bad "dam preprocess CRCs drifted"
diff -u "$GOLD/dam_statehash.txt" "$T/dam_statehash.txt" || bad "dam gameplay STATEHASH drifted"
cmpfile "$GOLD/boot_pcm.md5"      "$T/boot_pcm.md5"         "boot music PCM"
cmpfile "$GOLD/boot_frame_00120.ppm" "$T/frame_00120.ppm"   "legal screen frame"      ppm
cmpfile "$GOLD/boot_frame_00400.ppm" "$T/frame_00400.ppm"   "logo frame 400"          ppm
cmpfile "$GOLD/boot_frame_00500.ppm" "$T/frame_00500.ppm"   "logo frame 500"          ppm
cmpfile "$GOLD/dam_frame_00900.ppm"  "$T/dam_/frame_00900.ppm" "dam in-game frame"    ppm

# tolerant spectral check kept as a sanity net (catches a whine even if the
# exact goldens were just re-pinned wrong)
python3 port/tests/pcmcheck.py "$T/boot.pcm" "$GOLD/audio_profile.txt" \
    || bad "boot music spectral profile drifted"

if [ "$FAIL" = 0 ]; then
    echo "GOLDENS: PASS"
else
    echo "GOLDENS: FAIL"
fi
exit "$FAIL"
