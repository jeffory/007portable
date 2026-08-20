/**
 * @file main.c
 * PC port entry point. Replaces boot.s/_start.s/init.c/mainproc():
 * no data decompression (everything is linked normally), no TLB handler,
 * no threads — set up the platform, ROM, arena, and scheduler pump, then
 * call bossEntry() directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "port.h"

#if IS_64_BIT
#include <ucontext.h>
#endif

struct portconfig g_PortConfig = {
    "data/ge007.u.z64", /* rompath */
    0,                  /* selftest */
    0,                  /* stubstage */
    0,                  /* verbose */
    NULL,               /* stage */
    NULL,               /* hard */
};

void bossEntry(void);
void tokenSetString(const char *str);
u32 decompressdata(u8 *src, u8 *dst, void *huffman_table);
void romCopy(void *target, void *source, u32 size);
void romCreateMesgQueue(void);

/* --- --selftest: prove romCopy + 1172 decompression work on x86 ------------- */
#define SELFTEST_ROM_OFF  4425312   /* bg/bg_sev_all_p from filelist.u.csv */
#define SELFTEST_ROM_LEN  69104

static int runSelftest(void)
{
    static u8 src[SELFTEST_ROM_LEN + 16];
    static u8 dst[1024 * 1024];
    u32 crc = 0;
    u32 outlen;
    u32 i;

    romCreateMesgQueue();
    romCopy(src, (void *)SELFTEST_ROM_OFF, SELFTEST_ROM_LEN);

    fprintf(stderr, "selftest: first bytes: %02x %02x %02x %02x\n",
            src[0], src[1], src[2], src[3]);

    if (src[0] == 0x11 && src[1] == 0x72) {
        outlen = decompressdata(src, dst, NULL);
        fprintf(stderr, "selftest: 1172 decompress -> %u bytes\n", outlen);
        for (i = 0; i < outlen; i++) {
            crc = (crc << 1 | crc >> 31) ^ dst[i];
        }
    } else {
        /* stored uncompressed in ROM; CRC the raw data */
        for (i = 0; i < SELFTEST_ROM_LEN; i++) {
            crc = (crc << 1 | crc >> 31) ^ src[i];
        }
    }

    fprintf(stderr, "selftest: crc %08x\n", crc);
    if (crc != 0x88ecb6f6) { /* recorded from the verified US ROM */
        fprintf(stderr, "selftest: FAIL (expected crc 88ecb6f6)\n");
        return 1;
    }

    /* golden CRCs of the 18 compiled-in AI arrays (byte-exact vs the retail
     * ROM as of M3); emitted as CRCTRACE lines when PORT_CRC_TRACE is set,
     * pinned by port/tests/goldens/. */
    {
        int i;
        for (i = 0; i < g_PortAiArrayCount; i++) {
            portCrcTrace("ai", (u32)i, g_PortAiArrays[i].data, g_PortAiArrays[i].len);
            fprintf(stderr, "selftest: ai %-26s len=%-5u crc=%08x\n",
                    g_PortAiArrays[i].name, g_PortAiArrays[i].len,
                    portCrc32(g_PortAiArrays[i].data, g_PortAiArrays[i].len));
        }
    }

    fprintf(stderr, "selftest: PASS\n");
    return 0;
}

static void parseArgs(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--selftest") == 0) {
            g_PortConfig.selftest = 1;
        } else if (strcmp(argv[i], "--stub-stage") == 0) {
            g_PortConfig.stubstage = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_PortConfig.verbose = 1;
        } else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            g_PortConfig.rompath = argv[++i];
        } else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) {
            g_PortConfig.stage = argv[++i];
        } else if (strcmp(argv[i], "--hard") == 0 && i + 1 < argc) {
            g_PortConfig.hard = argv[++i];
        } else {
            fprintf(stderr,
                    "usage: %s [--rom path] [--selftest] [--stub-stage] [--verbose]\n"
                    "          [--stage <name|levelid>] [--hard <0-3>]\n",
                    argv[0]);
            exit(2);
        }
    }
}

/* Boot straight into a stage using Rare's own devkit boot-string mechanism:
 * boss.c parses "-level_NN" / "-hardN" out of the token string that the
 * devkit host passed at 0xFFB000. We just plant the same string. */
