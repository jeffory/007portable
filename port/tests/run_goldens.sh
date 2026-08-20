#!/usr/bin/env bash
# Phase-0 golden regression suite (see the Portable Roadmap).
#
#   port/tests/run_goldens.sh            check against pinned goldens
#   port/tests/run_goldens.sh capture    (re)write the goldens
#
# Needs: the ROM at data/ge007.u.z64, a built binary (BIN, default
# build-port/ge007-port), Xvfb + llvmpipe for headless rendering.
# Goldens live in port/tests/goldens/ and are pinned against THIS machine's
# Mesa; frame comparisons are tolerance-based so minor rasterizer drift
# warns before it fails.
#
# What it covers:
#   selftest  ROM sha1, romCopy, 1172 inflate, 18 compiled-in AI array CRCs
#   boot      natural boot to the GoldenEye logo: every preprocess CRC
#             (text banks, ctl/seq audio banks, image tables, logo models),
#             two exact-ish golden frames (legal screen, N64 logo anim),
#             audio spectral profile of the boot music
#   dam       --stage dam: bg/stan/setup/model/briefing preprocess CRCs,
#             one settled in-game frame (fuzzy: gameplay RNG is not yet
#             deterministic - phase 2)
set -u
cd "$(dirname "$0")/../.."

BIN=${BIN:-build-port/ge007-port}
GOLD=port/tests/goldens
MODE=${1:-check}
T=$(mktemp -d)
trap 'rm -rf "$T"; [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null' EXIT

FAIL=0
note() { echo "== $*"; }
bad()  { echo "!! $*"; FAIL=1; }

[ -x "$BIN" ] || { echo "no binary at $BIN (set BIN=...)"; exit 2; }
[ -f data/ge007.u.z64 ] || { echo "no ROM at data/ge007.u.z64"; exit 2; }

# headless display: reuse $DISPLAY if set, else spawn Xvfb
if [ -z "${DISPLAY:-}" ]; then
    Xvfb :98 -screen 0 1280x1024x24 &>/dev/null &
    XVFB_PID=$!
    export DISPLAY=:98
    sleep 1
fi
export LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11
export PORT_NO_GAMEPAD=1

run() { # run <timeout> <args...>  (golden env is set by the caller)
    local secs=$1; shift
    timeout "$secs" "$BIN" "$@" >"$T/stdout.log" 2>"$T/stderr.log"
}

# ---- selftest ---------------------------------------------------------------
note "selftest"
PORT_CRC_TRACE=$T/self.txt run 120 --selftest
grep -q "selftest: PASS" "$T/stderr.log" || bad "selftest did not PASS"
grep "^CRCTRACE ai:" "$T/self.txt" > "$T/self_ai.txt" || true

# ---- natural boot to the GE logo -------------------------------------------
note "boot capture (~40s)"
PORT_CRC_TRACE=$T/boot.txt PORT_AUDIO_DUMP=$T/boot.pcm \
PORT_FRAME_DUMP=$T PORT_FRAME_DUMP_AT=120,400,500 PORT_FRAME_DUMP_EXIT=1 \
    run 180
[ -f "$T/frame_00500.ppm" ] || bad "boot: frame dump did not complete"

# ---- dam stage ---------------------------------------------------------------
note "dam capture (~30s)"
mkdir -p "$T/dam_"
PORT_CRC_TRACE=$T/dam.txt \
PORT_FRAME_DUMP=$T/dam_ PORT_FRAME_DUMP_AT=600 PORT_FRAME_DUMP_EXIT=1 \
    run 240 --stage dam
[ -f "$T/dam_/frame_00600.ppm" ] || bad "dam: frame dump did not complete"

if [ "$MODE" = capture ]; then
    mkdir -p "$GOLD"
    cp "$T/self_ai.txt"          "$GOLD/selftest_crc.txt"
    cp "$T/boot.txt"             "$GOLD/boot_crc.txt"
    cp "$T/dam.txt"              "$GOLD/dam_crc.txt"
    cp "$T/frame_00120.ppm"      "$GOLD/boot_frame_00120.ppm"
    cp "$T/frame_00400.ppm"      "$GOLD/boot_frame_00400.ppm"
    cp "$T/frame_00500.ppm"      "$GOLD/boot_frame_00500.ppm"
    cp "$T/dam_/frame_00600.ppm" "$GOLD/dam_frame_00600.ppm"
    python3 port/tests/pcmcheck.py --capture "$T/boot.pcm" "$GOLD/audio_profile.txt"
    { glxinfo -display "$DISPLAY" 2>/dev/null | grep -m1 "OpenGL version"; date -I; } \
        > "$GOLD/CAPTURED_WITH.txt" || true
    echo "goldens written to $GOLD"
    exit 0
fi

# ---- compare -----------------------------------------------------------------
note "compare CRC streams"
diff -u "$GOLD/selftest_crc.txt" "$T/self_ai.txt"  || bad "selftest AI CRCs drifted"
diff -u "$GOLD/boot_crc.txt"     "$T/boot.txt"     || bad "boot preprocess CRCs drifted"
diff -u "$GOLD/dam_crc.txt"      "$T/dam.txt"      || bad "dam preprocess CRCs drifted"

note "compare frames"
python3 port/tests/imgdiff.py "$GOLD/boot_frame_00120.ppm" "$T/frame_00120.ppm" 0.001 \
    || bad "legal screen frame drifted"
python3 port/tests/imgdiff.py "$GOLD/boot_frame_00400.ppm" "$T/frame_00400.ppm" 0.001 \
    || bad "logo animation frame drifted"
python3 port/tests/imgdiff.py "$GOLD/boot_frame_00500.ppm" "$T/frame_00500.ppm" 0.001 \
    || bad "boot frame 500 drifted"
python3 port/tests/imgdiff.py "$GOLD/dam_frame_00600.ppm" "$T/dam_/frame_00600.ppm" 0.025 \
    || bad "dam in-game frame drifted"

note "audio profile"
python3 port/tests/pcmcheck.py "$T/boot.pcm" "$GOLD/audio_profile.txt" \
    || bad "boot music spectral profile drifted"

if [ "$FAIL" = 0 ]; then
    echo "GOLDENS: PASS"
else
    echo "GOLDENS: FAIL"
fi
exit "$FAIL"
