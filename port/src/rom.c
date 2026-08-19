/**
 * @file rom.c
 * Loads the US ROM (data/ge007.u.z64) into memory and verifies its SHA-1
 * against the byte-matched hash. All PI "DMA" (port/src/ultra/pi.c) and
 * the ROM-offset symbols in rom_symbols.ld index into this buffer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "port.h"

#define GE_U_SHA1 "abe01e4aeb033b6c0836819f549c791b26cfde83"
#define GE_ROM_SIZE (12 * 1024 * 1024)

u8 *g_PortRomData;
u32 g_PortRomSize;

/* --- tiny SHA-1 (public-domain style implementation) ----------------------- */
struct sha1ctx {
    u32 h[5];
    u64 len;
    u8 buf[64];
    u32 buflen;
};

static u32 rol(u32 v, int s) { return (v << s) | (v >> (32 - s)); }

static void sha1Block(struct sha1ctx *c, const u8 *p)
{
    u32 w[80], a, b, d, e, f, k, t;
    u32 cc;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)p[i * 4] << 24) | ((u32)p[i * 4 + 1] << 16) |
               ((u32)p[i * 4 + 2] << 8) | (u32)p[i * 4 + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1Init(struct sha1ctx *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0;
    c->buflen = 0;
}

static void sha1Update(struct sha1ctx *c, const u8 *data, u32 len)
{
    c->len += len;
    while (len > 0) {
        u32 n = 64 - c->buflen;

        if (n > len) {
            n = len;
        }
        memcpy(c->buf + c->buflen, data, n);
        c->buflen += n;
        data += n;
        len -= n;
        if (c->buflen == 64) {
            sha1Block(c, c->buf);
            c->buflen = 0;
        }
    }
}

static void sha1Final(struct sha1ctx *c, char out[41])
{
    u64 bits = c->len * 8;
    u8 pad = 0x80;
    u8 lenbuf[8];
    static const char hex[] = "0123456789abcdef";
    int i;

    sha1Update(c, &pad, 1);
    pad = 0;
    while (c->buflen != 56) {
        sha1Update(c, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        lenbuf[i] = (u8)(bits >> (56 - i * 8));
    }
    sha1Update(c, lenbuf, 8);
    for (i = 0; i < 5; i++) {
        out[i * 8 + 0] = hex[(c->h[i] >> 28) & 0xF];
        out[i * 8 + 1] = hex[(c->h[i] >> 24) & 0xF];
        out[i * 8 + 2] = hex[(c->h[i] >> 20) & 0xF];
        out[i * 8 + 3] = hex[(c->h[i] >> 16) & 0xF];
        out[i * 8 + 4] = hex[(c->h[i] >> 12) & 0xF];
        out[i * 8 + 5] = hex[(c->h[i] >> 8) & 0xF];
        out[i * 8 + 6] = hex[(c->h[i] >> 4) & 0xF];
        out[i * 8 + 7] = hex[c->h[i] & 0xF];
    }
    out[40] = '\0';
}

/* ---------------------------------------------------------------------------- */

int portRomLoad(const char *path)
{
    FILE *f;
    long size;
    struct sha1ctx ctx;
    char digest[41];

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr,
                "port: cannot open ROM '%s'.\n"
                "      Place the US GoldenEye 007 ROM there (z64/big-endian,\n"
                "      sha1 %s).\n", path, GE_U_SHA1);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size != GE_ROM_SIZE) {
        fprintf(stderr, "port: ROM '%s' has size %ld, expected %d (is it .z64 big-endian?)\n",
                path, size, GE_ROM_SIZE);
        fclose(f);
        return -1;
    }

    /* low 4GB: wavetable/seq preprocess and audio DMA store pointers into
     * this buffer in 4-byte slots on the 64-bit build */
    g_PortRomData = portLowAlloc((u32)size);
    if (g_PortRomData == NULL || fread(g_PortRomData, 1, size, f) != (size_t)size) {
        fprintf(stderr, "port: failed to read ROM\n");
        fclose(f);
        return -1;
    }
    fclose(f);
    g_PortRomSize = (u32)size;

    /* byte order check: z64 starts 0x80 0x37 0x12 0x40 */
    if (g_PortRomData[0] != 0x80 || g_PortRomData[1] != 0x37) {
        fprintf(stderr, "port: ROM is not in z64 (big-endian) byte order\n");
        return -1;
    }

    sha1Init(&ctx);
    sha1Update(&ctx, g_PortRomData, g_PortRomSize);
    sha1Final(&ctx, digest);
    if (strcmp(digest, GE_U_SHA1) != 0) {
        fprintf(stderr, "port: ROM sha1 mismatch:\n  got      %s\n  expected %s\n",
                digest, GE_U_SHA1);
        return -1;
    }

    fprintf(stderr, "port: ROM ok (%u bytes, sha1 verified)\n", g_PortRomSize);
    return 0;
}
