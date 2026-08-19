/**
 * @file platform_null.c
 * Headless platform backend: no window, monotonic clock, nanosleep.
 * Used when SDL2 devel headers aren't available and for CI/--selftest.
 */
#include <time.h>
#include <stdio.h>
#include <ultra64.h>
#include "port.h"

int portPlatformInit(const char *title, int width, int height)
{
    fprintf(stderr, "port: headless backend (%s %dx%d)\n", title, width, height);
    return 0;
}

void portPlatformShutdown(void) {}

void portPlatformPresent(void *framebuffer) {}

void portPlatformPoll(void) {}

void portPlatformSleepMs(u32 ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

u64 portPlatformTimeNs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
