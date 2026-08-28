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
#include <unistd.h>
#endif
#endif
#include "port.h"

/* Generous vs the N64's ~3.5MB so M2+ can raise asset budgets; still well
 * inside a 32-bit address space. */
#define PORT_ARENA_SIZE (32 * 1024 * 1024)

static u8 *sArena;

#define PORT_LOW_BASE 0x40000000u /* clear of the image (0x400000) and brk */
#define PORT_LOW_END  0x7FFFFFF0u /* keep bit 31 clear: pointers the game
                                   * parks in an s32 must not sign-extend */

#if IS_64_BIT
/* Bump cursor, shared by every 64-bit path. sLowStageBase records where the
 * permanent boot allocations end; each stage load frees everything past it
 * (portLowAllocStageScopeBegin) and rewinds the cursor there, so replaying
 * stages reuses the same address range instead of climbing out of the
 * window. The wrap in portLowAlloc is the backstop for anything the rewind
 * does not cover. */
static uintptr_t sLowHint = PORT_LOW_BASE;
static uintptr_t sLowStageBase;

/* Address space each request consumes. Windows is stuck with VirtualAlloc's
 * 64KB reservation granularity; on POSIX rounding to 64KB instead of the
 * page burned ~200x the address space per small allocation - the model64
 * node extensions alone (a few hundred bytes each, thousands of them)
 * chewed through the whole window. */
static u32 lowAlignUp(u32 size)
{
#if defined(_WIN32)
    return (size + 0xFFFFu) & ~0xFFFFu;
#else
    static u32 sPage;
    u32 mask;

    if (sPage == 0) {
        long p = sysconf(_SC_PAGESIZE);
        sPage = p > 0 ? (u32)p : 4096u;
    }
    mask = sPage - 1u;
    return (size + mask) & ~mask;
#endif
}
#endif

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
#if IS_64_BIT
    /* Everything past sLowStageBase was just unmapped, so hand the cursor
     * back to it. PORT_NO_REWIND=1 keeps the cursor climbing instead, so a
     * freed address is never handed out twice - a stale write then faults on
     * unmapped memory at the instruction that makes it, rather than quietly
     * shredding whatever moved in. Without this the cursor only ever climbs: the attract
     * sequence reloads stages forever and walked it past 2GB (where any
     * pointer the game parks in an s32 sign-extends into a segfault) and
     * then out of the window entirely, with only a few MB actually live. */
    if (sLowStageBase == 0) {
        sLowStageBase = sLowHint; /* boot allocations below here are permanent */
    } else if (getenv("PORT_NO_REWIND") == NULL) {
        sLowHint = sLowStageBase;
    }
#endif
    if (n != 0 && getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/mem: freed %d stage allocations (%u bytes)\n", n, freed);
    }
}

/**
 * Allocate memory the game may take pointers into.
 *
 * The game's data model is 32-bit: asset blobs store pointers in 4-byte
 * slots and shared code round-trips addresses through (u32) - and, in
 * places, through s32. On the 64-bit build every such allocation therefore
 * comes from the low 2GB, so both truncation AND sign extension are
 * lossless. Bit 31 must stay clear: a pointer parked in an s32 and read
 * back widens to 0xffffffff8xxxxxxx above 2GB, which is a segfault the
 * moment it is dereferenced. That is exactly the window x86-64's old
 * MAP_32BIT gave us (the first 2GB), now enforced on every 64-bit target.
 * On 32-bit this is plain malloc.
 *
 * MAP_32BIT exists only on x86-64 Linux, so the guarantee comes from
 * scanning ascending hint addresses with MAP_FIXED_NOREPLACE (Linux 4.17+,
 * bionic ok); a bump cursor keeps successive allocations from rescanning.
 * The cursor WRAPS at the ceiling: memory is freed (stage reloads munmap
 * the whole stage list) but the cursor cannot reclaim those holes on its
 * own, so a looping attract sequence used to march it up through 2GB -
 * and out of the window entirely - while barely any memory was live.
 * MAP_FIXED_NOREPLACE makes rescanning from the base safe: occupied
 * ranges simply fail and the scan steps over them.
 *
 * The single code path runs on x86-64 too so it is always exercised.
 */
#if IS_64_BIT
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000 /* Linux UAPI value */
#endif
#endif

