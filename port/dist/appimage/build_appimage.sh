#!/usr/bin/env bash
# Assemble a Linux AppImage (Portable Roadmap phase 5).
#
#   port/dist/appimage/build_appimage.sh [builddir]   -> dist/GE007-<arch>.AppImage
#
# Bundles the binary plus SDL2 via linuxdeploy (GL drivers stay on the
# host, per AppImage convention). The port ships no assets: on first run
# it tells the user where the ROM goes (~/.local/share/ge007/ unless a
# ./data folder exists next to the cwd).
set -ue
cd "$(dirname "$0")/../../.."

BUILD=${1:-build}
[ -x "$BUILD/ge007-port" ] || BUILD=build-64
[ -x "$BUILD/ge007-port" ] || { echo "no binary (ninja -C build first)"; exit 2; }

ARCH=$(uname -m)
mkdir -p dist
TOOL=dist/linuxdeploy-$ARCH.AppImage
if [ ! -x "$TOOL" ]; then
    curl -sL "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" -o "$TOOL"
    chmod +x "$TOOL"
fi

rm -rf dist/AppDir
# --appimage-extract-and-run: works without FUSE (containers, CI);
# OUTPUT names the result directly (the appimage plugin honors it)
OUTPUT="dist/GE007-$ARCH.AppImage" "$TOOL" --appimage-extract-and-run \
    --appdir dist/AppDir \
    -e "$BUILD/ge007-port" \
    -d port/dist/appimage/ge007.desktop \
    -i port/dist/appimage/ge007.png \
    --output appimage

rm -rf dist/AppDir
[ -f "dist/GE007-$ARCH.AppImage" ] && chmod +x "dist/GE007-$ARCH.AppImage"
echo "wrote dist/GE007-$ARCH.AppImage"
