/**
 * @file preprocess_setup.c
 * Load-time byteswap for stage setup files (Usetup*Z) — PORT_PREPROCESS.
 *
 * The file is a stagesetup header (10 u32 file offsets) plus the sections
 * it points at: waypoints, waygroups, intro records, prop definitions,
 * patrol paths, AI lists, pads, bound pads and optional name tables.
 * Everything is big-endian on disk; proplvreset2() then rebases offsets to
 * pointers in place, so this must run right after the file is loaded and
 * before any parsing. Mirrors the walks in src/game/prop.c:1276-1450 and
 * bondview_r.c (intro records).
 *
 * Compiled with the game headers so record layouts and sizepropdef() are
 * the real ones.
 */
#include <ultra64.h>
#include <bondgame.h>
#include <platform_info.h>
#include "game/loadobjectmodel.h"
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

static void swapWords(void *ptr, s32 count)
{
    u32 *w = ptr;
    while (count-- > 0) {
        *w = swap32(*w);
        w++;
    }
}

static void swapHalves(void *ptr, s32 count)
{
    u16 *h = ptr;
    while (count-- > 0) {
        *h = swap16(*h);
        h++;
    }
}

/* s32 array terminated by -1 (order-invariant), swap it too. */
static void swapS32ListNeg1(u8 *base, u32 off)
{
    u32 *w = (u32 *)(base + off);
    while (*w != 0xFFFFFFFF) {
        *w = swap32(*w);
        w++;
    }
}

static void swapIntro(u8 *base, u32 off)
{
    u32 *w = (u32 *)(base + off);

    for (;;) {
        s32 type, words;

        *w = swap32(*w);
        type = (s32)*w;
        switch (type) {
        case INTROTYPE_SPAWN:   words = 3; break;
        case INTROTYPE_ITEM:    words = 4; break;
        case INTROTYPE_AMMO:    words = 4; break;
        case INTROTYPE_SWIRL:   words = 8; break;
        case INTROTYPE_ANIM:    words = 2; break;
        case INTROTYPE_CUFF:    words = 2; break;
        case INTROTYPE_CAMERA:  words = 10; break;
        case INTROTYPE_WATCH:   words = 3; break;
        case INTROTYPE_CREDITS: words = 2; break;
        case INTROTYPE_END:     return;
        default:
            fprintf(stderr, "port: setup intro: unknown type %d\n", type);
            return;
        }
        swapWords(w + 1, words - 1);
        w += words;
    }
}

/**
 * One prop definition record. The header is {u16 extrascale; u8 state;
 * u8 type} — only the u16 swaps, the type byte is position-stable so it can
 * be read before any swapping. Body layout varies per type; the default is
 * all 32-bit words, with halfword patches for the exceptions.
 */
