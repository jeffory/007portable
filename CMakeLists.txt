# GoldenEye 007 PC port build.
#
# This build is entirely separate from the byte-matching N64 build (Makefile).
# The N64 Makefile wildcard-globs src/*.c and src/game/*.c, so ALL port-only
# code lives under port/ (never scanned by the N64 build). Shared sources are
# gated with `#ifdef TARGET_N64 / #else` only.
#
# Usage:
#   cmake -B build-port -G Ninja --toolchain port/cmake/i686-toolchain.cmake
#   ninja -C build-port
#
# The resulting ge007-port binary expects the US ROM at data/ge007.u.z64
# (sha1 abe01e4aeb033b6c0836819f549c791b26cfde83, same file the N64 build
# byte-matches against).

cmake_minimum_required(VERSION 3.20)
project(ge007-port C CXX)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(STATUS "Pointer model: 32-bit (i686)")
else()
  # M6: native x86_64. The game's data model is 32-bit, so the port keeps
  # every game-visible allocation below 4GB (mmap MAP_32BIT arena + ROM copy,
  # -no-pie statics at 0x400000) and 32-bit-layout asset blobs are expanded
  # to native layout at load time (port/src/preprocess_*64 paths).
  message(STATUS "Pointer model: 64-bit (low-4GB game heap)")
endif()

set(CMAKE_C_STANDARD 90)
set(CMAKE_CXX_STANDARD 20)        # fast3d (uses operator<=>)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)  # -O0 -g until M3; IDO-shaped code + heavy opts = tarpit
endif()

# ---------------------------------------------------------------------------
# Platform backend: SDL2 when devel headers are present, headless null
# backend otherwise (enough for --selftest and CI).
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
set(GE_HAVE_SDL2 OFF)
set(GE_SDL2_TARGET "")
if(PkgConfig_FOUND)
  pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
  if(SDL2_FOUND)
    set(GE_HAVE_SDL2 ON)
    set(GE_SDL2_TARGET PkgConfig::SDL2)
  endif()
endif()
if(NOT GE_HAVE_SDL2)
  # Fallback for hosts without the matching -devel package for this arch:
  # the SDL2 headers are arch-independent (any sdl2*-devel provides them)
  # and the runtime ships the versioned soname without the .so symlink.
  find_path(GE_SDL2_INC NAMES SDL.h PATH_SUFFIXES SDL2)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # must not pick up the 32-bit /usr/lib/libSDL2.so; the 64-bit runtime
    # ships only the versioned soname (no .so symlink without -devel)
    find_library(GE_SDL2_LIB NAMES SDL2 SDL2-2.0 libSDL2-2.0.so.0
                 PATHS /usr/lib64 NO_DEFAULT_PATH)
  else()
    find_library(GE_SDL2_LIB NAMES SDL2 SDL2-2.0 libSDL2-2.0.so.0)
  endif()
  if(GE_SDL2_INC AND GE_SDL2_LIB)
    add_library(ge_sdl2_fallback INTERFACE)
    target_include_directories(ge_sdl2_fallback INTERFACE ${GE_SDL2_INC})
    # arch-specific SDL_config-<arch>.h may be missing when only another
    # arch's -devel package is installed; port/compat64 provides a shim.
    target_include_directories(ge_sdl2_fallback INTERFACE ${CMAKE_SOURCE_DIR}/port/compat64)
    target_link_libraries(ge_sdl2_fallback INTERFACE ${GE_SDL2_LIB})
    set(GE_HAVE_SDL2 ON)
    set(GE_SDL2_TARGET ge_sdl2_fallback)
    message(STATUS "SDL2 via fallback: ${GE_SDL2_INC} + ${GE_SDL2_LIB}")
  endif()
