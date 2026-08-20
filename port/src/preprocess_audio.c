/**
 * @file preprocess_audio.c
 * Load-time byteswaps for audio data (PORT_PREPROCESS):
 *
 *  - ctl bank files (sfx.ctl / instruments.ctl), swapped IN PLACE right
 *    after romCopy and before alBnkfNew rebases the offsets to pointers.
 *    The walk mirrors bnkf.c's patch walk; a visited map replaces the
 *    'flags' dedup the runtime walk uses (shared sounds/wavetables).
 *  - the Rare sequence table (music sample tbl header + entries).
 *  - decompressed compressed-MIDI sequence headers (17 big-endian u32s).
 *
 * tbl sample data (ADPCM frames) stays in N64 byte order — it is a byte
 * stream. AL_RAW16_WAVE tables would need their s16 samples swapped in
 * the ROM copy; none exist in GE's banks, but we warn if one appears.
 */
#include <ultra64.h>
#include <PR/libaudio.h>
#include <platform_info.h>
#include "music.h"
#include "port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 swap32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static u16 swap16(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

struct bnkctx {
    u8 *base;
    u32 size;
    u8 *visited; /* one byte per 4 bytes of file */
    u32 tblRomBase; /* ROM offset of this ctl's tbl segment */
};

extern u8 *g_PortRomData;

static int visited(struct bnkctx *ctx, u32 off)
{
    if (off / 4 >= ctx->size / 4) {
        return 1; /* out of range: pretend visited so we don't touch it */
    }
    if (ctx->visited[off / 4]) {
        return 1;
    }
    ctx->visited[off / 4] = 1;
    return 0;
}

static u32 swapWordAt(struct bnkctx *ctx, u32 off)
{
    u32 *p = (u32 *)(ctx->base + off);
    *p = swap32(*p);
    return *p;
}

static u16 swapHalfAt(struct bnkctx *ctx, u32 off)
{
    u16 *p = (u16 *)(ctx->base + off);
    *p = swap16(*p);
    return *p;
}

static void swapEnvelope(struct bnkctx *ctx, u32 off)
{
    if (off == 0 || visited(ctx, off)) {
        return;
    }
    swapWordAt(ctx, off + 0); /* attackTime */
    swapWordAt(ctx, off + 4); /* decayTime */
    swapWordAt(ctx, off + 8); /* releaseTime */
    /* attackVolume/decayVolume are u8 */
}

static void swapAdpcmBook(struct bnkctx *ctx, u32 off)
{
    s32 order, npred, n, i;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    order = (s32)swapWordAt(ctx, off + 0);
    npred = (s32)swapWordAt(ctx, off + 4);
    n = order * npred * 8;
    if (n < 0 || (u32)n * 2 + off + 8 > ctx->size) {
        fprintf(stderr, "port/audio: bad ADPCM book at 0x%x (order %d npred %d)\n", off, order, npred);
        return;
    }
    for (i = 0; i < n; i++) {
        swapHalfAt(ctx, off + 8 + (u32)i * 2);
    }
}

static void swapAdpcmLoop(struct bnkctx *ctx, u32 off)
{
    s32 i;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    swapWordAt(ctx, off + 0);  /* start */
    swapWordAt(ctx, off + 4);  /* end */
    swapWordAt(ctx, off + 8);  /* count */
    for (i = 0; i < 16; i++) { /* ADPCM_STATE: s16[16] */
        swapHalfAt(ctx, off + 12 + (u32)i * 2);
    }
}

static void swapRawLoop(struct bnkctx *ctx, u32 off)
{
    if (off == 0 || visited(ctx, off)) {
        return;
    }
    swapWordAt(ctx, off + 0);
    swapWordAt(ctx, off + 4);
    swapWordAt(ctx, off + 8);
}