static s32 swapPropDef(PropDefHeaderRecord *pdef)
{
    u8 *rec = (u8 *)pdef;
    s32 words = sizepropdef(pdef); /* record size in 32-bit words, incl. header */
    s32 type = pdef->type;

    swapHalves(rec, 1); /* extrascale */

    switch (type) {
    case PROPDEF_GUARD:
        /* chrnum..HeadID: 10 halfwords at 0x4..0x16, then pointers */
        swapHalves(rec + 4, 10);
        swapWords(rec + 0x18, words - 6);
        break;

    case PROPDEF_GUARD_ATTRIBUTE:
        swapWords(rec + 4, 1);   /* s32 chrnum */
        swapHalves(rec + 8, 1);  /* s16 unk8; s8 unkA/GrenadeProb stay */
        if (words > 3) {
            swapWords(rec + 12, words - 3);
        }
        break;

    case PROPDEF_TAG:
        swapHalves(rec + 4, 2);  /* u16 ID, s16 OffsetToObj */
        swapWords(rec + 8, words - 2);
        break;

    case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
    case PROPDEF_OBJECTIVE_COMPLETE_CONDITION:
    case PROPDEF_OBJECTIVE_FAIL_CONDITION:
    case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
    case PROPDEF_OBJECTIVE_PHOTOGRAPH:
    case PROPDEF_OBJECTIVE_ENTER_ROOM:
    case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM:
    case PROPDEF_OBJECTIVE_COPY_ITEM:
        /* MissionObjectiveRecord / criteria_* shapes: every body field is a
         * 32-bit word (ObjRefID / pad / weaponnum @4, TextID / status @8,
         * MinDificulty / flag @0xC, runtime list pointer after). The old
         * halves@4 treatment mangled the tag/pad ids read by
         * get_status_of_objective and objectivestatusCheck*. */
        swapWords(rec + 4, words - 1);
        break;

    case PROPDEF_WATCH_MENU_OBJECTIVE_TEXT:
        /* struct watchMenuObjectiveText: u32 menu @4; u16 reserved @8;
         * u16 text @0xA; runtime next pointer @0xC. The old halves@4 +
         * words@8 treatment turned menu into menu<<16 and text into 0, so
         * the watch's mission-briefing tab called langGet(0) and crashed on
         * the NULL g_LangBanks[0]. */
        swapWords(rec + 4, 1);
        if (words > 2) {
            swapHalves(rec + 8, 2);
        }
        if (words > 3) {
            swapWords(rec + 0xC, words - 3);
        }
        break;

    case PROPDEF_OBJECTIVE_START:
        /* struct objective_entry: u32 menu @4; u16 reserved @8; u16 text
         * @0xA; u16 unkC @0xC; u8 unkD @0xE; s8 difficulty @0xF. The old
         * all-words treatment zeroed the text id (briefing objectives
         * sub-page) and read the wrong byte as the difficulty. */
        swapWords(rec + 4, 1);
        if (words > 2) {
            swapHalves(rec + 8, 2);
        }
        if (words > 3) {
            swapHalves(rec + 0xC, 1); /* unkD/difficulty are bytes: leave */
        }
        break;

    case PROPDEF_GAS_RELEASING:
        /* ObjectRecord-shaped (prop.c casts it for domakedefaultobj):
         * u16 unk4 + u16 unk6, then words */
        swapHalves(rec + 4, 2);
        if (words > 2) {
            swapWords(rec + 8, words - 2);
        }
        break;

    default:
        if (words > 1) {
            /* ObjectRecord family starts with s16 obj / s16 pad. Types that
             * are PropDefHeader-only with a pure word body (door scale,
             * link, switch, rename, lock, objectives 23/24) hold s32s
             * there; both shapes swap correctly as two halves ONLY for the
             * ObjectRecord shape, so pick by family. */
            switch (type) {
            case PROPDEF_DOOR_SCALE:
            case PROPDEF_LINK:
            case PROPDEF_SWITCH:
            case PROPDEF_OBJECTIVE_END:
            case PROPDEF_OBJECTIVE_NULL:
            case PROPDEF_RENAME:
            case PROPDEF_LOCK_DOOR:
                swapWords(rec + 4, words - 1);
                break;
            default:
                /* ObjectRecord-based (door, prop, key, alarm, cctv, weapon,
                 * ammo, armour, monitors, autogun, vehicles, ...) */
                swapHalves(rec + 4, 2);
                swapWords(rec + 8, words - 2);
                /* halfword patches inside extensions */
                if (type == PROPDEF_AMMO) {
                    /* MultiAmmoCrateRecord: u16 slot pairs from 0x80 */
                    s32 bytes = words * 4;
                    if (bytes > 0x80) {
                        swapWords(rec + 0x80, (bytes - 0x80) / 4);  /* undo */
                        swapHalves(rec + 0x80, (bytes - 0x80) / 2); /* redo */
                    }
                } else if (type == PROPDEF_COLLECTABLE || type == PROPDEF_MAGAZINE) {
                    /* WeaponObjRecord: s8,s8,s16 at 0x80 */
                    if (words * 4 > 0x80) {
                        swapWords(rec + 0x80, 1);   /* undo */
                        swapHalves(rec + 0x82, 1);  /* s16 timer */
                    }
                } else if (type == PROPDEF_VEHICHLE || type == PROPDEF_AIRCRAFT) {
                    /* u16 aioffset / s16 aireturnlist at 0x84 */
                    if (words * 4 > 0x84) {
                        swapWords(rec + 0x84, 1);
                        swapHalves(rec + 0x84, 2);
                    }
                } else if (type == PROPDEF_DOOR) {
                    /* DoorRecord: u16 doorFlags / u16 doorType at 0x98. The
                     * word swap exchanges the two halves (Dam's sliding gates
                     * became doorFlags=0/doorType=12, losing CLIP_TO_BBOX and
                     * FLIP, so they slid the wrong way and jammed partway on
                     * collision). Redo as halfwords. The 0xC4 word (s16
                     * CullDist; s8 soundType; s8 fadeTime60) stays
                     * word-swapped: its only consumer reads it as a raw s32
                     * (propobj.c:5839), which the word swap reproduces. */
                    swapWords(rec + 0x98, 1);   /* undo */
                    swapHalves(rec + 0x98, 2);  /* redo */
                    if (getenv("PORT_DOOR_TRACE") != NULL) {
                        fprintf(stderr,
                                "port/door: obj %d pad %d link %d maxFrac %08x "
                                "perim %08x accel %08x maxSpeed %08x flags %04x "
                                "type %04x key %08x autoclose %u\n",
                                *(s16 *)(rec + 4), *(s16 *)(rec + 6),
                                *(s32 *)(rec + 0x80), *(u32 *)(rec + 0x84),
                                *(u32 *)(rec + 0x88), *(u32 *)(rec + 0x8c),
                                *(u32 *)(rec + 0x94), *(u16 *)(rec + 0x98),
                                *(u16 *)(rec + 0x9a), *(u32 *)(rec + 0x9c),
                                *(u32 *)(rec + 0xa0));
                    }
                }
                break;
            }
        }
        break;
    }
    return words;
}

