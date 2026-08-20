/**
 * @file audio.c
 * M4 audio for the PC port. Replaces src/audi.c (the audio thread + RSP
 * task plumbing):
 *
 *  - amCreateAudioManager(): same ALSynConfig setup as audi.c (22050 Hz,
 *    Rare's custom-FX reverb params), but the DMA callback hands the
 *    synthesizer direct pointers into the loaded ROM instead of PI DMA.
 *  - amStartAudioThread(): opens the SDL audio device.
 *  - portAudioFrame(): called from the scheduler retrace at 60 Hz; runs
 *    alAudioFrame() — which, with the mixer macros from port/include/
 *    mixer.h, synthesizes PCM directly into our buffer — and queues it.
 *
 * The Acmd list alAudioFrame "builds" is vestigial: the abi.h macros are
 * redirected to the software mixer, so by the time it returns the samples
 * are already in outBuf.
 */
#include <ultra64.h>
#include <PR/libaudio.h>
#include "port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GE_HAVE_SDL2
#include <SDL.h>
#endif

#define OUTPUT_RATE 22050
#define EXTRA_SAMPLES 0x25
#define FRAMES_PER_FIELD_AS_POW2 1
#define MAYBE_FRAME_RATE 60

extern u8 *g_PortRomData;

static ALGlobals sALGlobals;
static s32 sFrameSize;
static s32 sMaxFrameSize;
static s32 sStarted;

/* Music/SFX entry points in shared code no-op when synthesis never came up
 * (64-bit builds without PORT_64_AUDIO): their AL players/queues are
 * uninitialized and e.g. alCSPStop would chase a garbage event queue. */
s32 portAudioActive(void)
{
    return sStarted;
}

#ifdef GE_HAVE_SDL2
static SDL_AudioDeviceID sDev;
static void portSynthThreadStart(void);
#endif

/* audi.c's custom reverb parameter block (CUSTOM_FX_PARAMS_N) */
#define ms *(((s32)((f32)44.1)) & ~0x7)
static s32 sCustomFxParams[6 * 8 + 2] = {
    6, 160 ms,
    0,     4 ms,  9830,  -9830,      0,      0,   0,      0,
    4 ms,  8 ms,  9830,  -9830, 0x2B84,      0,   0, 0x2500,
    20 ms, 64 ms, 16384, -16384, 0x11EB,     0,   0, 0x3000,
    80 ms, 140 ms, 16384, -16384, 0x11EB,    0,   0, 0x3500,
    84 ms, 120 ms,  8192,  -8192,     0,     0,   0, 0x4000,
    0,    148 ms, 13000, -13000,      0, 0x017C, 0xA, 0x4500
};
#undef ms

/* --- DMA: the sample "addresses" in the banks are ROM offsets (the tbl
 * segment symbols from rom_symbols.ld), so a DMA is just a pointer add. */
static s32 portAmDmaCallback(s32 addr, s32 len, void *state)
{
    (void)len;
    (void)state;
    return (s32)(g_PortRomData + addr);
}

static ALDMAproc portAmDmaNew(void *state)
{
    (void)state;
    return portAmDmaCallback;
}

