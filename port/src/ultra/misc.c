/**
 * @file misc.c
 * Everything else libultra: boot globals, cache ops (no-ops), interrupt
 * masks (no-ops), address translation (identity), SP/DP task stubs,
 * osSyncPrintf -> stderr.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "port.h"

/* Boot globals: on N64 these live at fixed RAM addresses set by the linker
 * script; on PC they're ordinary variables initialized by main(). */
u32 osTvType = OS_TV_NTSC;
s32 osRomType = 0;
u32 osRomBase = 0;
u32 osResetType = 0;
s32 osCicId = 0;
s32 osVersion = 0;
u32 osMemSize = 8 * 1024 * 1024;
u8 osAppNMIBuffer[64];

/* Counter/clock rate: keep in the same units as osGetCount (46.875 MHz) so
 * the OS_USEC_TO_CYCLES(osClockRate) macro branch stays consistent. */
u64 osClockRate = 46875000;

/* IDO libm's quiet-NaN constant, referenced by src/libultra/gu/{sinf,cosf}.c
 * for out-of-range inputs (bit pattern from libm_vals.s). */
union { u32 i; float f; } __libm_qnan_f = { 0x7FBFFFFF };

void osInitialize(void) {}

/* RSP microcode symbols referenced by rspGfxTaskStart (src/game/rsp.c).
 * No ucode ever runs on PC; dummy storage keeps the task fields harmless. */
long long int rspbootTextStart[2];
long long int rspbootTextEnd[1];
long long int gsp3DTextStart[2];
long long int gsp3DDataStart[2];

/* --- rmon / crash (excluded modules) -------------------------------------- */
s32 rmonStatus = 0; /* INDI_NOT_DETECTED-ish: no debugger */

s32 rmonGetToken(void)
{
    return 0;
}

void crashInit(void) {}

/* --- cache ----------------------------------------------------------------- */
void osInvalDCache(void *vaddr, s32 nbytes) {}
void osInvalICache(void *vaddr, s32 nbytes) {}
void osWritebackDCache(void *vaddr, s32 nbytes) {}
void osWritebackDCacheAll(void) {}

/* --- address translation ---------------------------------------------------- */
u32 osVirtualToPhysical(void *vaddr)
{
    return (u32)vaddr;
}

void *osPhysicalToVirtual(u32 paddr)
{
    return (void *)paddr;
}

/* --- interrupts / events ----------------------------------------------------- */
OSIntMask osGetIntMask(void) { return 0; }
OSIntMask osSetIntMask(OSIntMask mask) { return 0; }
u32 __osDisableInt(void) { return 0; }
void __osRestoreInt(u32 flags) {}
void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg) {}

/* --- FPU control -------------------------------------------------------------- */
u32 __osGetFpcCsr(void) { return 0; }
u32 __osSetFpcCsr(u32 v) { return 0; }

/* --- TLB ------------------------------------------------------------------------ */
void osMapTLB(s32 index, OSPageMask pm, void *vaddr, u32 evenpaddr, u32 oddpaddr, s32 asid) {}
void osUnmapTLB(s32 index) {}
void osUnmapTLBAll(void) {}
void osMapTLBRdb(void) {}

/* --- SP/DP (RSP/RDP) — consumed by the port video sink instead ---------------- */
void osSpTaskStartGo(OSTask *task) {}
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask *task) { return 0; }
void osSpTaskLoad(OSTask *task) {}
u32 osDpGetStatus(void) { return 0; }
void osDpSetStatus(u32 status) {}
void osDpGetCounters(u32 *counters)
{
    counters[0] = counters[1] = counters[2] = counters[3] = 0;
}
s32 osDpSetNextBuffer(void *buf, u64 size) { return 0; }

/* --- AI (audio) — real output arrives in M4 ------------------------------------ */
s32 osAiSetFrequency(u32 freq) { return (s32)freq; }
s32 osAiSetNextBuffer(void *buf, u32 size) { return 0; }
u32 osAiGetLength(void) { return 0; }
u32 osAiGetStatus(void) { return 0; }

/* --- logging -------------------------------------------------------------------- */
void osSyncPrintf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void rmonPrintf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void osLogEvent(void *log, s16 code, s16 numArgs, ...) {}

/* --- host (Indy dev link) — never detected --------------------------------------- */
s32 osTestHost(void) { return 0; }
void osReadHost(void *dramAddr, u32 nbytes) {}
void osWriteHost(void *dramAddr, u32 nbytes) {}