static void applyStageOverride(void)
{
    static const struct { const char *name; int id; } stages[] = {
        { "dam", 33 },      { "facility", 34 }, { "runway", 35 },
        { "surface", 36 },  { "bunker1", 9 },   { "silo", 20 },
        { "frigate", 26 },  { "surface2", 42 }, { "bunker2", 27 },
        { "statue", 22 },   { "archives", 24 }, { "streets", 29 },
        { "depot", 30 },    { "train", 25 },    { "jungle", 37 },
        { "control", 23 },  { "caverns", 39 },  { "cradle", 41 },
        { "aztec", 28 },    { "egypt", 32 },    { "complex", 31 },
        { "temple", 38 },   { "citadel", 40 },  { "cuba", 53 },
    };
    const char *stage = g_PortConfig.stage ? g_PortConfig.stage : getenv("PORT_BOOT_STAGE");
    const char *hard = g_PortConfig.hard ? g_PortConfig.hard : getenv("PORT_HARD");
    char tok[64];
    int id = -1;
    unsigned int i;

    if (stage == NULL) {
        return;
    }
    if (stage[0] >= '0' && stage[0] <= '9') {
        id = atoi(stage);
    } else {
        for (i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
            if (strcmp(stage, stages[i].name) == 0) {
                id = stages[i].id;
                break;
            }
        }
    }
    if (id < 0 || id > 99) {
        fprintf(stderr, "port: unknown stage '%s'\n", stage);
        exit(2);
    }
    /* -mt <KB>: Rare's own texture-pool-size token; the N64 default is too
     * small for llvmpipe-visible full-stage texture sets, and RAM is ample. */
    snprintf(tok, sizeof(tok), "port -level_%02d -mt4096%s%s%s%s",
             id, hard != NULL ? " -hard" : "", hard != NULL ? hard : "",
             getenv("PORT_TOKENS") != NULL ? " " : "",
             getenv("PORT_TOKENS") != NULL ? getenv("PORT_TOKENS") : "");
    tokenSetString(tok);
    fprintf(stderr, "port/note: stage override: \"%s\"\n", tok);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0); /* keep printf diagnostics when killed */

    parseArgs(argc, argv);

    if (portRomLoad(g_PortConfig.rompath) != 0) {
        return 1;
    }

    if (g_PortConfig.selftest) {
        return runSelftest();
    }

    if (portPlatformInit("GoldenEye 007 (PC port, M1)", 320, 240) != 0) {
        return 1;
    }

    portMemInit();
    portSchedInit();
    portVideoInit();
    applyStageOverride();

#if IS_64_BIT
    /* M6 PORT_TODO: the libultra audio path still promotes 32-bit offsets
     * inside ALBankFile/seq blobs through native-width structs (alBnkfNew
     * & friends), which is not yet 64-bit clean. Use Rare's own boot
     * switch to disable sound unless the user opts in to debug it. */
    if (getenv("PORT_64_AUDIO") == NULL) {
        extern s8 g_sndBootswitchSound;
        extern s16 *g_sndSfxSlotVolume;
        extern s16 *g_sndSfxSlotNaturalVolume;
        static s16 sDummySlotVolume[16];
        static s16 sDummySlotNaturalVolume[16];
        int i;

        g_sndBootswitchSound = 1;
        /* with the boot switch set, sndNewPlayerInit never runs, but
         * lvlStageLoad still pokes the sfx slot volume tables */
        for (i = 0; i < 16; i++) {
            sDummySlotVolume[i] = sDummySlotNaturalVolume[i] = 0x7FFF;
        }
        g_sndSfxSlotVolume = sDummySlotVolume;
        g_sndSfxSlotNaturalVolume = sDummySlotNaturalVolume;
        fprintf(stderr, "port: 64-bit build: audio disabled (set PORT_64_AUDIO=1 to attempt)\n");
    }
#endif

#if IS_64_BIT
    /* Run the game on a stack allocated below 4GB. Shared code truncates
     * addresses of stack locals through (u32) casts in many places (e.g.
     * image.c aligns a DMA buffer with ((u32)buf + 0xF) >> 4 << 4); the
     * host's default stack lives above 4GB, so give the game one that
     * satisfies the same invariant as the arena/ROM/statics. Everything
     * is cooperative on this one context (threads.c is a no-op), so no
     * other stack ever runs game code. */
    {
        static ucontext_t mainCtx, bossCtx;
        u32 stackSize = 16 * 1024 * 1024;
        void *stack = portLowAlloc(stackSize);

        if (stack == NULL || getcontext(&bossCtx) != 0) {
            fprintf(stderr, "port: failed to set up low-memory game stack\n");
            return 1;
        }
        bossCtx.uc_stack.ss_sp = stack;
        bossCtx.uc_stack.ss_size = stackSize;
        bossCtx.uc_link = &mainCtx;
        makecontext(&bossCtx, bossEntry, 0);
        swapcontext(&mainCtx, &bossCtx); /* never returns (bossEntry loops) */
    }
#else
    bossEntry(); /* never returns */
#endif
    return 0;
}
