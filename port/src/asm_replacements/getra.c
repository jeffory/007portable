/**
 * @file getra.c
 * Replaces src/getra.s. The original walked MIPS stack frames to recover
 * the caller's return address for debug prints; meaningless on PC.
 */
#include <ultra64.h>

u32 getReturnAddress(void)
{
    return (u32)-1; /* same as the asm's "couldn't find it" result */
}
