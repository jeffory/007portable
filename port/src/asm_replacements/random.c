/**
 * @file random.c
 * C reimplementation of src/random.s (64-bit xorshift-style PRNG).
 * Gameplay-visible: must match the assembly bit-for-bit.
 *
 * Decoded from the MIPS: with 64-bit seed s,
 *   t  = ((s << 63) >> 31) | ((s << 31) >> 32)
 *   t ^= (s << 44) >> 32
 *   s' = ((t >> 20) & 0xFFF) ^ t
 *   return low 32 bits of s'
 */
#include <ultra64.h>

u64 g_randomSeed = 0xAB8D9F7781280783ull;

static u64 randomStep(u64 s)
{
    u64 t;

    t = ((s << 63) >> 31) | ((s << 31) >> 32);
    t ^= (s << 44) >> 32;
    return ((t >> 20) & 0xFFF) ^ t;
}

u32 randomGetNext(void)
{
    g_randomSeed = randomStep(g_randomSeed);
    return (u32)g_randomSeed;
}

void randomSetSeed(u32 seed)
{
    /* daddiu on a sign-extended 32-bit arg */
    g_randomSeed = (u64)((s64)(s32)seed + 1);
}

u32 randomGetNextFrom(u64 *seed)
{
    *seed = randomStep(*seed);
    return (u32)*seed;
}
