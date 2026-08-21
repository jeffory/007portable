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
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#endif
#include "port.h"

/* Generous vs the N64's ~3.5MB so M2+ can raise asset budgets; still well
 * inside a 32-bit address space. */
#define PORT_ARENA_SIZE (32 * 1024 * 1024)

static u8 *sArena;

/* --- stage-scoped allocation tracking (the stage-reload leak fix) ----------
 * Everything portLowAlloc'd after a stage load begins (setup/stan/model
 * rebuilds, texture scratch, lazily loaded weapon models) lives exactly as
 * long as that stage: the next lvlStageLoad frees the whole list before
 * loading. Boot-time allocations (ROM, arena, game stack, menu/logo
 * models, audio banks) happen before the first stage load and stay
 * permanent. Works for both the mmap (64-bit) and malloc (32-bit) paths. */
struct lowAllocRec {
    void *ptr;
    u32 size;
    struct lowAllocRec *next;
};
static struct lowAllocRec *sStageAllocs;
static s32 sStageScope;
static u32 sStageBytes;

static void stageTrack(void *ptr, u32 size)
{
    struct lowAllocRec *r;

    if (!sStageScope || ptr == NULL) {
        return;
    }
    r = malloc(sizeof(*r));
    if (r == NULL) {
        return; /* untracked: leaks like before, never breaks */
    }
    r->ptr = ptr;
    r->size = size;
    r->next = sStageAllocs;
    sStageAllocs = r;
    sStageBytes += size;
}

static void stageUntrack(void *ptr)
{
    struct lowAllocRec **pp;

    for (pp = &sStageAllocs; *pp != NULL; pp = &(*pp)->next) {
        if ((*pp)->ptr == ptr) {
            struct lowAllocRec *r = *pp;
            *pp = r->next;
            sStageBytes -= r->size;
            free(r);
            return;
        }
    }
}

void portLowAllocStageScopeBegin(void)
{
    u32 freed = sStageBytes;
    s32 n = 0;

    while (sStageAllocs != NULL) {
        struct lowAllocRec *r = sStageAllocs;
        sStageAllocs = r->next;
#if IS_64_BIT && defined(_WIN32)
        VirtualFree(r->ptr, 0, MEM_RELEASE);
#elif IS_64_BIT
        munmap(r->ptr, r->size);
#else
        free(r->ptr);
#endif
        free(r);
        n++;
    }
    sStageBytes = 0;
    sStageScope = 1;
    if (n != 0 && getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/mem: freed %d stage allocations (%u bytes)\n", n, freed);
    }
}

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
#if IS_64_BIT && defined(_WIN32)
    /* same low-4GB guarantee via VirtualAlloc explicit-base scan */
    static uintptr_t sHint = 0x40000000;
    uintptr_t hint = sHint;
    u32 aligned = (size + 0xFFFFu) & ~0xFFFFu;

    while (hint + aligned <= 0xFFFFFFF0u) {
        void *p = VirtualAlloc((void *)hint, size, MEM_COMMIT | MEM_RESERVE,
                               PAGE_READWRITE);

        if (p != NULL) {
            sHint = (uintptr_t)p + aligned;
            stageTrack(p, size);
            return p;
        }
        hint += aligned > 0x1000000u ? aligned : 0x1000000u;
    }
    fprintf(stderr, "port: no low-4GB address space for %u bytes\n", size);
    return NULL;
#elif IS_64_BIT && defined(__APPLE__)
    /* Darwin has no MAP_FIXED_NOREPLACE; the kernel honors a plain hint
     * when the range is free, so scan hints and verify what came back is
     * below 4GB (unmap and advance otherwise). Requires the link to have
     * shrunk __PAGEZERO (-Wl,-pagezero_size,0x1000) — the macOS default
     * reserves the entire low 4GB. */
    static uintptr_t sHint = 0x40000000;
    uintptr_t hint = sHint;
    u32 aligned = (size + 0xFFFFu) & ~0xFFFFu;

    while (hint + aligned <= 0xFFFFFFF0u) {
        void *p = mmap((void *)hint, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (p != MAP_FAILED && (uintptr_t)p + aligned <= 0xFFFFFFF0u) {
            sHint = (uintptr_t)p + aligned;
            stageTrack(p, size);
            return p;
        }
        if (p != MAP_FAILED) {
            munmap(p, size); /* kernel ignored the hint: landed high */
        }
        hint += aligned > 0x1000000u ? aligned : 0x1000000u;
    }
    fprintf(stderr, "port: no low-4GB address space for %u bytes "
                    "(is -pagezero_size in the link flags?)\n", size);
    return NULL;
#elif IS_64_BIT
    /* start clear of the -no-pie image (0x400000) and the brk heap */
    static uintptr_t sHint = 0x40000000;
    uintptr_t hint = sHint;
    u32 aligned = (size + 0xFFFFu) & ~0xFFFFu;

    while (hint + aligned <= 0xFFFFFFF0u) {
        void *p = mmap((void *)hint, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

        if (p != MAP_FAILED) {
            sHint = (uintptr_t)p + aligned;
            stageTrack(p, size);
            return p;
        }
        hint += aligned > 0x1000000u ? aligned : 0x1000000u; /* skip >=16MB */
    }
    fprintf(stderr, "port: no low-4GB address space for %u bytes\n", size);
    return NULL;
#else
    {
        void *p = malloc(size);
        stageTrack(p, size);
        return p;
    }
#endif
}

void portLowFree(void *ptr, u32 size)
{
    if (ptr == NULL) {
        return;
    }
    stageUntrack(ptr);
#if IS_64_BIT && defined(_WIN32)
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#elif IS_64_BIT
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
