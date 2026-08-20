# Windows x86_64 cross toolchain (mingw-w64) for the GoldenEye 007 PC port.
#
#   cmake -B build-win64 -G Ninja --toolchain port/cmake/mingw64-toolchain.cmake
#   ninja -C build-win64
#
# Uses Fedora's mingw64-* packages (mingw64-gcc, mingw64-SDL2); the
# ge007-win podman container has everything (port/tests/run_win64.sh).
# The game's low-4GB invariant is provided by VirtualAlloc address scans
# (port/src/memory.c) and the cooperative game stack by Fibers
# (port/src/main.c).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Fedora keeps the sysroot under sys-root/mingw; Debian/Ubuntu directly
# under the triplet prefix.
if(EXISTS /usr/x86_64-w64-mingw32/sys-root/mingw)
  set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw)
else()
  set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig:/usr/x86_64-w64-mingw32/lib/pkgconfig")

# static libgcc/libstdc++ so the exe runs without mingw runtime DLLs
add_link_options(-static-libgcc -static-libstdc++)
