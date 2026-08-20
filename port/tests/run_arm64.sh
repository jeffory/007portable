#!/usr/bin/env bash
# aarch64 cross-build + emulated smoke test (Portable Roadmap phase 3/5).
#
# Builds the port for aarch64 Linux in a podman container (Debian cross
# toolchain, headless null backend — no aarch64 SDL sysroot yet) and runs
# it under qemu-user: selftest, a deterministic headless boot and a Dam
# load, comparing the preprocess CRC streams against the pinned goldens.
#
#   port/tests/run_arm64.sh            build + smoke test
#   port/tests/run_arm64.sh build      build only
#   port/tests/run_arm64.sh capture    (re)pin the arm64 golden streams
#
# Notes:
# - the image is built once and cached as localhost/ge007-cross.
# - CRC streams are compared against arm64's own pinned goldens
#   (goldens/arm64_*.txt): the null backend's stream is a strict subset of
#   the SDL build's (no cmidi — music never plays without a device; no bg
#   line — that trace is 32-bit-only; no render-triggered lazy model
#   loads). Cross-arch equivalence of the common lines vs x86 was
#   verified when these were first pinned.
# - gameplay STATEHASHes match x86-64 through the early mission (tick
#   600 verified identical); later ticks drift via libm differences, so
#   they are not compared here.
set -ue
cd "$(dirname "$0")/../.."

IMG=localhost/ge007-cross
if ! podman image exists "$IMG"; then
    podman build -t ge007-cross -f - <<'EOF'
FROM docker.io/library/debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libc6-dev-arm64-cross \
    cmake ninja-build python3 qemu-user-static gdb-multiarch \
    ca-certificates && rm -rf /var/lib/apt/lists/*
EOF
fi

RUN="podman run --rm -v $PWD:/work:z -w /work $IMG"

$RUN bash -c "cmake -B build-arm64 -G Ninja --toolchain port/cmake/aarch64-toolchain.cmake >/dev/null && ninja -C build-arm64"

if [ "${1:-}" = build ]; then
    exit 0
fi

[ -f data/ge007.u.z64 ] || { echo "no ROM at data/ge007.u.z64"; exit 2; }

QEMU="timeout -s KILL 900 qemu-aarch64-static -L /usr/aarch64-linux-gnu"
FAIL=0

echo "== arm64 selftest"
$RUN bash -c "$QEMU build-arm64/ge007-port --selftest 2>&1 | tail -1" | grep -q PASS \
    || { echo "!! selftest failed"; FAIL=1; }

echo "== arm64 deterministic boot (qemu, ~1min)"
$RUN bash -c "cp /work/port/tests/fixtures/eeprom.bin /work/build-arm64/eeprom_test.bin && PORT_DETERMINISTIC=1 PORT_EEPROM=/work/build-arm64/eeprom_test.bin PORT_CRC_TRACE=/work/build-arm64/crc_boot.txt \
    PORT_STATE_HASH=600 PORT_STATE_HASH_EXIT=1 $QEMU build-arm64/ge007-port" >/dev/null 2>&1 || true
if [ "${1:-}" = capture ]; then cp build-arm64/crc_boot.txt port/tests/goldens/arm64_boot_crc.txt; else
diff port/tests/goldens/arm64_boot_crc.txt build-arm64/crc_boot.txt \
    || { echo "!! boot CRC stream drifted"; FAIL=1; }
fi

echo "== arm64 dam load (qemu, ~2min)"
$RUN bash -c "cp /work/port/tests/fixtures/eeprom.bin /work/build-arm64/eeprom_test.bin && PORT_DETERMINISTIC=1 PORT_EEPROM=/work/build-arm64/eeprom_test.bin PORT_AUTOSTART=1 PORT_CRC_TRACE=/work/build-arm64/crc_dam.txt \
    PORT_STATE_HASH=900 PORT_STATE_HASH_EXIT=1 $QEMU build-arm64/ge007-port --stage dam" >/dev/null 2>&1 || true
if [ "${1:-}" = capture ]; then cp build-arm64/crc_dam.txt port/tests/goldens/arm64_dam_crc.txt; echo "arm64 goldens pinned"; exit 0; else
diff port/tests/goldens/arm64_dam_crc.txt build-arm64/crc_dam.txt \
    || { echo "!! dam CRC stream drifted"; FAIL=1; }
fi

if [ "$FAIL" = 0 ]; then
    echo "ARM64: PASS"
else
    echo "ARM64: FAIL"
fi
exit "$FAIL"