static void *sSetupBlobBase; /* for blob-relative fixups (credits records) */

void *portSetupFileBlobBase(void)
{
    return sSetupBlobBase;
}

#if IS_64_BIT
/* ---------------------------------------------------------------------------
 * 64-bit: the setup blob's fixed-size record arrays (header, waypoints,
 * waygroups, ai list records, patrol paths, pads, bound pads, name tables)
 * are indexed in place through native structs with 8-byte pointer members,
 * so they must be rebuilt in native layout. Reference slots keep working
 * with prop.c's untouched rebase loops because every stored value V is
 * crafted so that (u32)nativeHeader + (u32)V == the real target (u32
 * wraparound math — all game memory sits below 4GB).
 *
 * Variable-size sections stay in the blob: neighbour/waypoint id lists,
 * ai bytecode, pad name strings, intro records. PROP DEFINITIONS are NOT
 * yet expanded (each type is an in-place ObjectRecord variant with many
 * pointer members): the rebuilt header points at an empty PROPDEF_END
 * terminator, so 64-bit stages boot with no objects/guards. PORT_TODO(M6).
 * ------------------------------------------------------------------------- */

static void *portRebuildSetupFile64(u8 *base)
{
    u32 *hdr = (u32 *)base;
    s32 nwp = 0, nwg = 0, nai = 0, npath = 0, npad = 0, nbpad = 0, npn = 0, nbpn = 0;
    u32 introBytes = 0;
    u32 total;
    u8 *nat;
    u8 *cur;
    stagesetup *nh;
    uintptr_t craftbase;
    s32 i;

#define CRAFT(blobOff) ((void *)(uintptr_t)(u32)((uintptr_t)(base + (blobOff)) - craftbase))

    /* count records in each section */
    if (hdr[0]) { u32 *w = (u32 *)(base + hdr[0]); while ((s32)w[0] >= 0) { nwp++; w += 4; } nwp++; /* incl. terminator */ }
    if (hdr[1]) { u32 *w = (u32 *)(base + hdr[1]); while (w[0] != 0) { nwg++; w += 3; } }
    if (hdr[5]) { u32 *w = (u32 *)(base + hdr[5]); while (w[0] != 0) { nai++; w += 2; } }
    if (hdr[4]) { u32 *w = (u32 *)(base + hdr[4]); while (w[0] != 0) { npath++; w += 2; } }
    if (hdr[6]) { u32 *w = (u32 *)(base + hdr[6]); while (w[9] != 0) { npad++; w += 11; } }
    if (hdr[7]) { u32 *w = (u32 *)(base + hdr[7]); while (w[9] != 0) { nbpad++; w += 17; } }
    if (hdr[8]) { u32 *w = (u32 *)(base + hdr[8]); while (*w != 0) { npn++; w++; } }
    if (hdr[9]) { u32 *w = (u32 *)(base + hdr[9]); while (*w != 0) { nbpn++; w++; } }

    /* intro stream: records are parsed via native sizeof strides and the
     * CAMERA record holds pointer unions, so the whole stream is re-emitted
     * natively. All types except CAMERA keep 4*words native size. */
    {
        u32 *w = hdr[2] ? (u32 *)(base + hdr[2]) : NULL;
        introBytes = 16; /* END record + slack */
        while (w != NULL) {
            s32 t = (s32)w[0];
            s32 words =
                t == INTROTYPE_SPAWN ? 3 : t == INTROTYPE_ITEM ? 4 :
                t == INTROTYPE_AMMO ? 4 : t == INTROTYPE_SWIRL ? 8 :
                t == INTROTYPE_ANIM ? 2 : t == INTROTYPE_CUFF ? 2 :
                t == INTROTYPE_CAMERA ? 10 : t == INTROTYPE_WATCH ? 3 :
                t == INTROTYPE_CREDITS ? 2 : 0;
            if (words == 0) {
                break; /* END or unknown */
            }
            introBytes += (t == INTROTYPE_CAMERA) ? (u32)sizeof(SetupIntroCamera)
                                                  : (u32)words * 4;
            w += words;
        }
    }

    total = sizeof(stagesetup)
          + (u32)nwp * sizeof(waypoint)
          + (u32)(nwg + 1) * sizeof(waygroup)
          + (u32)(nai + 1) * sizeof(AIListRecord)
          + (u32)(npath + 1) * sizeof(PathRecord)
          + (u32)(npad + 1) * sizeof(PadRecord)
          + (u32)(nbpad + 1) * sizeof(BoundPadRecord)
          + (u32)(npn + 1) * sizeof(pname)
          + (u32)(nbpn + 1) * sizeof(pname)
          + introBytes
          + 16 /* empty propdef terminator */ + 64 /* alignment slop */;

    nat = portLowAlloc(total);
    if (nat == NULL) {
        fprintf(stderr, "port/setup64: alloc %u failed\n", total);
        exit(1);
    }
    memset(nat, 0, total);
    nh = (stagesetup *)nat;
    cur = nat + sizeof(stagesetup);
    craftbase = (uintptr_t)nat;

    /* waypoints (terminator record included; its padID is negative) */
    if (hdr[0]) {
        u32 *w = (u32 *)(base + hdr[0]);
        waypoint *d = (waypoint *)cur;

        nh->pathwaypoints = CRAFT((u8 *)d - base); /* self-relative: see below */
        nh->pathwaypoints = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < nwp; i++, w += 4) {
            d[i].padID = (s32)w[0];
            d[i].neighbours = w[1] ? (void *)(uintptr_t)(u32)((uintptr_t)(base + w[1]) - craftbase)
                                   : NULL;
            d[i].groupNum = (s32)w[2];
            d[i].dist = (s32)w[3];
        }
        cur = (u8 *)(d + nwp);
    }

    /* waygroups (terminator: neighbours slot must stay 0) */
    {
        u32 *w = hdr[1] ? (u32 *)(base + hdr[1]) : NULL;
        waygroup *d = (waygroup *)cur;

        nh->waypointgroups = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < nwg; i++, w += 3) {
            d[i].neighbours = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[0]) - craftbase);
            d[i].waypoints = w[1] ? (void *)(uintptr_t)(u32)((uintptr_t)(base + w[1]) - craftbase)
                                  : NULL;
            d[i].dist = (s32)w[2];
        }
        cur = (u8 *)(d + nwg + 1);
    }

    /* intro records: re-emit natively (variable native strides; CAMERA
     * grows for its pointer unions) */
    if (hdr[2]) {
        u32 *w = (u32 *)(base + hdr[2]);
        u8 *d = cur;

        nh->intro = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (;;) {
            s32 t = (s32)w[0];
            s32 words =
                t == INTROTYPE_SPAWN ? 3 : t == INTROTYPE_ITEM ? 4 :
                t == INTROTYPE_AMMO ? 4 : t == INTROTYPE_SWIRL ? 8 :
                t == INTROTYPE_ANIM ? 2 : t == INTROTYPE_CUFF ? 2 :
                t == INTROTYPE_CAMERA ? 10 : t == INTROTYPE_WATCH ? 3 :
                t == INTROTYPE_CREDITS ? 2 : 0;

            if (words == 0) {
                *(s32 *)d = t; /* END (or unknown: stop the parser safely) */
                d += 4;
                break;
            }
            if (t == INTROTYPE_CAMERA) {
                SetupIntroCamera *c = (SetupIntroCamera *)d;

                memcpy(c, w, 0x18);                     /* type + 5 fields + pad */
                c->lang1c.lang_index[0] = ((u16 *)w)[0x1C / 2];
                c->lang1c.lang_index[1] = ((u16 *)w)[0x1E / 2];
                c->lang20.lang_index = (s32)w[0x20 / 4];
                c->prev = NULL;
                d += sizeof(SetupIntroCamera);
            } else {
                memcpy(d, w, (u32)words * 4);
                if (t == INTROTYPE_CREDITS) {
                    /* unk04 is a blob-relative offset; bondview_r adds
                     * g_ptrStageSetupFile (the native header), so craft it */
                    *(u32 *)(d + 4) = (u32)((uintptr_t)(base + w[1]) - craftbase);
                }
                d += (u32)words * 4;
            }
            w += words;
        }
        cur = d + 8;
    } else {
        nh->intro = NULL;
    }

    /* ai list records (bytecode stays in the blob) */
    {
        u32 *w = hdr[5] ? (u32 *)(base + hdr[5]) : NULL;
        AIListRecord *d = (AIListRecord *)cur;

        nh->ailists = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < nai; i++, w += 2) {
            d[i].ailist = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[0]) - craftbase);
            d[i].ID = (s32)w[1];
        }
        cur = (u8 *)(d + nai + 1);
    }

    /* patrol paths (id lists stay in the blob) */
    {
        u32 *w = hdr[4] ? (u32 *)(base + hdr[4]) : NULL;
        PathRecord *d = (PathRecord *)cur;

        nh->patrolpaths = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < npath; i++, w += 2) {
            d[i].waypoints = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[0]) - craftbase);
            d[i].ID = ((u8 *)&w[1])[0];
            d[i].isLoop = ((u8 *)&w[1])[1];
            d[i].len = 0; /* computed at load */
        }
        cur = (u8 *)(d + npath + 1);
    }

    /* pads: 9 f32 + plink offset + stan (runtime) */
    {
        u32 *w = hdr[6] ? (u32 *)(base + hdr[6]) : NULL;
        PadRecord *d = (PadRecord *)cur;

        nh->pads = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < npad; i++, w += 11) {
            memcpy(&d[i], w, 36); /* pos/up/look floats */
            d[i].plink = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[9]) - craftbase);
            d[i].stan = NULL;
        }
        cur = (u8 *)(d + npad + 1);
    }

    /* bound pads: pads + bbox */
    {
        u32 *w = hdr[7] ? (u32 *)(base + hdr[7]) : NULL;
        BoundPadRecord *d = (BoundPadRecord *)cur;

        nh->boundpads = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < nbpad; i++, w += 17) {
            memcpy(&d[i], w, 36);
            d[i].plink = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[9]) - craftbase);
            d[i].stan = NULL;
            memcpy(&d[i].bbox, w + 11, 24);
        }
        cur = (u8 *)(d + nbpad + 1);
    }

    /* pad name / bound pad name tables (strings stay in the blob) */
    if (hdr[8]) {
        u32 *w = (u32 *)(base + hdr[8]);
        pname *d = (pname *)cur;

        nh->padnames = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < npn; i++) {
            d[i].p = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[i]) - craftbase);
        }
        cur = (u8 *)(d + npn + 1);
    } else {
        nh->padnames = NULL;
    }
    if (hdr[9]) {
        u32 *w = (u32 *)(base + hdr[9]);
        pname *d = (pname *)cur;

        nh->boundpadnames = (void *)(uintptr_t)(u32)((uintptr_t)d - craftbase);
        for (i = 0; i < nbpn; i++) {
            d[i].p = (void *)(uintptr_t)(u32)((uintptr_t)(base + w[i]) - craftbase);
        }
        cur = (u8 *)(d + nbpn + 1);
    } else {
        nh->boundpadnames = NULL;
    }

    /* PORT_TODO(M6): prop definitions are in-place ObjectRecord variants
     * and need per-type native expansion; give the game an empty list so
     * the stage boots without objects/guards. */
    {
        PropDefHeaderRecord *term = (PropDefHeaderRecord *)cur;

        term->type = PROPDEF_END;
        nh->propDefs = (void *)(uintptr_t)(u32)((uintptr_t)term - craftbase);
        cur += 16;
        if (hdr[3]) {
            fprintf(stderr, "port/setup64: prop definitions not yet expanded on "
                            "64-bit; stage objects/guards disabled (PORT_TODO)\n");
        }
    }

    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/setup64: rebuilt %d wp %d wg %d ai %d path %d pad %d bpad at %p (%u bytes)\n",
                nwp, nwg, nai, npath, npad, nbpad, (void *)nat, total);
    }
