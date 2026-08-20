package com.ge007.port;

import org.libsdl.app.SDLActivity;

/**
 * Thin shell over SDL's activity: load libSDL2 + our game (libmain.so,
 * whose SDL_main is port/src/main.c's entry point renamed).
 *
 * The org.libsdl.app.* glue classes are vendored from the SDL2 source
 * release by fetch_sdl.sh (not committed; zlib-licensed).
 */
public class GEActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
