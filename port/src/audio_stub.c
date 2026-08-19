/**
 * @file audio_stub.c
 * M1 audio: no output. music.c and snd.c compile and link against these
 * audio-library stubs; the real path (libultra audio synthesis in C plus a
 * software Acmd mixer) is milestone M4.
 *
 * Symbols here are added on demand as the link asks for them.
 */
#include <ultra64.h>
#include <PR/libaudio.h>
#include "port.h"

/* --- audio manager (src/audi.c replacement) -------------------------------- */
void amCreateAudioManager(void) {}
void amStartAudioThread(void) {}

ALGlobals *alGlobals;

f32 alCents2Ratio(s32 cents)
{
    f32 r = 1.0f;
    /* 2^(cents/1200); avoid libm dependency subtleties, cheap approx is
       fine for a muted stub */
    return r;
}

void alLink(ALLink *element, ALLink *after) {}
void alUnlink(ALLink *element) {}
void alSeqpSetBank(ALSeqPlayer *seqp, ALBank *b) {}
void alSynAddPlayer(ALSynth *drv, ALPlayer *client) {}

/* --- synthesizer driver ------------------------------------------------------ */
void alInit(ALGlobals *g, ALSynConfig *c) {}
void alClose(ALGlobals *g) {}
Acmd *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen)
{
    *cmdLen = 0;
    return cmdList;
}

void alSynNew(ALSynth *drv, ALSynConfig *c) {}
void alSynDelete(ALSynth *drv) {}
s32 alSynAllocVoice(ALSynth *drv, ALVoice *v, ALVoiceConfig *vc) { return 0; }
void alSynFreeVoice(ALSynth *drv, ALVoice *voice) {}
void alSynStartVoice(ALSynth *drv, ALVoice *voice, ALWaveTable *w) {}
void alSynStartVoiceParams(ALSynth *drv, ALVoice *voice, ALWaveTable *w,
                           f32 pitch, s16 vol, ALPan pan, u8 fxmix, ALMicroTime t) {}
void alSynStopVoice(ALSynth *drv, ALVoice *voice) {}
void alSynSetVol(ALSynth *drv, ALVoice *v, s16 vol, ALMicroTime delta) {}
void alSynSetPitch(ALSynth *drv, ALVoice *v, f32 ratio) {}
void alSynSetPan(ALSynth *drv, ALVoice *v, ALPan pan) {}
void alSynSetFXMix(ALSynth *drv, ALVoice *v, u8 fxmix) {}
void alSynSetPriority(ALSynth *drv, ALVoice *v, s16 pri) {}
s16 alSynGetPriority(ALSynth *drv, ALVoice *v) { return 0; }
ALFxRef *alSynAllocFX(ALSynth *drv, s16 bus, ALSynConfig *c, ALHeap *hp) { return 0; }
ALFxRef alSynGetFXRef(ALSynth *drv, s16 bus, s16 index) { return 0; }
void alSynFreeFX(ALSynth *drv, ALFxRef *fx) {}
void alSynSetFXParam(ALSynth *drv, ALFxRef fx, s16 paramID, void *param) {}

/* --- heap ---------------------------------------------------------------------- */
void alHeapInit(ALHeap *hp, u8 *base, s32 len)
{
    hp->base = base;
    hp->cur = base;
    hp->len = len;
    hp->count = 0;
}

void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size)
{
    s32 bytes = (num * size + 15) & ~15;
    u8 *p;

    if (hp == 0 || hp->cur + bytes > hp->base + hp->len) {
        return 0;
    }
    p = hp->cur;
    hp->cur += bytes;
    return p;
}

/* --- bank files ------------------------------------------------------------------ */
void alBnkfNew(ALBankFile *f, u8 *table) {}
void alSeqFileNew(ALSeqFile *f, u8 *base) {}

/* --- sequence player (music.c uses the compressed-midi variant) ------------------- */
void alCSPNew(ALCSPlayer *seqp, ALSeqpConfig *c) {}
void alCSPDelete(ALCSPlayer *seqp) {}
void alCSPSetSeq(ALCSPlayer *seqp, ALCSeq *seq) {}
ALCSeq *alCSPGetSeq(ALCSPlayer *seqp) { return 0; }
void alCSPPlay(ALCSPlayer *seqp) {}
void alCSPStop(ALCSPlayer *seqp) {}
s32 alCSPGetState(ALCSPlayer *seqp) { return 0; }
void alCSPSetBank(ALCSPlayer *seqp, ALBank *b) {}
void alCSPSetTempo(ALCSPlayer *seqp, s32 tempo) {}
s32 alCSPGetTempo(ALCSPlayer *seqp) { return 120; }
s16 alCSPGetVol(ALCSPlayer *seqp) { return 0; }
void alCSPSetVol(ALCSPlayer *seqp, s16 vol) {}
void alCSPSendMidi(ALCSPlayer *seqp, s32 ticks, u8 status, u8 byte1, u8 byte2) {}
void alCSeqNew(ALCSeq *seq, u8 *ptr) {}
void alCSeqGetLoc(ALCSeq *seq, ALCSeqMarker *m) {}
void alCSeqSetLoc(ALCSeq *seq, ALCSeqMarker *m) {}

/* --- event queue / sndp-style player (src/snd.c) ----------------------------------- */
void alEvtqNew(ALEventQueue *evtq, ALEventListItem *items, s32 itemCount) {}
void alEvtqPostEvent(ALEventQueue *evtq, ALEvent *evt, ALMicroTime delta) {}
ALMicroTime alEvtqNextEvent(ALEventQueue *evtq, ALEvent *evt) { return 0; }
void alEvtqFlush(ALEventQueue *evtq) {}
void alEvtqFlushType(ALEventQueue *evtq, s16 type) {}
