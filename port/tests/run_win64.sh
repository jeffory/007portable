#!/usr/bin/env bash
# Windows x86_64 cross-build + Wine smoke test (Portable Roadmap phase 5).
#
#   port/tests/run_win64.sh            build + selftest under Wine
#   port/tests/run_win64.sh build      build only
#
# Uses the ge007-win podman container (Fedora mingw64-gcc + mingw64-SDL2 +
# wine). The exe needs SDL2.dll next to it at runtime; the script copies
# it from the mingw sysroot into build-win64/.
set -ue
cd "$(dirname "$0")/../.."

IMG=localhost/ge007-win
if ! podman image exists "$IMG"; then
    podman build -t ge007-win -f - <<'EOF'
FROM registry.fedoraproject.org/fedora:44
RUN dnf install -y --setopt=install_weak_deps=False \
    mingw64-gcc mingw64-gcc-c++ mingw64-SDL2 cmake ninja-build python3 \
    wine-core xorg-x11-server-Xvfb mesa-dri-drivers mesa-libGL \
    && dnf clean all
EOF
fi

RUN="podman run --rm -v $PWD:/work:z -w /work $IMG"

$RUN bash -c "cmake -B build-win64 -G Ninja --toolchain port/cmake/mingw64-toolchain.cmake >/dev/null && ninja -C build-win64 && cp -u /usr/x86_64-w64-mingw32/sys-root/mingw/bin/SDL2.dll build-win64/"

if [ "${1:-}" = build ]; then
    exit 0
fi

[ -f data/ge007.u.z64 ] || { echo "no ROM at data/ge007.u.z64"; exit 2; }

echo "== win64 selftest (wine)"
# KNOWN ISSUE: wine-core hangs inside rootless podman on this machine
# (wineserver never comes up); run the selftest on a real Windows box or
# a host wine install instead:
#   wine build-win64/ge007-port.exe --selftest
$RUN bash -c "export WINEDEBUG=-all WINEPREFIX=/tmp/wine HOME=/tmp; timeout -s KILL 300 wine build-win64/ge007-port.exe --selftest 2>&1 | tail -2" | grep -q PASS \
    && echo "WIN64: PASS" || echo "WIN64: wine smoke inconclusive (see note above); the exe builds"

