/**
 * @file preprocess.c
 * Load-time byteswap helpers (assets are big-endian on ROM). M1 carries
 * just the boot-path swaps; the general per-format preprocessing pass
 * (bg/stan/prop/chr/setup) is M2/M3 work, PD-port style.
 */
#include <ultra64.h>
#include "port.h"
#include <stdio.h>
#include <stdlib.h>

static u32 swap32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

/* Swap a buffer of 32-bit words in place (size in bytes). */
void portSwapU32InPlace(void *data, u32 size)
{
    u32 *p = data;
    u32 n = size / 4;
    u32 i;

    for (i = 0; i < n; i++) {
        p[i] = swap32(p[i]);
    }
}

struct portgitspan { u32 off, size, kind; };
#include "globalimagetable_spans.inc"
#include "rarewarelogo_spans.inc"

static u16 swap16v(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

/**
 * Apply a generated blob span table: kind 0 = Gfx words, kind 1 =
 * sImageTableEntry stride 12 (swap the u32 index), kind 2 = Vtx arrays
 * (halfwords at 0x0..0xB, color/normal bytes stay).
 */
static void applySpans(void *data, u32 size, const struct portgitspan *spans, u32 nspans)
{
    u32 i, o;
    u8 *base = data;

    for (i = 0; i < nspans; i++) {
        const struct portgitspan *sp = &spans[i];

        if (sp->off + sp->size > size) {
            continue;
        }
        switch (sp->kind) {
        case 0:
            portSwapU32InPlace(base + sp->off, sp->size);
            break;
        case 1:
            for (o = 0; o + 12 <= sp->size; o += 12) {
                u32 *idx = (u32 *)(base + sp->off + o);

                *idx = swap32(*idx);
            }
            break;
        case 2:
            for (o = 0; o + 16 <= sp->size; o += 16) {
                u32 h;

                for (h = 0; h < 12; h += 2) {
                    u16 *p = (u16 *)(base + sp->off + o + h);

                    *p = swap16v(*p);
                }
            }
            break;
        }
    }
}

void portSwapGlobalImagetable(void *data, u32 size)
{
    applySpans(data, size,
               sGlobalImagetableSpans,
               sizeof(sGlobalImagetableSpans) / sizeof(sGlobalImagetableSpans[0]));
}

void portSwapRarewareLogo(void *data, u32 size)
{
    applySpans(data, size,
               sRarewareLogoSpans,
               sizeof(sRarewareLogoSpans) / sizeof(sRarewareLogoSpans[0]));
}


/**
 * Swap one big-endian ModelAnimation header (0x40 bytes) plus its
 * ModelAnimBitField descriptor records, which occupy [bitDescriptors,
 * bitStream) as data-relative offsets before promotion. The frame bit
 * stream itself is read byte-wise and stays untouched. A seen-list guards
 * against double swaps when both promote tables reference a header.
 */
void portSwapAnimHeader(void *header, void *blobBase)
{
    static u32 seen[1024];
    static u32 nseen;
    u8 *h = header;
    u8 *base = blobBase;
    u32 d0, d1, off, i;
    u16 frames, stride;

    for (i = 0; i < nseen; i++) {
        if (seen[i] == (u32)h) {
            return;
        }
    }

    /* plausibility check BEFORE touching anything: a real header has sane
     * frame/stride counts and a small descriptor region ending at the
     * bitstream. A bogus pointer (mis-resolved table entry) would otherwise
     * swap 0x30 bytes of a NEIGHBORING anim's descriptors. */
    frames = swap16v(*(u16 *)(h + 0x04));
    stride = swap16v(*(u16 *)(h + 0x0C));
    d0 = swap32(*(u32 *)(h + 0x08));
    d1 = swap32(*(u32 *)(h + 0x10));
    if (frames == 0 || frames > 4000 || stride > 0x800 ||
        d1 <= d0 || d1 - d0 > 0x2000 || (d1 - d0) % 6 != 0) {
        if (getenv("PORT_ANIM_TRACE") != NULL) {
            fprintf(stderr,
                    "port/anim: REJECT header %p (off 0x%x): frames=%u stride=%u d0=0x%x d1=0x%x\n",
                    header, (u32)(h - base), frames, stride, d0, d1);
        }
        return;
    }
    if (getenv("PORT_ANIM_TRACE") != NULL) {
        fprintf(stderr, "port/anim: swap header off 0x%x frames=%u desc=[0x%x,0x%x)\n",
                (u32)(h - base), frames, d0, d1);
    }

    if (nseen < 1024) {
        seen[nseen++] = (u32)h;
    }

    portSwapU32InPlace(h + 0x00, 4);   /* address (ROM offset) */
    *(u16 *)(h + 0x04) = swap16v(*(u16 *)(h + 0x04)); /* frames */
    portSwapU32InPlace(h + 0x08, 4);   /* bitDescriptors (offset) */
    *(u16 *)(h + 0x0C) = swap16v(*(u16 *)(h + 0x0C));
    *(u16 *)(h + 0x0E) = swap16v(*(u16 *)(h + 0x0E));
    portSwapU32InPlace(h + 0x10, 4);   /* bitStream (offset) */
    /* The on-disk header is ONLY 0x14 bytes: the blob layout is
     * [descriptors][bitstream][header] back-to-back per anim (verified:
     * idle = desc@0, bs@0x18, header@0x1C, next anim's desc@0x30), so
     * bytes past +0x14 are the NEXT anim's descriptors. The old
     * +0x14..0x30 swap here was trampling them — the cause of the
     * garbage attack-anim root motion. No engine field past +0x10 is
     * ever read. */

    d0 = *(u32 *)(h + 0x08);
    d1 = *(u32 *)(h + 0x10);
    if (d1 > d0 && d1 - d0 < 0x10000) {
        for (off = d0; off + 6 <= d1; off += 6) {
            *(u16 *)(base + off + 0) = swap16v(*(u16 *)(base + off + 0));
            *(u16 *)(base + off + 4) = swap16v(*(u16 *)(base + off + 4));
        }
    }
}

/**
 * Swap a loaded mission briefing file (Ubrief*Z): 4 u16 brief text ids
 * followed by OBJECTIVES_MAX(10) pairs of {u16 textid, u16 difficulty}.
 * Unswapped ids sent through langGet() yield wild text pointers whose
 * high bytes then take the JPN glyph path into a NULL cache (crash).
 */
void portSwapBriefingData(void *data)
{
    u16 *p = data;
    u32 i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < 4 + 10 * 2; i++) {
        p[i] = swap16v(p[i]);
    }
}

/**
 * Swap the leading u32 offset table of a loaded text bank (L*.seg).
 * The table runs until the first string byte; its end is discovered as the
 * smallest nonzero offset seen while swapping (offsets point past the
 * table, so the bound only shrinks).
 */
void portSwapTextBank(u32 *bank)
{
    u32 end = 0xFFFFFFFF;
    u32 i;

    if (bank == NULL) {
        return;
    }
    for (i = 0; i * 4 < end && i < 0x400; i++) {
        bank[i] = swap32(bank[i]);
        if (bank[i] != 0 && bank[i] < end) {
            end = bank[i];
        }
    }
}
