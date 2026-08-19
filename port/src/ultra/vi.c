/**
 * @file vi.c
 * VI state recorder. The game (fr.c/vi.c) drives the N64 video interface
 * through these; on PC we just remember the state so the renderer can read
 * resolution/scale per frame, and track the framebuffer swap chain.
 */
#include <ultra64.h>
#include "port.h"

static void *sCurrentFb;
static void *sNextFb;
static OSViMode sCurrentMode;
static f32 sXScale = 1.0f;
static f32 sYScale = 1.0f;
static u32 sSpecialFeatures;

void osCreateViManager(OSPri pri) {}

void osViSetMode(OSViMode *mode)
{
    sCurrentMode = *mode;
}

void osViSetXScale(f32 s) { sXScale = s; }
void osViSetYScale(f32 s) { sYScale = s; }
void osViSetSpecialFeatures(u32 f) { sSpecialFeatures = f; }
void osViBlack(u8 active) {}
void osViRepeatLine(u8 active) {}
void osViFade(u8 active, u16 value) {}
void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {}

void osViSwapBuffer(void *frameBufPtr)
{
    sNextFb = frameBufPtr;
    /* the "swap" itself happens at the pump's retrace boundary, but for
       state-tracking purposes current==next is fine on PC */
    sCurrentFb = frameBufPtr;
}

void *osViGetCurrentFramebuffer(void)
{
    return sCurrentFb;
}

void *osViGetNextFramebuffer(void)
{
    return sNextFb;
}

u32 *osViGetCurrentContext(void)
{
    return (u32 *)&sCurrentMode;
}
