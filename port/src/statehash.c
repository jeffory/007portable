/**
 * @file statehash.c
 * Deterministic-replay game-state hashing (roadmap phase 2).
 *
 * PORT_STATE_HASH=<tick>[,<tick>...]: at each named retrace tick, hash the
 * gameplay-visible state — RNG seed, Bond's position and view angles, and
 * every chr slot's action type and prop position — and print one line:
 *
 *     STATEHASH tick=<n> crc=<crc32>
 *
 * PORT_STATE_HASH_EXIT=1 exits 0 after the last named tick. Meaningful
 * under PORT_DETERMINISTIC (with input from PORT_INPUT_REPLAY or
 * PORT_AUTOSTART), where the hash is bit-reproducible: any divergence
 * means game logic changed. Called from portSchedPump's retrace.
 */
#include <ultra64.h>
#include <bondgame.h>
#include "game/player.h"
#include "game/chr.h"
#include "port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern u64 g_randomSeed;

static u32 sTicks[32];
static s32 sNumTicks = -1; /* -1 unchecked, 0 disabled */
static s32 sExitAfter;

static void parseTicks(void)
{
    const char *env = getenv("PORT_STATE_HASH");

    sNumTicks = 0;
    if (env == NULL || env[0] == '\0') {
        return;
    }
    while (*env != '\0' && sNumTicks < 32) {
        sTicks[sNumTicks++] = (u32)strtoul(env, (char **)&env, 10);
        if (*env == ',') {
            env++;
        } else {
            break;
        }
    }
    sExitAfter = getenv("PORT_STATE_HASH_EXIT") != NULL;
}

void portStateHashTick(u32 tick)
{
    u32 crc;
    s32 i;
    s32 wanted = 0;

    if (sNumTicks < 0) {
        parseTicks();
    }
    for (i = 0; i < sNumTicks; i++) {
        if (sTicks[i] == tick) {
            wanted = 1;
        }
    }
    if (!wanted) {
        return;
    }

    crc = portCrc32Update(0xFFFFFFFFu, &g_randomSeed, sizeof(g_randomSeed));

    if (g_CurrentPlayer != NULL) {
        if (g_CurrentPlayer->prop != NULL) {
            crc = portCrc32Update(crc, &g_CurrentPlayer->prop->pos,
                                  sizeof(g_CurrentPlayer->prop->pos));
        }
        crc = portCrc32Update(crc, &g_CurrentPlayer->vv_theta,
                              sizeof(g_CurrentPlayer->vv_theta));
        crc = portCrc32Update(crc, &g_CurrentPlayer->vv_verta,
                              sizeof(g_CurrentPlayer->vv_verta));
    }

    if (g_ChrSlots != NULL) {
        for (i = 0; i < g_NumChrSlots; i++) {
            u8 act = (u8)g_ChrSlots[i].actiontype;
            crc = portCrc32Update(crc, &act, 1);
            if (g_ChrSlots[i].prop != NULL) {
                crc = portCrc32Update(crc, &g_ChrSlots[i].prop->pos,
                                      sizeof(g_ChrSlots[i].prop->pos));
            }
        }
    }

    crc ^= 0xFFFFFFFFu;
    fprintf(stderr, "STATEHASH tick=%u crc=%08x\n", tick, crc);
    fflush(stderr);

    if (sExitAfter && sNumTicks > 0 && tick == sTicks[sNumTicks - 1]) {
        exit(0);
    }
}
