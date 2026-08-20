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
 * (the -no-pie image itself sits at 0x400000), so u32 truncation
 * round-trips losslessly. On 32-bit this is plain malloc.
 *
 * The low-4GB reservation used to be mmap MAP_32BIT, which exists only on
 * x86-64 Linux. For aarch64/Android the same guarantee comes from scanning
 * ascending hint addresses below 4GB with MAP_FIXED_NOREPLACE (Linux 4.17+,
 * bionic ok); a bump cursor keeps successive allocations from rescanning.
 * The single code path runs on x86-64 too so it is always exercised.
 */
#if IS_64_BIT
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000 /* Linux UAPI value */
#endif
#endif

void *portLowAlloc(u32 size)
{
#if IS_64_BIT
    /* start clear of the -no-pie image (0x400000) and the brk heap */
    static uintptr_t sHint = 0x40000000;
    uintptr_t hint = sHint;
    u32 aligned = (size + 0xFFFFu) & ~0xFFFFu;

    while (hint + aligned <= 0xFFFFFFF0u) {
        void *p = mmap((void *)hint, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

        if (p != MAP_FAILED) {
            sHint = (uintptr_t)p + aligned;
            return p;
        }
        hint += aligned > 0x1000000u ? aligned : 0x1000000u; /* skip >=16MB */
    }
    fprintf(stderr, "port: no low-4GB address space for %u bytes\n", size);
    return NULL;
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
