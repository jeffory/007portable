package com.ge007.port;

import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * Thin shell over SDL's activity: load libSDL2 + our game (libmain.so,
 * whose SDL_main is port/src/main.c's entry point renamed).
 *
 * The org.libsdl.app.* glue classes are vendored from the SDL2 source
 * release by fetch_sdl.sh (not committed; zlib-licensed).
 *
 * Fullscreen comes from three places that all have to agree:
 *   - the manifest's Theme.NoTitleBar.Fullscreen drops the activity title
 *     bar and the status bar,
 *   - port/src/video.c asks SDL for a fullscreen window (default on
 *     Android), which makes SDLActivity turn on immersive sticky and hide
 *     the navigation bar, re-hiding it after a swipe,
 *   - and onCreate below lets the window use the display cutout area, so
 *     a notch leaves a black bar instead of shrinking the picture.
 */
public class GEActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        /* Before super: the window attributes have to be set before the
         * decor view is laid out. Landscape-locked, so shortEdges puts the
         * cutout on the left or right edge, never across the middle. */
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        super.onCreate(savedInstanceState);
    }
}
