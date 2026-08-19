/**
 * @file platform_sdl.c
 * SDL2 platform backend for M1: a window cleared to a color each frame
 * (proof the boss loop breathes). fast3d takes over rendering in M2.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <ultra64.h>
#include "port.h"

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static u32 sFrame;

int portPlatformInit(const char *title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "port: SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
#ifndef GE_HAVE_FAST3D
    /* M1 fallback window (fast3d creates the real one in portVideoInit;
     * a second window would steal focus from input/mouselook) */
    sWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width * 2, height * 2, SDL_WINDOW_RESIZABLE);
    if (sWindow == NULL) {
        fprintf(stderr, "port: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED);
    if (sRenderer == NULL) {
        sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
    }
#endif
    return 0;
}

void portPlatformShutdown(void)
{
    if (sRenderer) SDL_DestroyRenderer(sRenderer);
    if (sWindow) SDL_DestroyWindow(sWindow);
    SDL_Quit();
}

void portPlatformPresent(void *framebuffer)
{
    if (sRenderer == NULL) {
        return;
    }
    /* M1: solid clear that slowly cycles so a live loop is visible */
    sFrame++;
    SDL_SetRenderDrawColor(sRenderer, 8, (Uint8)(32 + (sFrame & 63)), 24, 255);
    SDL_RenderClear(sRenderer);
    SDL_RenderPresent(sRenderer);
}

void portPlatformPoll(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT ||
            (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
            fprintf(stderr, "port: quit requested\n");
            portPlatformShutdown();
            exit(0);
        }
    }
}

void portPlatformSleepMs(u32 ms)
{
    SDL_Delay(ms);
}

u64 portPlatformTimeNs(void)
{
    static Uint64 sFreq;

    if (sFreq == 0) {
        sFreq = SDL_GetPerformanceFrequency();
    }
    return (u64)((double)SDL_GetPerformanceCounter() * 1e9 / (double)sFreq);
}
