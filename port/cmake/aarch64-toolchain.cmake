# aarch64 Linux cross toolchain for the GoldenEye 007 PC port.
#
# Targets the ARM handheld class (RG35xxSP etc. via PortMaster) and any
# aarch64 Linux box. Without an aarch64 SDL2 sysroot the build falls back
# to the headless null backend — enough for --selftest, the CRC goldens
# and deterministic headless runs under qemu-user:
#
#   cmake -B build-arm64 -G Ninja --toolchain port/cmake/aarch64-toolchain.cmake
#   ninja -C build-arm64
#   qemu-aarch64-static build-arm64/ge007-port --selftest
#
# (port/tests/run_arm64.sh drives this inside a podman container with the
#  Debian cross toolchain.)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# compile-only sanity checks (no target SDL/GL to link against)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# don't pick up host libraries/headers
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# On a Debian-multiarch host (the ge007-cross container with
# libsdl2-dev:arm64 + mesa :arm64) resolve the target SDL2/GL through
# pkg-config's arm64 libdir — SDL2 backend ON, real rendering under
# qemu-user against a host X server. Without those packages this finds
# nothing and the build falls back to the headless null backend.
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu /usr)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
