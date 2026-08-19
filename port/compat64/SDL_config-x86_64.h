/*
 * Shim for Fedora multilib systems where only sdl2-compat-devel.i686 is
 * installed: /usr/include/SDL2/SDL_config.h dispatches to an arch-named
 * file that the i686 package does not provide for x86_64. sdl2-compat's
 * per-arch config headers are all identical arch-independent dispatchers
 * (they just chain to SDL_config_unix.h and friends), so reusing the
 * i386 one is exact.
 *
 * This directory is only added to the include path by the CMake SDL2
 * fallback (when pkg-config has no native sdl2 for this arch).
 */
#include "SDL_config-i386.h"