static void swapWaveTable(struct bnkctx *ctx, u32 off)
{
    u8 type;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    swapWordAt(ctx, off + 0); /* base (tbl-relative offset) */
    swapWordAt(ctx, off + 4); /* len */
    type = ctx->base[off + 8]; /* type, flags: u8 */

    if (type == AL_ADPCM_WAVE) {
        u32 loop = swapWordAt(ctx, off + 12);
        u32 book = swapWordAt(ctx, off + 16);
        swapAdpcmLoop(ctx, loop);
        swapAdpcmBook(ctx, book);
    } else if (type == AL_RAW16_WAVE) {
        u32 loop = swapWordAt(ctx, off + 12);
        u32 base = *(u32 *)(ctx->base + off);   /* already swapped above */
        u32 len = *(u32 *)(ctx->base + off + 4);
        swapRawLoop(ctx, loop);
        /* raw s16 samples live in the tbl segment in the ROM buffer;
         * swap them in place (each wavetable is visited once) */
        if (ctx->tblRomBase != 0) {
            u16 *smp = (u16 *)(g_PortRomData + ctx->tblRomBase + base);
            u32 i;
            for (i = 0; i < len / 2; i++) {
                smp[i] = swap16(smp[i]);
            }
        }
    } else {
        fprintf(stderr, "port/audio: unknown wavetable type %d at 0x%x\n", type, off);
    }
}

static void swapSound(struct bnkctx *ctx, u32 off)
{
    u32 env, key, wav;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    env = swapWordAt(ctx, off + 0);
    key = swapWordAt(ctx, off + 4);
    wav = swapWordAt(ctx, off + 8);
    (void)key; /* ALKeyMap is all u8 */
    swapEnvelope(ctx, env);
    swapWaveTable(ctx, wav);
}

static void swapInstrument(struct bnkctx *ctx, u32 off)
{
    s16 soundCount;
    s32 i;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    /* 12 u8s, then s16 bendRange, s16 soundCount, then offsets */
    swapHalfAt(ctx, off + 12);
    soundCount = (s16)swapHalfAt(ctx, off + 14);
    for (i = 0; i < soundCount; i++) {
        u32 s = swapWordAt(ctx, off + 16 + (u32)i * 4);
        swapSound(ctx, s);
    }
}

static void swapBank(struct bnkctx *ctx, u32 off)
{
    s16 instCount;
    u32 perc;
    s32 i;

    if (off == 0 || visited(ctx, off)) {
        return;
    }
    instCount = (s16)swapHalfAt(ctx, off + 0); /* instCount; flags/pad u8 */
    swapWordAt(ctx, off + 4);                  /* sampleRate */
    perc = swapWordAt(ctx, off + 8);           /* percussion */
    swapInstrument(ctx, perc);
    for (i = 0; i < instCount; i++) {
        u32 inst = swapWordAt(ctx, off + 12 + (u32)i * 4);
        swapInstrument(ctx, inst);
    }
}

void portSwapBankFile(void *data, u32 size, u32 tblRomBase)
{
    struct bnkctx ctx;
    s16 bankCount;
    s32 i;
    u16 *hdr = data;

    ctx.base = data;
    ctx.size = size;
    ctx.tblRomBase = tblRomBase;
    ctx.visited = calloc(1, size / 4 + 1);
    if (ctx.visited == NULL) {
        return;
    }

    hdr[0] = swap16(hdr[0]); /* revision ('B1') */
    bankCount = (s16)(hdr[1] = swap16(hdr[1]));

    for (i = 0; i < bankCount; i++) {
        u32 b = swapWordAt(&ctx, 4 + (u32)i * 4);
        swapBank(&ctx, b);
    }

    free(ctx.visited);
    portCrcTrace("ctl", tblRomBase, data, size);
    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/audio: ctl bank swapped (%u bytes, %d banks)\n", size, bankCount);
    }
}

/* Rare sequence table: {u16 seqCount; u16 pad;
 *                       {u32 address; u16 uncompressed_len; u16 len}[]} */
void portSwapRareSeqHeader(void *data)
{
    u16 *hdr = data;
    hdr[0] = swap16(hdr[0]); /* seqCount */
    hdr[1] = swap16(hdr[1]);
}

