/**
 * @file video.c
 * fast3d glue: turns the gfx tasks the game submits (rspGfxTaskStart ->
 * sched cmdQ -> portVideoProcessTask) into rendered frames. GE analog of
 * the PD port's video.c, minus the config system.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ultra64.h>
#include "sched.h"
#include "port.h"

s16 viGetX(void); /* fr.c */
s16 viGetY(void);

#include "../fast3d/gfx_api.h"
#include "../fast3d/gfx_sdl.h"
#include "../fast3d/gfx_opengl.h"

static bool sInitDone = false;

int portVideoInitFast3d(void)
{
    struct GfxInitSettings set;
    int scale = 2; /* window = 320x240 * scale */

    /* PORT_WINDOW_SCALE=<1..8>: window size multiple; PORT_FULLSCREEN=1:
     * start fullscreen (desktop mode; Alt+Enter toggles at runtime). */
    {
        const char *e = getenv("PORT_WINDOW_SCALE");

        if (e != NULL) {
            scale = atoi(e);
            if (scale < 1) scale = 1;
            if (scale > 8) scale = 8;
        }
    }

    memset(&set, 0, sizeof(set));
    set.wapi = &gfx_sdl;
    set.rapi = &gfx_opengl_api;
    set.window_settings.title = "GoldenEye 007 (PC port)";
    set.window_settings.width = 320 * scale;
    set.window_settings.height = 240 * scale;
    set.window_settings.x = 100;
    set.window_settings.y = 100;
    {
        const char *e = getenv("PORT_FULLSCREEN");

        set.window_settings.fullscreen = e != NULL;
        /* =2: exclusive (mode-setting) fullscreen — works without a window
         * manager; default (=1) is borderless desktop fullscreen. */
        set.window_settings.fullscreen_is_exclusive = e != NULL && atoi(e) == 2;
    }

    gfx_current_native_viewport.width = 320;
    gfx_current_native_viewport.height = 240;
    gfx_current_native_aspect = 320.0f / 240.0f;
    gfx_framebuffers_enabled = true; /* menus render via offscreen color images */
    gfx_msaa_level = 1;

    gfx_init(&set);
    gfx_set_texture_filter(FILTER_THREE_POINT);

    sInitDone = true;
    return 0;
}

void portVideoInit(void)
{
    if (portVideoInitFast3d() != 0) {
        fprintf(stderr, "port/video: fast3d init failed\n");
    }
}

void portVideoProcessTask(OSScTask *task)
{
    Gfx *dl = (Gfx *)task->list.t.data_ptr;

    if (!sInitDone || dl == NULL) {
        return;
    }

    /* Menus/gunbarrel render at 440x330, gameplay at 320x240; the game
     * records the per-frame framebuffer size in the VI globals before
     * submitting (fr.c video_related_8). Track it as the native mode. */
    {
        u32 w = (u32)viGetX();
        u32 h = (u32)viGetY();

        if (getenv("PORT_PIN_VIEWPORT") == NULL &&
            w >= 320 && w <= 640 && h >= 200 && h <= 480 &&
            (w != gfx_current_native_viewport.width ||
             h != gfx_current_native_viewport.height)) {
            gfx_current_native_viewport.width = w;
            gfx_current_native_viewport.height = h;
            gfx_current_native_aspect = (float)w / (float)h;
        }
    }

    if (getenv("PORT_TASK_TRACE") != NULL) {
        static unsigned taskn;
        u32 w0 = dl->words.w0, w1 = dl->words.w1;
        fprintf(stderr, "port/task: #%u t=%.2fs dl=%p first=%08x %08x\n",
                taskn++, portPlatformTimeNs() / 1e9, (void *)dl, w0, w1);
    }
    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();

    {
        extern unsigned gfx_dbg_vtxdump;
        static int armed;

        /* dump a handful of vertex transforms around the logo phase */
        if (!armed && getenv("PORT_VTX_DUMP") != NULL) {
            static u64 t0;
            u64 now = portPlatformTimeNs();

            if (t0 == 0) {
                t0 = now;
            }
            {
                static u64 armNs;
                const char *e = getenv("PORT_DUMP_AFTER");

                if (armNs == 0) {
                    armNs = e ? (u64)atoi(e) * 1000000000ull : 60ull * 1000000000ull;
                }
                if (now - t0 <= armNs) {
                    goto notyet;
                }
            }
            if (1) {
                extern unsigned gfx_dbg_rectdump, gfx_dbg_tridump, gfx_dbg_ccdump;
                const char *n = getenv("PORT_DUMP_COUNT");
                unsigned count = n ? (unsigned)atoi(n) : 0;

                gfx_dbg_vtxdump = 30;
                gfx_dbg_rectdump = count ? count : 60;
                gfx_dbg_tridump = count ? count : 50;
                gfx_dbg_ccdump = count ? count : 40;
                armed = 1;
                fprintf(stderr, "port/dbg: dump ARMED\n");
            }
notyet:;
        }
    }
    if (g_PortConfig.verbose) {
        extern unsigned gfx_dbg_tris_in, gfx_dbg_tris_out, gfx_dbg_vtx, gfx_dbg_mtx;
        static unsigned frame;

        if ((frame++ % 30) == 0) {
            fprintf(stderr, "port/video: t=%us f%u tris_in=%u tris_out=%u vtx=%u mtx=%u\n",
                    (unsigned)(portPlatformTimeNs() / 1000000000ull),
                    frame, gfx_dbg_tris_in, gfx_dbg_tris_out, gfx_dbg_vtx, gfx_dbg_mtx);
        }
        gfx_dbg_tris_in = gfx_dbg_tris_out = gfx_dbg_vtx = gfx_dbg_mtx = 0;
    }
}

/* Android: re-bind the GL context on the game pthread (see main.c) */
void gfx_sdl_make_current(void);
void portVideoMakeCurrent(void)
{
    gfx_sdl_make_current();
}

void gfx_sdl_release_current(void);
void portVideoReleaseCurrent(void)
{
    gfx_sdl_release_current();
}
