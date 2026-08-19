# i686 (32-bit) toolchain for the GoldenEye 007 PC port.
#
# The game's data model is 32-bit (asset blobs contain 32-bit offsets that are
# rebased in place as pointers), so the first port target is i686. Use:
#   cmake -B build-port -G Ninja --toolchain port/cmake/i686-toolchain.cmake
#
# Fedora prerequisites:
#   sudo dnf install glibc-devel.i686 libgcc.i686 libatomic.i686 \
#        sdl2-compat-devel.i686 mesa-libGL-devel.i686 libglvnd-devel.i686 \
#        mesa-dri-drivers.i686

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

# Sanity-check by compiling only: lets configure succeed on hosts that can
# compile -m32 but not yet link it (glibc-devel.i686 not installed).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")

# Make pkg-config resolve the 32-bit SDL2, not the lib64 one.
# (Fedora: /usr/lib/pkgconfig; Debian/Ubuntu: /usr/lib/i386-linux-gnu/pkgconfig)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