void portSwapRareSeqTable(void *data)
{
    u16 *hdr = data;
    u8 *entry;
    s32 count, i;

    portSwapRareSeqHeader(data);
    count = hdr[0];
    entry = (u8 *)data + 4;
    for (i = 0; i < count; i++) {
        *(u32 *)(entry + 0) = swap32(*(u32 *)(entry + 0));
        *(u16 *)(entry + 4) = swap16(*(u16 *)(entry + 4));
        *(u16 *)(entry + 6) = swap16(*(u16 *)(entry + 6));
        entry += 8;
    }
    portCrcTrace("seqtable", 0, data, 4 + (u32)count * 8);
}

/* Decompressed compressed-MIDI sequence: ALCMidiHdr is 17 u32s
 * (trackOffset[16] + division); the event stream after it is bytes. */
void portSwapCMidiHdr(void *data)
{
    u32 *w = data;
    s32 i;
    for (i = 0; i < 17; i++) {
        w[i] = swap32(w[i]);
    }
    portCrcTrace("cmidi", 0, data, 17 * 4);
}

/* ---------------------------------------------------------------------------
 * 64-bit: native bank promotion. The ctl blob is 32-bit layout (4-byte
 * pointer slots); alBnkfNew promotes offsets IN PLACE through native
 * structs, which mis-strides on 64-bit. Instead, rebuild the five
 * pointer-bearing containers (file, bank, instrument, sound, wavetable)
 * as native structs; the pointer-free leaves (envelopes, keymaps, loops,
 * books) are layout-identical and stay in the blob, pointed at directly.
 * On 32-bit this is just alBnkfNew. Wavetable base keeps alBnkfNew's
 * semantics: blob value + the tbl segment's ROM offset (the DMA callback
 * resolves it against the loaded ROM).
 * ------------------------------------------------------------------------- */
#if IS_64_BIT

struct natmap {
    u32 off;
    void *nat;
};

struct natctx {
    u8 *blob;
    uintptr_t tbl;
    struct natmap map[512];
    s32 nmap;
};

static void *natLookup(struct natctx *c, u32 off)
{
    s32 i;
    for (i = 0; i < c->nmap; i++) {
        if (c->map[i].off == off) {
            return c->map[i].nat;
        }
    }
    return NULL;
}

static void *natAlloc(struct natctx *c, u32 off, u32 size)
{
    void *p = portLowAlloc(size);
    memset(p, 0, size);
    if (c->nmap < (s32)(sizeof(c->map) / sizeof(c->map[0]))) {
        c->map[c->nmap].off = off;
        c->map[c->nmap].nat = p;
        c->nmap++;
    } else {
        fprintf(stderr, "port/audio: natmap full\n");
    }
    return p;
}

static u32 rd32(const u8 *p) { return *(const u32 *)p; }
static u16 rd16(const u8 *p) { return *(const u16 *)p; }

static ALWaveTable *natWaveTable(struct natctx *c, u32 off)
{
    ALWaveTable *w;
    u8 type;

    if (off == 0) {
        return NULL;
    }
    w = natLookup(c, off);
    if (w != NULL) {
        return w;
    }
    w = natAlloc(c, off, sizeof(ALWaveTable));
    w->base = (u8 *)(c->tbl + rd32(c->blob + off));
    w->len = (s32)rd32(c->blob + off + 4);
    w->type = c->blob[off + 8];
    w->flags = 1; /* promoted */
    type = w->type;
    if (type == AL_ADPCM_WAVE) {
        u32 loop = rd32(c->blob + off + 12);
        u32 book = rd32(c->blob + off + 16);
        w->waveInfo.adpcmWave.loop = loop ? (ALADPCMloop *)(c->blob + loop) : NULL;
        w->waveInfo.adpcmWave.book = book ? (ALADPCMBook *)(c->blob + book) : NULL;
    } else if (type == AL_RAW16_WAVE) {
        u32 loop = rd32(c->blob + off + 12);
        w->waveInfo.rawWave.loop = loop ? (ALRawLoop *)(c->blob + loop) : NULL;
    }
    return w;
}

