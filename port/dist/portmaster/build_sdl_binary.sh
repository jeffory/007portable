#!/usr/bin/env bash
# aarch64 SDL build for the PortMaster package (the plain run_arm64.sh
# build is the headless CRC-smoke one — no SDL on that container).
# Needs the ge007-cross-sdl podman image (Debian trixie + multiarch arm64
# SDL2/Mesa). Output: build-arm64-sdl/ge007-port
set -ue
cd "$(dirname "$0")/../../.."

if ! podman image exists localhost/ge007-cross-sdl; then
    podman build -t ge007-cross-sdl -f - <<'IMG'
FROM docker.io/debian:trixie
RUN dpkg --add-architecture arm64 && apt-get update && \
    apt-get install -y --no-install-recommends \
    cmake ninja-build python3 gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    libsdl2-dev:arm64 libgl1-mesa-dev:arm64 pkg-config \
    qemu-user-static && apt-get clean
IMG
fi

podman run --rm -v "$PWD:/work:z" -w /work localhost/ge007-cross-sdl bash -c \
    "cmake -B build-arm64-sdl -G Ninja --toolchain port/cmake/aarch64-toolchain.cmake && ninja -C build-arm64-sdl"
echo "built build-arm64-sdl/ge007-port"
