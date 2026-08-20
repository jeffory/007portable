/**
 * @file system.c
 * Implementation of the small service layer fast3d expects (system.h).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "system.h"

static int sArgc;
static char **sArgv;

void sysArgsSet(int argc, char **argv)
{
    sArgc = argc;
    sArgv = argv;
}

int sysArgCheck(const char *arg)
{
    int i;

    for (i = 1; i < sArgc; i++) {
        if (strcmp(sArgv[i], arg) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *sysArgGetString(const char *arg)
{
    int i;

    for (i = 1; i + 1 < sArgc; i++) {
        if (strcmp(sArgv[i], arg) == 0) {
            return sArgv[i + 1];
        }
    }
    return NULL;
}

void sysLogPrintf(int level, const char *fmt, ...)
{
    static const char *tags[] = { "note", "warn", "ERROR" };
    va_list args;

    fprintf(stderr, "port/%s: ", tags[level >= 0 && level <= 2 ? level : 2]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

void sysFatalError(const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "port/FATAL: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

void sysSleep(unsigned long long usec)
{
    struct timespec ts;

    ts.tv_sec = usec / 1000000ull;
    ts.tv_nsec = (long)(usec % 1000000ull) * 1000L;
    nanosleep(&ts, 0);
}

void sysCpuRelax(void)
{
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

/* debug helper for the attack-visibility probe in chr.c */
int osGetenv_port_dbg_attackvis(void)
{
    static int v = -1;
    if (v < 0) {
        v = getenv("PORT_DBG_ATTACKVIS") != NULL;
    }
    return v;
}

/* --- virtual clock (PORT_DETERMINISTIC=1, roadmap phase 2) -----------------
 * When active, portPlatformTimeNs() returns a virtual timestamp that
 * advances by exactly one retrace period per portSchedPump() call (and by
 * the requested amount on sleeps) instead of reading the wall clock. Every
 * derived clock — osGetCount/osGetTime, the audio synthesis budget, the
 * autostart poll counter's cadence — becomes a pure function of the
 * retrace count, so headless runs are bit-reproducible and run flat out
 * (no real sleeping). Headless use only: pair with SDL_AUDIODRIVER=dummy
 * (the real audio queue is wall-clock-paced). */
static long long sVirtualNs = -2; /* -2 = unchecked, -1 = real clock */

int portTimeVirtualActive(void)
{
    if (sVirtualNs == -2) {
        const char *env = getenv("PORT_DETERMINISTIC");
        sVirtualNs = (env != NULL && env[0] != '0') ? 0 : -1;
    }
    return sVirtualNs >= 0;
}

unsigned long long portTimeVirtualNow(void)
{
    return (unsigned long long)sVirtualNs;
}

void portTimeVirtualStep(unsigned long long ns)
{
    if (portTimeVirtualActive()) {
        sVirtualNs += (long long)ns;
    }
}