#undef CRAFT
    return nat;
}
#endif /* IS_64_BIT */

void *portSwapSetupFile(void *data)
{
    u8 *base = data;
    u32 *hdr = data;
    u32 off;
    u32 *w;

    swapWords(hdr, 10);

    /* waypoints: {s32 padID; off neighbours; s32 group; s32 dist} until padID < 0 */
    off = hdr[0];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            swapWords(w, 4);
            if ((s32)w[0] < 0) {
                break;
            }
            if (w[1] != 0) {
                swapS32ListNeg1(base, w[1]);
            }
            w += 4;
        }
    }

    /* waygroups: {off neighbours; off waypoints; s32 dist} until neighbours == 0 */
    off = hdr[1];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            swapWords(w, 3);
            if (w[0] == 0) {
                break;
            }
            swapS32ListNeg1(base, w[0]);
            if (w[1] != 0) {
                swapS32ListNeg1(base, w[1]);
            }
            w += 3;
        }
    }

    /* intro records */
    if (hdr[2] != 0) {
        swapIntro(base, hdr[2]);

        /* PORT_SPAWN_PAD=<n>: debug aid — retarget the spawn intro record's
         * pad so a stage can be entered at an arbitrary pad. */
        if (getenv("PORT_SPAWN_PAD") != NULL) {
            u32 *w = (u32 *)(base + hdr[2]);
            for (;;) {
                s32 t = (s32)*w;
                s32 words =
                    t == INTROTYPE_SPAWN ? 3 : t == INTROTYPE_ITEM ? 4 :
                    t == INTROTYPE_AMMO ? 4 : t == INTROTYPE_SWIRL ? 8 :
                    t == INTROTYPE_ANIM ? 2 : t == INTROTYPE_CUFF ? 2 :
                    t == INTROTYPE_CAMERA ? 10 : t == INTROTYPE_WATCH ? 3 :
                    t == INTROTYPE_CREDITS ? 2 : 0;
                if (t == INTROTYPE_SPAWN) {
                    w[1] = (u32)atoi(getenv("PORT_SPAWN_PAD"));
                    fprintf(stderr, "port: spawn pad override -> %u\n", w[1]);
                    break;
                }
                if (words == 0) {
                    break;
                }
                w += words;
            }
        }
    }

    /* prop definitions until PROPDEF_END (AI bytecode inside guards etc.
     * stays byte-ordered; only the records swap) */
    off = hdr[3];
    if (off != 0) {
        PropDefHeaderRecord *pdef = (PropDefHeaderRecord *)(base + off);
        while (pdef->type != PROPDEF_END) {
            s32 words = swapPropDef(pdef);
            pdef = (PropDefHeaderRecord *)((u32 *)pdef + words);
        }
        swapHalves(pdef, 1); /* terminator's extrascale, for completeness */
    }

    /* patrol paths: {off waypoints; u8 ID; u8 isLoop; u16 len} until off == 0.
     * len is computed at load time; ID/isLoop are bytes — only word 0 swaps. */
    off = hdr[4];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            w[0] = swap32(w[0]);
            if (w[0] == 0) {
                break;
            }
            swapS32ListNeg1(base, w[0]);
            w += 2;
        }
    }

    /* ai lists: {off ailist; s32 ID} until ailist == 0; bytecode is u8s */
    off = hdr[5];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            swapWords(w, 2);
            if (w[0] == 0) {
                break;
            }
            w += 2;
        }
    }

    /* pads: {9 f32; off plink; ptr stan} until plink == 0 */
    off = hdr[6];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            swapWords(w, 11);
            if (w[9] == 0) {
                break;
            }
            w += 11;
        }
    }

    /* bound pads: pads + bbox (6 f32) */
    off = hdr[7];
    if (off != 0) {
        w = (u32 *)(base + off);
        for (;;) {
            swapWords(w, 17);
            if (w[9] == 0) {
                break;
            }
            w += 17;
        }
    }

    /* pad name / bound pad name offset tables (strings stay as bytes) */
    if (hdr[8] != 0) {
        w = (u32 *)(base + hdr[8]);
        while (*w != 0) {
            *w = swap32(*w);
            w++;
        }
    }
    if (hdr[9] != 0) {
        w = (u32 *)(base + hdr[9]);
        while (*w != 0) {
            *w = swap32(*w);
            w++;
        }
    }

    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port: setup file swapped (base %p)\n", data);
    }

    sSetupBlobBase = data;
#if IS_64_BIT
    return portRebuildSetupFile64(data);
#else
    return data;
#endif
}