void amCreateAudioManager(ALSynConfig *alconf)
{
    f32 fsize;

    alconf->dmaproc = (void *)portAmDmaNew;
    alconf->outputRate = OUTPUT_RATE;

    fsize = (f32)((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);
    sFrameSize = (s32)fsize;
    if (sFrameSize < fsize) {
        sFrameSize++;
    }
    if (sFrameSize & 0xF) {
        sFrameSize = (sFrameSize & ~0xF) + 0x10;
    }
    sMaxFrameSize = sFrameSize + EXTRA_SAMPLES + 0x10;

    if (alconf->fxType == AL_FX_CUSTOM) {
        alconf->params = sCustomFxParams;
    }
    if (getenv("PORT_NO_REVERB") != NULL) {
        alconf->fxType = AL_FX_NONE; /* debug: isolate the fx/reverb path */
    }

    alInit(&sALGlobals, alconf);
}

void amStartAudioThread(void)
{
#ifdef GE_HAVE_SDL2
    SDL_AudioSpec want, have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "port/audio: SDL audio init failed: %s\n", SDL_GetError());
        return;
    }
    memset(&want, 0, sizeof(want));
    want.freq = OUTPUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    sDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sDev == 0) {
        fprintf(stderr, "port/audio: open device failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(sDev, 0);
    sStarted = 1;
    fprintf(stderr, "port/audio: SDL device open (%d Hz stereo s16)\n", have.freq);
    portSynthThreadStart();
#endif
}

/* ---------------------------------------------------------------------------
 * Synthesis. Exactly one thread synthesizes at a time:
 *   - threaded mode (real devices): a dedicated synth thread, so a render
 *     hitch on the game thread can't drain the queue (handheld underruns).
 *     Cross-thread safety is the N64 audio thread's own design — libaudio
 *     and snd.c bracket every shared structure with osSetIntMask, which
 *     portIntMaskInit() arms as a real lock (port/src/ultra/misc.c).
 *   - main-thread mode (PORT_DETERMINISTIC virtual clock, or
 *     PORT_AUDIO_MAINTHREAD=1): synthesis from the 60 Hz retrace, budget-
 *     paced — the bit-exact PCM golden path, unchanged.
 */
static u64 sTotalSamples;
static u64 sT0;

/* One audio frame of synthesis (the cmd list is vestigial but alAudioFrame
 * still writes list heads). Static buffers are safe: only the one
 * synthesizing thread ever gets here. */
static void portSynthFrame(int toDevice, int toDump)
{
    static Acmd cmds[64];
    static s16 buf[4096];
    s32 cmdLen = 0;
    s32 n = sFrameSize;

    if (n <= 0) {
        return; /* audio manager not initialized; nothing advances state */
    }
    if (n > (s32)(sizeof(buf) / 4)) {
        n = sizeof(buf) / 4;
    }
    alAudioFrame(cmds, &cmdLen, buf, n);
    sTotalSamples += (u64)n;
#ifdef GE_HAVE_SDL2
    if (toDevice && sStarted) {
        SDL_QueueAudio(sDev, buf, (u32)n * 4);
    }
#else
    (void)toDevice;
#endif
    if (toDump) {
        static FILE *dump;
        static int checked;
        if (!checked) {
            const char *p = getenv("PORT_AUDIO_DUMP");
            if (p != NULL) {
                dump = fopen(p, "wb");
            }
            checked = 1;
        }
        if (dump != NULL) {
            fwrite(buf, 4, (size_t)n, dump);
            fflush(dump);
        }
    }
}

/* Rate-limit synthesis to real time + a three-frame lead. Without this, a
 * sink that drains instantly (SDL dummy driver) makes the queue-depth check
 * always pass and the synth runs 4x realtime, burning a core. Under the
 * virtual clock this is the sole pacing (the budget advances exactly one
 * frame per retrace). */
static int portSynthUnderBudget(void)
{
    if (sT0 == 0) {
        sT0 = portPlatformTimeNs();
    }
    return sTotalSamples < (portPlatformTimeNs() - sT0) * OUTPUT_RATE / 1000000000ull
                           + (u64)(sFrameSize * 3);
}

#ifdef GE_HAVE_SDL2
static SDL_Thread *sSynthThread;
static SDL_atomic_t sSynthStop;
static s32 sThreaded;

static int portSynthThreadMain(void *arg)
{
    u32 target = (u32)(sFrameSize * 3) * 4; /* bytes: stereo s16 */
    s32 guard;

    (void)arg;
    while (!SDL_AtomicGet(&sSynthStop)) {
        for (guard = 0;
             guard < 4 && portSynthUnderBudget()
             && SDL_GetQueuedAudioSize(sDev) < target;
             guard++) {
            portSynthFrame(1, 1);
        }
        SDL_Delay(2);
    }
    return 0;
}

/* atexit: exit() while the thread is mid-alAudioFrame would race the
 * C runtime teardown; stop and join first. */
static void portSynthThreadStop(void)
{
    if (sSynthThread != NULL) {
        SDL_AtomicSet(&sSynthStop, 1);
        SDL_WaitThread(sSynthThread, NULL);
        sSynthThread = NULL;
    }
}

static void portSynthThreadStart(void)
{
    if (portTimeVirtualActive() || getenv("PORT_AUDIO_MAINTHREAD") != NULL) {
        return;
    }
    portIntMaskInit(); /* arm libaudio's critical sections BEFORE the thread */
    sSynthThread = SDL_CreateThread(portSynthThreadMain, "audio-synth", NULL);
    if (sSynthThread != NULL) {
        sThreaded = 1;
        atexit(portSynthThreadStop);
        fprintf(stderr, "port/audio: synthesis on its own thread\n");
    }
}
#endif

/**
 * Advance synthesis from a game-thread busy-wait.
 *
 * The game spins on sequence player state in a few places
 * (musicTrackNPlay: "while (alCSPGetState(...));"). On N64 the parallel
 * audio thread advanced the player out of AL_STOPPING between polls; in
 * threaded mode ours does too, so just yield. In main-thread mode the wait
 * must pump the synthesizer or spin forever. Output goes to the device
 * queue (it is real audio - the stop tail) and counts against the budget.
 */
void portAudioPump(void)
{
#ifdef GE_HAVE_SDL2
    if (sThreaded) {
        SDL_Delay(1);
        return;
    }
#endif
    portSynthFrame(1, 0);
}

/**
 * Called once per retrace (60 Hz) in main-thread mode. Keeps roughly three
 * video frames of audio queued, synthesizing in frame-sized chunks like
 * the N64 audio manager did. No-op in threaded mode.
 */
void portAudioFrame(void)
{
#ifdef GE_HAVE_SDL2
    s32 guard;

    if (!sStarted || sThreaded) {
        return;
    }

    for (guard = 0;
         guard < 4 && portSynthUnderBudget()
         /* PORT_DETERMINISTIC: the SDL queue drains on a realtime thread,
          * so gating on its depth couples synthesis interleaving to the
          * wall clock; under the virtual clock the budget alone paces
          * (and the queue is not fed — virtual runs outpace the realtime
          * drain, the queue would balloon). */
         && (portTimeVirtualActive()
             || SDL_GetQueuedAudioSize(sDev) < (u32)(sFrameSize * 3) * 4);
         guard++) {
        portSynthFrame(!portTimeVirtualActive(), 1);
    }
#endif
}