static void *lowAlloc(u32 size, int stageScoped)
{
#if IS_64_BIT && defined(_WIN32)
    /* same low-2GB guarantee via VirtualAlloc explicit-base scan */
    uintptr_t hint = sLowHint;
    u32 aligned = lowAlignUp(size);
    int wrapped = 0;

    for (;;) {
        void *p;

        if (hint + aligned > PORT_LOW_END) {
            if (wrapped) {
                break;
            }
            hint = PORT_LOW_BASE;
            wrapped = 1;
        }
        p = VirtualAlloc((void *)hint, size, MEM_COMMIT | MEM_RESERVE,
                         PAGE_READWRITE);

        if (p != NULL) {
            sLowHint = (uintptr_t)p + aligned;
            if (stageScoped) { stageTrack(p, size); }
            return p;
        }
        /* a wrapped pass steps by the request so it can land in the
         * small holes a 16MB stride flies over */
        hint += wrapped ? aligned : (aligned > 0x1000000u ? aligned : 0x1000000u);
    }
    fprintf(stderr, "port: no low-2GB address space for %u bytes\n", size);
    return NULL;
#elif IS_64_BIT && defined(__APPLE__)
    /* Darwin has no MAP_FIXED_NOREPLACE; the kernel honors a plain hint
     * when the range is free, so scan hints and verify what came back is
     * below 4GB (unmap and advance otherwise). Requires the link to have
     * shrunk __PAGEZERO (-Wl,-pagezero_size,0x1000) — the macOS default
     * reserves the entire low 4GB. */
    uintptr_t hint = sLowHint;
    u32 aligned = lowAlignUp(size);
    int wrapped = 0;

    for (;;) {
        void *p;

        if (hint + aligned > PORT_LOW_END) {
            if (wrapped) {
                break;
            }
            hint = PORT_LOW_BASE;
            wrapped = 1;
        }
        p = mmap((void *)hint, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (p != MAP_FAILED && (uintptr_t)p + aligned <= PORT_LOW_END) {
            sLowHint = (uintptr_t)p + aligned;
            if (stageScoped) { stageTrack(p, size); }
            return p;
        }
        if (p != MAP_FAILED) {
            munmap(p, size); /* kernel ignored the hint: landed high */
        }
        /* a wrapped pass steps by the request so it can land in the
         * small holes a 16MB stride flies over */
        hint += wrapped ? aligned : (aligned > 0x1000000u ? aligned : 0x1000000u);
    }
    fprintf(stderr, "port: no low-2GB address space for %u bytes "
                    "(is -pagezero_size in the link flags?)\n", size);
    return NULL;
#elif IS_64_BIT
    uintptr_t hint = sLowHint;
    u32 aligned = lowAlignUp(size);
    int wrapped = 0;

    for (;;) {
        void *p;

        if (hint + aligned > PORT_LOW_END) {
            if (wrapped) {
                break; /* a full pass found no free range */
            }
            hint = PORT_LOW_BASE;
            wrapped = 1;
        }
        p = mmap((void *)hint, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

        if (p != MAP_FAILED) {
            sLowHint = (uintptr_t)p + aligned;
            if (stageScoped) { stageTrack(p, size); }
            return p;
        }
        /* a wrapped pass steps by the request so it can land in the
         * small holes a 16MB stride flies over */
        hint += wrapped ? aligned : (aligned > 0x1000000u ? aligned : 0x1000000u);
    }
    fprintf(stderr, "port: no low-2GB address space for %u bytes\n", size);
    return NULL;
#else
    {
        void *p = malloc(size);

        if (stageScoped) {
            stageTrack(p, size);
        }
        return p;
    }
#endif
}

/**
 * Stage-scoped: freed wholesale by the next stage load.
 */
void *portLowAlloc(u32 size)
{
    return lowAlloc(size, 1);
}

/**
 * Persistent: allocated once, cached in a global, alive for the process.
 *
 * The game's one-shot pools (props, projectiles, casings, the anim frame
 * buffer, light and matrix singletons) follow `if (p == NULL) p = alloc()`,
 * so a stage-scoped free leaves the global dangling and the next stage load
 * writes through it - `g_Props[i].prev = &g_Props[i + 1]` shredded whatever
 * had taken the pool's address, which after a menu round trip was the new
 * level's stan. These never come back through portLowFree, so they are kept
 * out of the stage list entirely; the low-window scan steps over them.
 */
void *portLowAllocPersistent(u32 size)
{
    return lowAlloc(size, 0);
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