static ALSound *natSound(struct natctx *c, u32 off)
{
    ALSound *s;
    u32 env, key, wav;

    if (off == 0) {
        return NULL;
    }
    s = natLookup(c, off);
    if (s != NULL) {
        return s;
    }
    s = natAlloc(c, off, sizeof(ALSound));
    env = rd32(c->blob + off + 0);
    key = rd32(c->blob + off + 4);
    wav = rd32(c->blob + off + 8);
    s->envelope = env ? (ALEnvelope *)(c->blob + env) : NULL;
    s->keyMap = key ? (ALKeyMap *)(c->blob + key) : NULL;
    s->wavetable = natWaveTable(c, wav);
    s->samplePan = c->blob[off + 12];
    s->sampleVolume = c->blob[off + 13];
    s->flags = 1;
    return s;
}

static ALInstrument *natInstrument(struct natctx *c, u32 off)
{
    ALInstrument *inst;
    s16 n;
    s32 i;

    if (off == 0) {
        return NULL;
    }
    inst = natLookup(c, off);
    if (inst != NULL) {
        return inst;
    }
    n = (s16)rd16(c->blob + off + 14);
    inst = natAlloc(c, off, sizeof(ALInstrument) + (n > 0 ? (u32)(n - 1) : 0) * sizeof(ALSound *));
    memcpy(inst, c->blob + off, 16); /* 12 u8s + bendRange + soundCount: identical layout */
    inst->flags = 1;
    for (i = 0; i < n; i++) {
        inst->soundArray[i] = natSound(c, rd32(c->blob + off + 16 + (u32)i * 4));
    }
    return inst;
}

static ALBank *natBank(struct natctx *c, u32 off)
{
    ALBank *b;
    s16 n;
    s32 i;

    if (off == 0) {
        return NULL;
    }
    b = natLookup(c, off);
    if (b != NULL) {
        return b;
    }
    n = (s16)rd16(c->blob + off + 0);
    b = natAlloc(c, off, sizeof(ALBank) + (n > 0 ? (u32)(n - 1) : 0) * sizeof(ALInstrument *));
    b->instCount = n;
    b->flags = 1;
    b->sampleRate = (s32)rd32(c->blob + off + 4);
    b->percussion = natInstrument(c, rd32(c->blob + off + 8));
    for (i = 0; i < n; i++) {
        b->instArray[i] = natInstrument(c, rd32(c->blob + off + 12 + (u32)i * 4));
    }
    return b;
}

void *portBnkfPromote(void *blob, u32 size, u8 *tbl)
{
    static struct natctx c; /* single-threaded; large map */
    ALBankFile *f;
    s16 n;
    s32 i;

    (void)size;
    memset(&c, 0, sizeof(c));
    c.blob = blob;
    c.tbl = (uintptr_t)tbl;
    n = (s16)rd16(c.blob + 2);
    f = portLowAlloc(sizeof(ALBankFile) + (n > 0 ? (u32)(n - 1) : 0) * sizeof(ALBank *));
    f->revision = (s16)rd16(c.blob + 0);
    f->bankCount = n;
    for (i = 0; i < n; i++) {
        f->bankArray[i] = natBank(&c, rd32(c.blob + 4 + (u32)i * 4));
    }
    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/audio64: bank promoted natively (%d banks, %d objects)\n",
                f->bankCount, c.nmap);
    }
    return f;
}

/* Rare seq table: blob records are 8 bytes ({u32 addr; u16 unc; u16 len});
 * native RareALSeqData is pointer-widened. Rebuild natively. */
void *portSeqTablePromote(void *blob)
{
    u8 *b = blob;
    u16 count = rd16(b + 0);
    RareALSeqBankFile *nat = portLowAlloc(sizeof(RareALSeqBankFile)
                                          + (count > 0 ? (u32)(count - 1) : 0) * sizeof(RareALSeqData));
    u32 i;

    nat->seqCount = count;
    nat->unk = 0;
    for (i = 0; i < count; i++) {
        const u8 *r = b + 4 + i * 8;
        nat->seqArray[i].address = (u8 *)(uintptr_t)rd32(r);
        nat->seqArray[i].uncompressed_len = rd16(r + 4);
        nat->seqArray[i].len = rd16(r + 6);
    }
    return nat;
}

#else /* 32-bit */

void *portBnkfPromote(void *blob, u32 size, u8 *tbl)
{
    (void)size;
    alBnkfNew(blob, tbl);
    return blob;
}

void *portSeqTablePromote(void *blob)
{
    return blob;
}

#endif