endif()
message(STATUS "SDL2 backend: ${GE_HAVE_SDL2}")

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# All game logic. Port-authored files never go in src/game, so a glob is safe.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.c)
# chraidata.c goes through the same ConvertAIPRINT sed pass as the N64
# Makefile (PRINT("s") -> AI_PRINT,'s',... inline bytes); the CPPLib/SWITCH/
# AND/CALL macros have GCC-compatible PC branches, so the generated file
# compiles natively and its 18 AI byte arrays are byte-exact vs the ROM.
list(REMOVE_ITEM GAME_SOURCES ${CMAKE_SOURCE_DIR}/src/game/chraidata.c)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/gen/chraidata_ai.c
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND sed -E -f ${CMAKE_SOURCE_DIR}/port/tools/ai_print.sed
          ${CMAKE_SOURCE_DIR}/src/game/chraidata.c > ${CMAKE_BINARY_DIR}/gen/chraidata_ai.c
  DEPENDS ${CMAKE_SOURCE_DIR}/src/game/chraidata.c ${CMAKE_SOURCE_DIR}/port/tools/ai_print.sed
  COMMENT "ConvertAIPRINT: chraidata.c -> gen/chraidata_ai.c"
  VERBATIM)
list(APPEND GAME_SOURCES ${CMAKE_BINARY_DIR}/gen/chraidata_ai.c)

# Kept N64-side platform files (the rest — init.c, sched.c, tlb_*, rmon.c,
# crash.c, usb.c, audi.c — are excluded and replaced by port/src/).
set(KEPT_SRC
  src/boss.c
  src/c_data_filler.c
  src/cfb.c
  src/deb.c
  src/debugmenu.c
  src/fr.c
  src/joy.c
  src/mema.c
  src/memp.c
  src/motor.c
  src/music.c
  src/pi.c
  src/ramrom.c
  src/snd.c
  src/speed_graph.c
  # src/sprintf.c excluded: it sits on libultra's _Printf engine; libc sprintf serves
  src/stacks.c
  src/str.c
  src/token.c
  src/vi.c
)

