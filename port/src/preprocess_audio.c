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
}
