/**
 * @file memory.c
 * The game arena. On N64 this is [_bssSegmentEnd, TLB page pool) inside
 * 4/8MB RDRAM; on PC it's one fixed allocation that memp/mema carve up.
 * The TLB pager itself (tlb_manage.c) doesn't exist on PC — code is
 * linked normally — so its two entry points called from boss.c become
 * the arena accessors.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <platform_info.h>
#if IS_64_BIT
#include <sys/mman.h>
#endif
#include "port.h"

/* Generous vs the N64's ~3.5MB so M2+ can raise asset budgets; still well
 * inside a 32-bit address space. */
#define PORT_ARENA_SIZE (32 * 1024 * 1024)

static u8 *sArena;

/**
 * Allocate memory the game may take pointers into.
 *
 * The game's data model is 32-bit: asset blobs store pointers in 4-byte
 * slots and shared code truncates addresses through (u32) casts. On the
 * 64-bit build every such allocation therefore comes from the low 4GB
 * (mmap MAP_32BIT; the -no-pie image itself sits at 0x400000), so u32
 * truncation round-trips losslessly. On 32-bit this is plain malloc.
 */
void *portLowAlloc(u32 size)
{
#if IS_64_BIT
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }
    if ((uintptr_t)p + size > 0xFFFFFFFFu) {
        fprintf(stderr, "port: MAP_32BIT gave %p (+%u), above 4GB?!\n", p, size);
        munmap(p, size);
        return NULL;
    }
    return p;
#else
    return malloc(size);
#endif
}

void portLowFree(void *ptr, u32 size)
{
    if (ptr == NULL) {
        return;
    }
#if IS_64_BIT
    munmap(ptr, size);
#else
    (void)size;
    free(ptr);
#endif
}

void portMemInit(void)
{
    sArena = portLowAlloc(PORT_ARENA_SIZE);
    if (sArena == NULL) {
        fprintf(stderr, "port: failed to allocate %d MB arena\n",
                PORT_ARENA_SIZE >> 20);
        exit(1);
    }
    memset(sArena, 0, PORT_ARENA_SIZE);
}

void *portMemArenaStart(void)
{
    return sArena;
}

u32 portMemArenaSize(void)
{
    return PORT_ARENA_SIZE;
}

/* --- tlb_manage.c entry points still referenced by kept code --------------- */

void tlbmanageEstablishManagementTable(void)
{
    /* no TLB pager on PC; the arena is set up in portMemInit() */
}

void *tlbmanageGetTlbAllocatedBlock(void)
{
    return sArena + PORT_ARENA_SIZE;
}

void tlbmanageResetCurrentEntriesCount(void)
{
}
