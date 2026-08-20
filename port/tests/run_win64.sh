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

$RUN bash -c "cmake -B build-win64 -G Ninja --toolchain port/cmake/mingw64-toolchain.cmake >/dev/null && ninja -C build-win64 && cp -u /usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll build-win64/"

# Ship the OFFICIAL libsdl.org SDL2.dll: Fedora's mingw64-SDL2 build
# deadlocks the loader under Wine 11 (loader_section hang before main —
# reproduced with a plain hello-world linking it). The official build is
# fine, and real Windows may work with either.
if [ ! -f build-win64/SDL2.dll ] || [ "$(stat -c%s build-win64/SDL2.dll)" -lt 1000000 ]; then
    curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9-win32-x64.zip -o build-win64/sdl-official.zip
    (cd build-win64 && unzip -o -q sdl-official.zip SDL2.dll && rm sdl-official.zip)
fi

if [ "${1:-}" = build ]; then
    exit 0
fi

[ -f data/ge007.u.z64 ] || { echo "no ROM at data/ge007.u.z64"; exit 2; }

echo "== win64 selftest (host wine)"
# wine-core inside rootless podman hangs; the HOST wine works fine.
export WINEPREFIX="${WINEPREFIX:-$HOME/.cache/ge007-wine}" WINEDEBUG=-all
timeout -s KILL 300 wine build-win64/ge007-port.exe --selftest 2>&1 | tail -1 | grep -q PASS \
    && echo "WIN64: PASS" || { echo "WIN64: FAIL"; exit 1; }