# Portable libultra pieces: matrix/projection math and the VI mode tables
# (pure data). Everything else in libultra/libultrare is N64 plumbing that
# port/src/ultra/ replaces.
file(GLOB GU_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/libultra/gu/*.c)
# IDO's polynomial sinf/cosf use big-endian float bit-tricks that return
# garbage on x86 (inf cot -> invisible 3D). libm provides them instead.
list(REMOVE_ITEM GU_SOURCES
  ${CMAKE_SOURCE_DIR}/src/libultra/gu/sinf.c
  ${CMAKE_SOURCE_DIR}/src/libultra/gu/cosf.c)
set(LIBULTRA_DATA_SRC
  src/libultrare/io/vitbl.c
)

# Rare's decompressor used by the asset loader.
file(GLOB INFLATE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/inflate/*.c)

# libultra audio synthesis + Rare's driver overrides (M4). The Acmd macros
# these emit are redirected to the software mixer via include/PR/abi.h.
file(GLOB AL_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/libultra/audio/*.c
  ${CMAKE_SOURCE_DIR}/src/libultrare/audio/*.c)

set(PORT_SRC
  port/src/main.c
  port/src/preprocess_bg.c
  port/src/preprocess_setup.c
  port/src/sched.c
  port/src/rom.c
  port/src/memory.c
  port/src/preprocess.c
  port/src/preprocess_model.c
  port/src/audio.c
  port/src/mixer.c
  port/src/preprocess_audio.c
  port/src/ultra/mq.c
  port/src/ultra/threads.c
  port/src/ultra/timers.c
  port/src/ultra/pi.c
  port/src/ultra/vi.c
  port/src/ultra/si.c
  port/src/ultra/eeprom.c
  port/src/ultra/misc.c
  port/src/asm_replacements/random.c
  port/src/asm_replacements/math_sincos.c
  port/src/asm_replacements/chr_obj_random.c
  port/src/asm_replacements/getra.c
)

if(GE_HAVE_SDL2)
  # fast3d renderer path: gfx_sdl owns window+events; platform_null still
  # provides timing/sleep; input_sdl polls SDL keyboard state.
  # Built as its own library with a NARROW include path: the game's src/
  # dir shadows <sched.h>, which poisons C++ system headers.
  add_library(fast3d STATIC
    port/fast3d/gfx_pc.cpp
    port/fast3d/gfx_cc.cpp
    port/fast3d/gfx_opengl.cpp
    port/fast3d/gfx_sdl2.cpp
    port/fast3d/glad/glad.c
  )
  target_include_directories(fast3d BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/port/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/port/fast3d
  )
  if(NOT EXISTS /usr/include/gnu/stubs-32.h)
    target_include_directories(fast3d PRIVATE ${CMAKE_SOURCE_DIR}/port/compat32)
  endif()
  target_compile_options(fast3d PRIVATE -fno-strict-aliasing -fsigned-char -fwrapv)
  target_link_libraries(fast3d PRIVATE ${GE_SDL2_TARGET} dl)
  list(APPEND PORT_SRC
    port/src/video.c
    port/src/system.c
    port/src/platform_null.c
    port/src/input_sdl.c
  )
else()
  list(APPEND PORT_SRC
    port/src/video_stub.c
    port/src/system.c
    port/src/platform_null.c
    port/src/input_null.c
  )
endif()

add_executable(ge007-port
  ${PORT_SRC}
  ${KEPT_SRC}
  ${GAME_SOURCES}
  ${GU_SOURCES}
  ${LIBULTRA_DATA_SRC}
  ${INFLATE_SOURCES} ${AL_SOURCES}
)

# ---------------------------------------------------------------------------
# Defines and include paths
# ---------------------------------------------------------------------------
# US flag set from the N64 Makefile's VERSION=US block, minus BYTEMATCH,
# plus TARGET_PC. TARGET_N64 is deliberately NOT defined.
target_compile_definitions(ge007-port PRIVATE
  TARGET_PC
  _LANGUAGE_C   # IDO predefines this; ultratypes.h gates its typedefs on it
  VERSION_US
  LANG_US
  REFRESH_NTSC
  LEFTOVERDEBUG
  LEFTOVERSPECTRUM
  BUGFIX_R0
)
if(GE_HAVE_SDL2)
  target_compile_definitions(ge007-port PRIVATE GE_HAVE_SDL2)
endif()

# port/include must come FIRST so PC shims win over repo headers.
target_include_directories(ge007-port BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/port/include
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/game
  ${CMAKE_SOURCE_DIR}          # for <assets/images.def> style includes
  ${CMAKE_SOURCE_DIR}/include/PR  # libultra-internal headers use <os_internal.h>
  ${CMAKE_SOURCE_DIR}/src/libultra  # vitbl.c uses <io/viint.h>
)

# Allow compiling on hosts without glibc-devel.i686 (linking still needs it).
if(NOT EXISTS /usr/include/gnu/stubs-32.h)
  message(WARNING "glibc-devel.i686 not installed - compiling with a stubs-32.h "
                  "shim; the final link requires: sudo dnf install glibc-devel.i686 libgcc.i686")
  target_include_directories(ge007-port PRIVATE ${CMAKE_SOURCE_DIR}/port/compat32)
endif()

# ---------------------------------------------------------------------------
# The IDO-tolerance flag set. The original code compiled under IDO 5.3 with
# warnings 609/649/709/712/807/838/763 suppressed; modern GCC makes several
# of those hard errors without these flags.
# ---------------------------------------------------------------------------
target_compile_options(ge007-port PRIVATE
  $<$<COMPILE_LANGUAGE:C>:
    -std=gnu89
    -fno-strict-aliasing
    -fms-extensions
    -fsigned-char
    -fwrapv
    -fcommon
    -Wno-implicit-function-declaration
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-implicit-int
    -Wno-builtin-declaration-mismatch
    -Wno-discarded-qualifiers
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
  >
  $<$<COMPILE_LANGUAGE:CXX>:
    -fno-strict-aliasing
    -fsigned-char
    -fwrapv
    -Wno-int-to-pointer-cast
  >
)

# rom_symbols.ld is passed as a plain linker input (NOT -T, which would
# replace ld's default script); it only adds PROVIDE()d absolute symbols.
target_link_options(ge007-port PRIVATE
  -no-pie
  ${CMAKE_SOURCE_DIR}/port/src/rom_symbols.ld
)
set_target_properties(ge007-port PROPERTIES
  LINK_DEPENDS ${CMAKE_SOURCE_DIR}/port/src/rom_symbols.ld)

if(GE_HAVE_SDL2)
  target_compile_definitions(ge007-port PRIVATE GE_HAVE_FAST3D)
  target_link_libraries(ge007-port PRIVATE fast3d ${GE_SDL2_TARGET} dl)
endif()
target_link_libraries(ge007-port PRIVATE m)

set_source_files_properties(port/src/mixer.c PROPERTIES COMPILE_OPTIONS "-std=gnu11")
