#!/usr/bin/env bash
# Assemble the PortMaster zip from an aarch64 build.
#   port/tests/run_arm64.sh build         (or an SDL-enabled device build)
#   port/dist/portmaster/build_package.sh [builddir]  -> dist/GE007.zip
set -ue
cd "$(dirname "$0")/../../.."

BUILD=${1:-build-arm64-sdl}
[ -x "$BUILD/ge007-port" ] || BUILD=build-arm64
[ -x "$BUILD/ge007-port" ] || { echo "no aarch64 binary (run port/tests/run_arm64.sh build)"; exit 2; }

OUT=dist/portmaster-stage
rm -rf "$OUT" && mkdir -p "$OUT/ge007/data" "$OUT/ge007/licenses" dist

cp port/dist/portmaster/GE007.sh "$OUT/"
cp "$BUILD/ge007-port" "$OUT/ge007/"
cp port/dist/portmaster/README.md "$OUT/ge007/"
cp port/fast3d/LICENSE.txt "$OUT/ge007/licenses/fast3d-LICENSE.txt" 2>/dev/null || true
cat > "$OUT/ge007/data/PLACE_ROM_HERE.txt" <<'EOF'
Put your US GoldenEye 007 ROM here as: ge007.u.z64
(z64 byte order, sha1 abe01e4aeb033b6c0836819f549c791b26cfde83)
No ROM, no game — the port ships no assets.
EOF

(cd "$OUT" && zip -qr ../GE007.zip .)
rm -rf "$OUT"
echo "wrote dist/GE007.zip"
