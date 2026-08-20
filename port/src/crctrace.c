/**
 * Phase-0 golden CRC tracing (see the Portable Roadmap).
 *
 * Every asset-preprocess entry point reports the bytes it transformed via
 * portCrcTrace()/portCrcTraceValue(). When PORT_CRC_TRACE is set, each
 * report prints one line:
 *
 *     CRCTRACE <tag>:<key>#<seq> len=<len> crc=<crc32>
 *
 * to stderr (PORT_CRC_TRACE=1) or an append-truncated file
 * (PORT_CRC_TRACE=<path>). <key> is a caller-chosen stable id (a VMA, a ROM
 * base, or 0); <seq> is a per-tag counter, so a deterministic run produces a
 * deterministic, diffable stream. port/tests/run_goldens.sh pins these
 * streams as goldens.
 *
 * When PORT_CRC_TRACE is unset the trace calls are no-ops (one getenv on
 * first use); the CRC math itself only runs for callers that pre-accumulate
 * with portCrc32Update (setup file sections), which is once-per-load noise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"

static u32 sCrcTable[256];
static s32 sCrcTableReady = 0;

static void crcInitTable(void)
{
    u32 c;
    s32 n, k;

    for (n = 0; n < 256; n++) {
        c = (u32)n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        sCrcTable[n] = c;
    }
    sCrcTableReady = 1;
}

u32 portCrc32Update(u32 crc, const void *data, u32 len)
{
    const u8 *p = data;
    u32 i;

    if (!sCrcTableReady) {
        crcInitTable();
    }
    for (i = 0; i < len; i++) {
        crc = sCrcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

u32 portCrc32(const void *data, u32 len)
{
    return portCrc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/* -1 unchecked, 0 off, 1 on */
static s32 sTraceState = -1;
static FILE *sTraceOut;

static s32 traceEnabled(void)
{
    if (sTraceState < 0) {
        const char *env = getenv("PORT_CRC_TRACE");

        if (env == NULL || env[0] == '\0') {
            sTraceState = 0;
        } else {
            if (strcmp(env, "1") == 0 || strcmp(env, "stderr") == 0) {
                sTraceOut = stderr;
            } else {
                sTraceOut = fopen(env, "w");
                if (sTraceOut == NULL) {
                    fprintf(stderr, "port: PORT_CRC_TRACE: cannot open %s\n", env);
                    sTraceOut = stderr;
                }
            }
            sTraceState = 1;
        }
    }
    return sTraceState;
}

/* Per-tag sequence numbers; tags are string literals, compare by content so
 * the same tag from two TUs shares a counter. */
static struct {
    const char *tag;
    u32 seq;
} sTags[32];

static u32 nextSeq(const char *tag)
{
    s32 i;

    for (i = 0; i < 32; i++) {
        if (sTags[i].tag == NULL) {
            sTags[i].tag = tag;
            sTags[i].seq = 0;
            return sTags[i].seq++;
        }
        if (strcmp(sTags[i].tag, tag) == 0) {
            return sTags[i].seq++;
        }
    }
    return 0xFFFFFFFFu; /* table full: seq is meaningless but lines still print */
}

void portCrcTraceValue(const char *tag, u32 key, u32 len, u32 crc)
{
    if (!traceEnabled()) {
        return;
    }
    fprintf(sTraceOut, "CRCTRACE %s:%08x#%u len=%u crc=%08x\n",
            tag, key, nextSeq(tag), len, crc);
    fflush(sTraceOut);
}

void portCrcTrace(const char *tag, u32 key, const void *data, u32 len)
{
    if (!traceEnabled()) {
        return;
    }
    portCrcTraceValue(tag, key, len, portCrc32(data, len));
}
