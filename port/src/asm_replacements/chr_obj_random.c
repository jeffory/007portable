/**
 * @file chr_obj_random.c
 * C reimplementation of src/game/chrObjRandom.s — the identical xorshift
 * PRNG as random.s, with its own seed for NPC/object randomness.
 */
#include <ultra64.h>

u64 g_chrObjRandomSeed = 0xAB8D9F7781280783ull;

u32 chrObjRandomGetNext(void)
{
    u64 s = g_chrObjRandomSeed;
    u64 t;

    t = ((s << 63) >> 31) | ((s << 31) >> 32);
    t ^= (s << 44) >> 32;
    s = ((t >> 20) & 0xFFF) ^ t;
    g_chrObjRandomSeed = s;
    return (u32)s;
}

void chrObjRandomSetSeed(u32 seed)
{
    g_chrObjRandomSeed = (u64)((s64)(s32)seed + 1);
}
