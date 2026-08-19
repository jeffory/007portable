/**
 * @file preprocess_model.c
 * Byteswaps a loaded GE model file (big-endian on ROM) in place, called
 * from load_object_fill_header() before the game's offset->pointer
 * promotion pass (sub_GAME_7F075A90 / modelPromoteNodeOffsetsToPointers).
 *
 * File layout (see load_object_fill_header):
 *   [ s32 switches[numSwitches] ][ ModelFileTextures[numtextures] ][ nodes... ]
 * The compiled-in ModelFileHeader is already host-endian; only the blob
 * needs swapping. Node links and data pointers are vma-based offsets
 * (vma = 0x05000000) until promoted.
 *
 * The traversal mirrors modelPromoteNodeOffsetsToPointers exactly,
 * including the LOD child override.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <platform_info.h>
#include "port.h"

#define SWAP32(p) (*(u32 *)(p) = swap32(*(u32 *)(p)))
#define SWAP16(p) (*(u16 *)(p) = swap16(*(u16 *)(p)))

static u32 swap32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static u16 swap16(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

static int sTrace = -1;

struct mdlctx {
    u8 *base;      /* filedata */
    u32 vma;       /* 0x05000000 */
    u32 size;      /* file size in bytes */
    /* one bit per Gfx (8 bytes) already swapped: DL streams share tails
     * between nodes, and re-swapping would flip them back to BE */
    u8 *dlBitmap;
};

static void *off2ptr(struct mdlctx *ctx, u32 off)
{
    if (off == 0) {
        return NULL;
    }
    return ctx->base + (off - ctx->vma);
}

/* ---- leaf data ------------------------------------------------------------ */

/* render vertex: 16-byte gbi-compatible Vtx; halfwords at 0x0..0xB,
 * color/normal bytes at 0xC..0xF stay */
static void swapRenderVertices(void *ptr, s32 count)
{
    u8 *v = ptr;
    s32 i, j;

    for (i = 0; i < count; i++, v += 16) {
        for (j = 0; j < 12; j += 2) {
            SWAP16(v + j);
        }
    }
}

/* collision vertex: coords/index halfwords, a node OFFSET at 0x8
 * (CollisionRelatedNode/LinkedTo, promoted later), halfwords at 0xC/0xE */
static void swapCollisionVertices(void *ptr, s32 count)
{
    u8 *v = ptr;
    s32 i, j;

    for (i = 0; i < count; i++, v += 16) {
        for (j = 0; j < 8; j += 2) {
            SWAP16(v + j);
        }
        SWAP32(v + 8);
        SWAP16(v + 12);
        SWAP16(v + 14);
    }
}

/* Display list: swap Gfx words until G_ENDDL (or a branching G_DL).
 * Streams share tails between nodes, so a per-Gfx bitmap prevents
 * re-swapping; hitting an already-swapped command means the rest of the
 * stream was processed already. */
static void swapDisplayList(struct mdlctx *ctx, u32 dlOff)
{
    Gfx *g = off2ptr(ctx, dlOff);
    u32 idx;

    if (g == NULL) {
        return;
    }

    for (;;) {
        u8 opcode;

        idx = (u32)((u8 *)g - ctx->base) / 8;
        if ((u8 *)g < ctx->base || idx * 8 >= ctx->size) {
            fprintf(stderr, "port/model: DL 0x%x ran outside the file\n", dlOff);
            break;
        }
        if (ctx->dlBitmap[idx >> 3] & (1 << (idx & 7))) {
            break; /* rest of stream already swapped */
        }
        ctx->dlBitmap[idx >> 3] |= (u8)(1 << (idx & 7));

        SWAP32(&g->words.w0);
        SWAP32(&g->words.w1);
        opcode = (u8)(g->words.w0 >> 24);

        if (sTrace) {
            fprintf(stderr, "  [0x%x] %02x %08x %08x\n",
                    (u32)((u8 *)g - ctx->base), opcode, g->words.w0, g->words.w1);
        }

        /* the G_* immediates are NEGATIVE macro values (G_IMMFIRST-n) */
        if (opcode == (u8)G_ENDDL) {
            break;
        }
        if (opcode == (u8)G_DL) {
            /* sublists may be reachable only from here; recurse (the
             * bitmap breaks cycles) */
            if ((g->words.w1 >> 24) == (ctx->vma >> 24)) {
                swapDisplayList(ctx, g->words.w1);
            }
            if (((g->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH) {
                break; /* branch = end of this stream */
            }
        }
        g++;
    }
}

/* Op05/Op07 child records: {u8 NumEntries, u8, u16, u8 *entries} */
static void swapChildRecords(struct mdlctx *ctx, u32 childrenOff, s32 numChildren)
{
    u8 *c = off2ptr(ctx, childrenOff);
    s32 i;

    if (c == NULL) {
        return;
    }
    for (i = 0; i < numChildren; i++, c += 8) {
        u8 numEntries = c[0];
        u32 entOff;
        u8 *e;

        SWAP16(c + 2);
        SWAP32(c + 4);
        entOff = *(u32 *)(c + 4);
        e = off2ptr(ctx, entOff);
        if (e != NULL) {
            s32 n;

            for (n = 0; n < numEntries; ) {
                switch (e[0]) { /* MODELNODE_CHILD_* */
                case 1: /* VTX: u8,u8,u16 index */
                    SWAP16(e + 2);
                    e += 4;
                    n++;
                    break;
                case 3: /* TRI: 4 x u8 */
                    e += 4;
                    n++;
                    break;
                case 2: /* IMAGE: 2 x u8 */
                    e += 2;
                    n++;
                    break;
                default:
                    n = numEntries; /* unknown entry type; stop */
                    break;
                }
            }
        }
    }
}

static void swap32Run(void *ptr, s32 count)
{
    u32 *p = ptr;
    s32 i;

    for (i = 0; i < count; i++) {
        SWAP32(&p[i]);
    }
}

/* ---- node walk ------------------------------------------------------------ */

static void swapNodeData(struct mdlctx *ctx, u8 *node)
{
    u16 opcode;
    u8 *ro;

    if (sTrace < 0) {
        sTrace = getenv("PORT_MDL_TRACE") != NULL;
    }

    /* ModelNode: u16 Opcode; (pad) u32 Data,Parent,Next,Prev,Child */
    SWAP16(node + 0x00);
    SWAP32(node + 0x04);
    SWAP32(node + 0x08);
    SWAP32(node + 0x0C);
    SWAP32(node + 0x10);
    SWAP32(node + 0x14);

    opcode = (*(u16 *)node) & 0xFF;
    ro = off2ptr(ctx, *(u32 *)(node + 0x04));
    if (sTrace) {
        fprintf(stderr, "port/model: node@0x%x op=%u data=0x%x\n",
                (u32)(node - ctx->base), opcode, *(u32 *)(node + 0x04));
    }
    if (ro == NULL) {
        return;
    }

    switch (opcode) {
    case MODELNODE_OPCODE_HEADER:   /* 1 */
    case MODELNODE_OPCODE_OP20:     /* 19? kept same layout as header */
        SWAP16(ro + 0x0);  /* AnimPart */
        SWAP16(ro + 0x2);  /* MatrixIndex */
        SWAP32(ro + 0x4);  /* FirstGroup */
        SWAP16(ro + 0x8);  /* Group1 */
        SWAP16(ro + 0xA);  /* Group2 */
        SWAP16(ro + 0xC);  /* RwDataIndex */
        break;

    case MODELNODE_OPCODE_GROUP:    /* 2 */
    case MODELNODE_OPCODE_OP03:     /* 3 */
        swap32Run(ro + 0x0, 3);     /* Origin */
        SWAP16(ro + 0xC);           /* JointID */
        SWAP16(ro + 0xE);           /* MatrixID0 */
        SWAP16(ro + 0x10);          /* MatrixID1 */
        SWAP16(ro + 0x12);          /* MatrixID2 */
        SWAP32(ro + 0x14);          /* ChildGroup */
        SWAP32(ro + 0x18);          /* BoundingVolumeRadius */
        break;

    case MODELNODE_OPCODE_DL: {     /* 4 */
        u32 primary, secondary, vertices;
        u16 nverts;

        swap32Run(ro + 0x0, 4);     /* Primary, Secondary, BaseAddr, Vertices */
        SWAP16(ro + 0x10);          /* numVertices */
        primary = *(u32 *)(ro + 0x0);
        secondary = *(u32 *)(ro + 0x4);
        vertices = *(u32 *)(ro + 0xC);
        nverts = *(u16 *)(ro + 0x10);
        if (sTrace) {
            fprintf(stderr, "port/model:   DL prim=0x%x sec=0x%x vtx=0x%x n=%u\n",
                    primary, secondary, vertices, nverts);
        }
        if (primary) swapDisplayList(ctx, primary);
        if (secondary) swapDisplayList(ctx, secondary);
        if (vertices) swapRenderVertices(off2ptr(ctx, vertices), nverts);
        break;
    }

    case MODELNODE_OPCODE_OP05: {   /* 5 */
        s32 nchildren;
        u32 children, vertices;

        SWAP32(ro + 0x00);          /* NumChildren */
        swap32Run(ro + 0x04, 3);    /* Children, Vertices, Images */
        SWAP32(ro + 0x1A0);
        nchildren = *(s32 *)(ro + 0x00);
        children = *(u32 *)(ro + 0x04);
        vertices = *(u32 *)(ro + 0x08);
        if (children) swapChildRecords(ctx, children, nchildren);
        /* vertex count not stored here; vertices are indexed by children.
         * PORT_TODO(M3): count from child VtxIndex maxima if needed */
        (void)vertices;
        break;
    }

    case MODELNODE_OPCODE_OP07: {   /* 7 */
        s32 nchildren;
        u32 children;

        swap32Run(ro + 0x00, 2);    /* unk00, unk04 */
        SWAP32(ro + 0x08);          /* NumChildren */
        swap32Run(ro + 0x0C, 3);    /* Children, Vertices, Images */
        SWAP16(ro + 0x1A8);
        SWAP16(ro + 0x1AA);
        nchildren = *(s32 *)(ro + 0x08);
        children = *(u32 *)(ro + 0x0C);
        if (children) swapChildRecords(ctx, children, nchildren);
        break;
    }

    case MODELNODE_OPCODE_OP06:     /* 6 */
        swap32Run(ro + 0x0, 6);
        break;

    case MODELNODE_OPCODE_LOD:      /* 8 */
        swap32Run(ro + 0x0, 3);     /* MinDistance, MaxDistance, Affects */
        SWAP16(ro + 0xC);           /* RwDataIndex */
        break;

    case MODELNODE_OPCODE_BSP:      /* 9 */
        swap32Run(ro + 0x0, 8);     /* Point, Vector, leftChild, rightChild */
        SWAP16(ro + 0x20);
        SWAP16(ro + 0x22);
        break;

    case MODELNODE_OPCODE_BBOX:     /* 10 */
        swap32Run(ro + 0x0, 7);     /* ModelNumber + bbox(6 f32) */
        break;

    case MODELNODE_OPCODE_OP11:     /* 11 */
        swap32Run(ro + 0x0, 17);    /* unk0c[16] + radius */
        SWAP16(ro + 0x44);
        SWAP16(ro + 0x46);
        SWAP32(ro + 0x48);
        break;

    case MODELNODE_OPCODE_GUNFIRE:  /* 12 */
        swap32Run(ro + 0x0, 8);     /* Offset, Size, Image, Scale */
        SWAP16(ro + 0x20);
        SWAP16(ro + 0x22);
        SWAP32(ro + 0x24);
        break;

    case MODELNODE_OPCODE_SHADOW:   /* 13 */
        swap32Run(ro + 0x0, 8);     /* pos, size, image, Header, Scale, Base */
        break;

    case MODELNODE_OPCODE_OP14:     /* 14 */
        swap32Run(ro + 0x0, 4);
        break;

    case MODELNODE_OPCODE_INTERLINK: /* 15 */
        swap32Run(ro + 0x0, 7);
        break;

    case MODELNODE_OPCODE_OP16:     /* 16 */
        swap32Run(ro + 0x0, 3);     /* pos */
        SWAP16(ro + 0xC);
        SWAP16(ro + 0xE);
        SWAP16(ro + 0x10);
        SWAP16(ro + 0x12);
        SWAP32(ro + 0x14);          /* Scale */
        break;

    case MODELNODE_OPCODE_OP17:     /* 17 */
        swap32Run(ro + 0x0, 9);     /* hitpart, radiusSq, pos, othernode, scales */
        break;

    case MODELNODE_OPCODE_SWITCH:   /* 18 */
        SWAP32(ro + 0x0);           /* Controls */
        SWAP16(ro + 0x4);           /* RwDataIndex */
        break;

    case MODELNODE_OPCODE_GROUPSIMPLE: /* 21 */
        swap32Run(ro + 0x0, 3);     /* Origin */
        SWAP16(ro + 0xC);
        SWAP16(ro + 0xE);
        SWAP32(ro + 0x10);          /* BoundingVolumeRadius */
        break;

    case MODELNODE_OPCODE_DLPRIMARY: { /* 22 */
        s32 nverts;
        u32 vertices, primary;

        swap32Run(ro + 0x0, 4);     /* numVertices, Vertices, Primary, Base */
        nverts = *(s32 *)(ro + 0x0);
        vertices = *(u32 *)(ro + 0x4);
        primary = *(u32 *)(ro + 0x8);
        if (primary) swapDisplayList(ctx, primary);
        if (vertices) swapRenderVertices(off2ptr(ctx, vertices), nverts);
        break;
    }

    case MODELNODE_OPCODE_HEAD:     /* 23 */
        SWAP16(ro + 0x0);           /* RwDataIndex */
        break;

    case MODELNODE_OPCODE_DLCOLLISION: { /* 24 */
        u32 primary, secondary, vertices, collVerts, pointUsage;
        s16 nverts, ncoll;

        swap32Run(ro + 0x0, 3);     /* Primary, Secondary, Vertices */
        SWAP16(ro + 0xC);           /* numVertices */
        SWAP16(ro + 0xE);           /* numCollisionVertices */
        SWAP32(ro + 0x10);          /* CollisionVertices */
        SWAP32(ro + 0x14);          /* PointUsage */
        SWAP16(ro + 0x18);          /* ModelType */
        SWAP16(ro + 0x1A);          /* RwDataIndex */
        primary = *(u32 *)(ro + 0x0);
        secondary = *(u32 *)(ro + 0x4);
        vertices = *(u32 *)(ro + 0x8);
        nverts = *(s16 *)(ro + 0xC);
        ncoll = *(s16 *)(ro + 0xE);
        collVerts = *(u32 *)(ro + 0x10);
        pointUsage = *(u32 *)(ro + 0x14);
        if (primary) swapDisplayList(ctx, primary);
        if (secondary) swapDisplayList(ctx, secondary);
        if (vertices) swapRenderVertices(off2ptr(ctx, vertices), nverts);
        if (collVerts) swapCollisionVertices(off2ptr(ctx, collVerts), ncoll);
        if (pointUsage) {
            u16 *pu = off2ptr(ctx, pointUsage);
            s32 i;

            for (i = 0; i < ncoll; i++) {
                SWAP16(&pu[i]);
            }
        }
        break;
    }

    case MODELNODE_OPCODE_NULL:
    case MODELNODE_OPCODE_OP19: /* unused in retail files */
        break;

    default:
        fprintf(stderr, "port/model: unhandled node opcode %u\n", opcode);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * File-base registry.
 *
 * On N64/PC32 header->Switches aliases the start of the loaded blob and
 * sub_GAME_7F0762E0 uses it as the file base for its GDL rewrite. The
 * 64-bit build relocates the switch table into native-width memory, so
 * the blob base is recorded here instead (used on PC32 too, where it is
 * simply the same value ->Switches would have given).
 * ------------------------------------------------------------------------- */

#define MDLREG_MAX 256

static struct {
    struct ModelFileHeader *hdr;
    void *base;
} sMdlReg[MDLREG_MAX];
static s32 sMdlRegCount;

static void mdlRegisterFile(struct ModelFileHeader *hdr, void *base)
{
    s32 i;

    for (i = 0; i < sMdlRegCount; i++) {
        if (sMdlReg[i].hdr == hdr) {
            sMdlReg[i].base = base; /* header reused for a reloaded file */
            return;
        }
    }
    if (sMdlRegCount >= MDLREG_MAX) {
        fprintf(stderr, "port/model: file-base registry full\n");
        exit(1);
    }
    sMdlReg[sMdlRegCount].hdr = hdr;
    sMdlReg[sMdlRegCount].base = base;
    sMdlRegCount++;
}

void *portModelFileBase(struct ModelFileHeader *hdr)
{
    s32 i;

    for (i = 0; i < sMdlRegCount; i++) {
        if (sMdlReg[i].hdr == hdr) {
            return sMdlReg[i].base;
        }
    }
    /* fall back to the historical alias */
    return hdr->Switches;
}

#if IS_64_BIT
/* ---------------------------------------------------------------------------
 * 64-bit rebuild: the blob's nodes/rodata are packed 32-bit records, but
 * the game reads them through native structs whose pointer members are 8
 * bytes. Rebuild the node tree (nodes + pointer-bearing rodata + Op05/07
 * child record arrays + the switch table) into a fresh low-4GB extension
 * with fully promoted pointers, leaving all pointer-free data (display
 * lists, vertices, textures, collision vertex arrays, point-usage,
 * child entry streams) in the original blob at unchanged offsets. This
 * REPLACES the game's sub_GAME_7F075A90 promote pass (skipped on 64-bit).
 * ------------------------------------------------------------------------- */

struct mdlobj {
    u32 nodeOff;      /* blob-relative offset of the 0x18-byte disk node */
    void *node;       /* native ModelNode in the extension */
    void *rodata;     /* native rodata in the extension (NULL: unmoved) */
    void *children;   /* native ModelRoData_Child array (op5/7) */
};

struct mdlbuild {
    struct mdlctx *ctx;
    struct mdlobj *objs;
    s32 count;
    s32 cap;
    u8 *ext;
    u32 extSize;
    u32 cursor;
};

/* native (expanded) rodata size per opcode; 0 = layout unchanged, leave in
 * blob and point Data at it directly */
static u32 roNativeSize(u32 op)
{
    switch (op) {
    case MODELNODE_OPCODE_HEADER:
    case MODELNODE_OPCODE_OP20:      return sizeof(ModelRoData_HeaderRecord);
    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03:      return sizeof(ModelRoData_GroupRecord);
    case MODELNODE_OPCODE_DL:        return sizeof(ModelRoData_DisplayListRecord);
    case MODELNODE_OPCODE_OP05:      return sizeof(ModelRoData_Op05Record);
    case MODELNODE_OPCODE_OP06:      return sizeof(ModelRoData_Op06Record);
    case MODELNODE_OPCODE_OP07:      return sizeof(ModelRoData_Op07Record);
    case MODELNODE_OPCODE_LOD:       return sizeof(ModelRoData_LODRecord);
    case MODELNODE_OPCODE_BSP:       return sizeof(ModelRoData_BSPRecord);
    case MODELNODE_OPCODE_OP11:      return sizeof(ModelRoData_Op11Record);
    case MODELNODE_OPCODE_GUNFIRE:   return sizeof(ModelRoData_GunfireRecord);
    case MODELNODE_OPCODE_SHADOW:    return sizeof(ModelRoData_ShadowRecord);
    case MODELNODE_OPCODE_OP17:      return sizeof(ModelRoData_Op17Record);
    case MODELNODE_OPCODE_SWITCH:    return sizeof(ModelRoData_SwitchRecord);
    case MODELNODE_OPCODE_DLPRIMARY: return sizeof(ModelRoData_DisplayListPrimaryRecord);
    case MODELNODE_OPCODE_DLCOLLISION:
        return sizeof(ModelRoData_DisplayList_CollisionRecord);
    default:
        /* BBOX, OP14, INTERLINK, OP16, GROUPSIMPLE, HEAD, NULL, OP19:
         * no pointer members, disk layout == native layout */
        return 0;
    }
}

static s32 roChildCount(struct mdlctx *ctx, u32 op, u8 *ro)
{
    if (op == MODELNODE_OPCODE_OP05) {
        return *(s32 *)(ro + 0x00);
    }
    if (op == MODELNODE_OPCODE_OP07) {
        return *(s32 *)(ro + 0x08);
    }
    (void)ctx;
    return 0;
}

static struct mdlobj *buildFind(struct mdlbuild *b, u32 nodeOff)
{
    s32 i;

    for (i = 0; i < b->count; i++) {
        if (b->objs[i].nodeOff == nodeOff) {
            return &b->objs[i];
        }
    }
    return NULL;
}

/* resolve a vma-based disk reference to a host pointer */
static void *buildResolve(struct mdlbuild *b, u32 v)
{
    u32 off;
    s32 i;

    if (v == 0) {
        return NULL;
    }
    off = v - b->ctx->vma;
    if (off >= b->ctx->size) {
        fprintf(stderr, "port/model64: ref 0x%x outside file (size 0x%x)\n", v, b->ctx->size);
        return NULL;
    }
    for (i = 0; i < b->count; i++) {
        struct mdlobj *o = &b->objs[i];

        if (o->nodeOff == off) {
            return o->node;
        }
        if (o->rodata != NULL && *(u32 *)(b->ctx->base + o->nodeOff + 0x04) - b->ctx->vma == off) {
            return o->rodata;
        }
    }
    /* unmoved blob data (DLs, vertices, textures, entry streams, ...) */
    return b->ctx->base + off;
}

static void *extAlloc(struct mdlbuild *b, u32 size)
{
    void *p = b->ext + b->cursor;

    b->cursor += (size + 7) & ~7u;
    if (b->cursor > b->extSize) {
        fprintf(stderr, "port/model64: extension overflow (%u > %u)\n", b->cursor, b->extSize);
        exit(1);
    }
    return p;
}

/* pass 1: enumerate nodes (same traversal as the swap pass / the game's
 * promote pass, including the LOD child override) */
static void buildCollect(struct mdlbuild *b, u8 *rootnode)
{
    struct mdlctx *ctx = b->ctx;
    u8 *node = rootnode;

    while (node != NULL) {
        u32 nodeOff = (u32)(node - ctx->base);
        u16 opcode = (*(u16 *)node) & 0xFF;
        u32 childOff;

        if (buildFind(b, nodeOff) != NULL) {
            break; /* cycle guard; the tree should not revisit */
        }
        if (b->count == b->cap) {
            b->cap = b->cap ? b->cap * 2 : 64;
            b->objs = realloc(b->objs, b->cap * sizeof(*b->objs));
        }
        b->objs[b->count].nodeOff = nodeOff;
        b->objs[b->count].node = NULL;
        b->objs[b->count].rodata = NULL;
        b->objs[b->count].children = NULL;
        b->count++;

        childOff = *(u32 *)(node + 0x14);
        if (opcode == MODELNODE_OPCODE_LOD) {
            u8 *ro = off2ptr(ctx, *(u32 *)(node + 0x04));

            if (ro != NULL) {
                childOff = *(u32 *)(ro + 0x8); /* Affects */
            }
        }

        if (childOff != 0) {
            node = off2ptr(ctx, childOff);
        } else {
            while (node != NULL) {
                u32 nextOff = *(u32 *)(node + 0x0C);
                u32 parentOff = *(u32 *)(node + 0x08);

                if (nextOff != 0) {
                    node = off2ptr(ctx, nextOff);
                    break;
                }
                node = off2ptr(ctx, parentOff);
            }
        }
    }
}

/* fill one native rodata record from its packed disk image */
static void buildFillRodata(struct mdlbuild *b, u32 op, u8 *ro, void *nat)
{
    u8 *base = b->ctx->base;

    switch (op) {
    case MODELNODE_OPCODE_HEADER:
    case MODELNODE_OPCODE_OP20: {
        ModelRoData_HeaderRecord *d = nat;

        d->AnimPart = *(u16 *)(ro + 0x0);
        d->MatrixIndex = *(s16 *)(ro + 0x2);
        d->FirstGroup = buildResolve(b, *(u32 *)(ro + 0x4));
        d->Group1 = *(u16 *)(ro + 0x8);
        d->Group2 = *(u16 *)(ro + 0xA);
        d->RwDataIndex = *(u16 *)(ro + 0xC);
        d->reserved = *(u16 *)(ro + 0xE);
        break;
    }

    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03: {
        ModelRoData_GroupRecord *d = nat;

        memcpy(&d->Origin, ro + 0x0, 12);
        d->JointID = *(u16 *)(ro + 0xC);
        d->MatrixID0 = *(s16 *)(ro + 0xE);
        d->MatrixID1 = *(s16 *)(ro + 0x10);
        d->MatrixID2 = *(s16 *)(ro + 0x12);
        d->ChildGroup = buildResolve(b, *(u32 *)(ro + 0x14));
        memcpy(&d->BoundingVolumeRadius, ro + 0x18, 4);
        break;
    }

    case MODELNODE_OPCODE_DL: {
        ModelRoData_DisplayListRecord *d = nat;

        /* Primary/Secondary stay vma-coded; render/rewrite code masks
         * the low 24 bits against BaseAddr */
        d->Primary = (Gfx *)(uintptr_t)*(u32 *)(ro + 0x0);
        d->Secondary = (Gfx *)(uintptr_t)*(u32 *)(ro + 0x4);
        d->BaseAddr = base;
        d->Vertices = buildResolve(b, *(u32 *)(ro + 0xC));
        d->numVertices = *(u16 *)(ro + 0x10);
        d->ModelType = *(s8 *)(ro + 0x12);
        break;
    }

    case MODELNODE_OPCODE_OP05: {
        ModelRoData_Op05Record *d = nat;

        d->NumChildren = *(s32 *)(ro + 0x0);
        d->Vertices = buildResolve(b, *(u32 *)(ro + 0x8));
        d->Images = buildResolve(b, *(u32 *)(ro + 0xC));
        memcpy(d->Data, ro + 0x10, sizeof(d->Data));
        d->unk1A0 = *(u32 *)(ro + 0x1A0);
        d->BaseAddr = base;
        /* Children filled by caller (relocated array) */
        break;
    }

    case MODELNODE_OPCODE_OP06: {
        ModelRoData_Op06Record *d = nat;

        d->unk00 = *(u32 *)(ro + 0x00);
        d->unk04 = *(u32 *)(ro + 0x04);
        d->unk08 = *(u32 *)(ro + 0x08);
        d->unk0C = *(u32 *)(ro + 0x0C);
        d->unk10 = *(u32 *)(ro + 0x10);
        d->BaseAddr = base;
        break;
    }

    case MODELNODE_OPCODE_OP07: {
        ModelRoData_Op07Record *d = nat;

        d->unk00 = buildResolve(b, *(u32 *)(ro + 0x0));
        d->unk04 = buildResolve(b, *(u32 *)(ro + 0x4));
        d->NumChildren = *(s32 *)(ro + 0x8);
        d->Vertices = buildResolve(b, *(u32 *)(ro + 0x10));
        d->Images = buildResolve(b, *(u32 *)(ro + 0x14));
        memcpy(d->Data, ro + 0x18, sizeof(d->Data));
        d->unk1A8 = *(u16 *)(ro + 0x1A8);
        d->RwDataIndex = *(u16 *)(ro + 0x1AA);
        d->BaseAddr = base;
        break;
    }

    case MODELNODE_OPCODE_LOD: {
        ModelRoData_LODRecord *d = nat;

        memcpy(&d->MinDistance, ro + 0x0, 4);
        memcpy(&d->MaxDistance, ro + 0x4, 4);
        d->Affects = buildResolve(b, *(u32 *)(ro + 0x8));
        d->RwDataIndex = *(u16 *)(ro + 0xC);
        d->reserved = *(u16 *)(ro + 0xE);
        break;
    }

    case MODELNODE_OPCODE_BSP: {
        ModelRoData_BSPRecord *d = nat;

        memcpy(&d->Point, ro + 0x0, 12);
        memcpy(&d->Vector, ro + 0xC, 12);
        d->leftChild = buildResolve(b, *(u32 *)(ro + 0x18));
        d->rightChild = buildResolve(b, *(u32 *)(ro + 0x1C));
        d->reserved = *(s16 *)(ro + 0x20);
        d->RwDataIndex = *(u16 *)(ro + 0x22);
        break;
    }

    case MODELNODE_OPCODE_OP11: {
        ModelRoData_Op11Record *d = nat;

        memcpy(d->unk0c, ro + 0x0, 16 * 4);
        /* the game's promote pass rebases unk0c[15] like a pointer */
        d->unk0c[15] = (u32)(uintptr_t)buildResolve(b, *(u32 *)(ro + 0x3C));
        memcpy(&d->BoundingVolumeRadius, ro + 0x40, 4);
        d->RwDataIndex = *(u16 *)(ro + 0x44);
        d->unk46 = *(u16 *)(ro + 0x46);
        d->BaseAddr = base;
        break;
    }

    case MODELNODE_OPCODE_GUNFIRE: {
        ModelRoData_GunfireRecord *d = nat;

        memcpy(&d->Offset, ro + 0x0, 12);
        memcpy(&d->Size, ro + 0xC, 12);
        d->Image = buildResolve(b, *(u32 *)(ro + 0x18));
        memcpy(&d->Scale, ro + 0x1C, 4);
        d->RwDataIndex = *(u16 *)(ro + 0x20);
        d->reserved = *(u16 *)(ro + 0x22);
        d->BaseAddr = (u32)(uintptr_t)base;
        break;
    }

    case MODELNODE_OPCODE_SHADOW: {
        ModelRoData_ShadowRecord *d = nat;

        memcpy(&d->pos, ro + 0x0, 8);
        memcpy(&d->size, ro + 0x8, 8);
        d->image = buildResolve(b, *(u32 *)(ro + 0x10));
        d->Header = buildResolve(b, *(u32 *)(ro + 0x14));
        memcpy(&d->Scale, ro + 0x18, 4);
        d->BaseAddr = base;
        break;
    }

    case MODELNODE_OPCODE_OP17: {
        ModelRoData_Op17Record *d = nat;

        d->hitpart = *(s32 *)(ro + 0x0);
        memcpy(&d->radiusSq, ro + 0x4, 4);
        memcpy(&d->pos, ro + 0x8, 12);
        d->othernode = buildResolve(b, *(u32 *)(ro + 0x14));
        memcpy(&d->scale1, ro + 0x18, 4);
        memcpy(&d->scale2, ro + 0x1C, 4);
        break;
    }

    case MODELNODE_OPCODE_SWITCH: {
        ModelRoData_SwitchRecord *d = nat;

        d->Controls = buildResolve(b, *(u32 *)(ro + 0x0));
        d->RwDataIndex = *(u16 *)(ro + 0x4);
        d->reserved = *(u16 *)(ro + 0x6);
        break;
    }

    case MODELNODE_OPCODE_DLPRIMARY: {
        ModelRoData_DisplayListPrimaryRecord *d = nat;

        d->numVertices = *(s32 *)(ro + 0x0);
        d->Vertices = buildResolve(b, *(u32 *)(ro + 0x4));
        d->Primary = (Gfx *)(uintptr_t)*(u32 *)(ro + 0x8);
        d->BaseAddr = base;
        break;
    }

    case MODELNODE_OPCODE_DLCOLLISION: {
        ModelRoData_DisplayList_CollisionRecord *d = nat;
        s32 i;

        d->Primary = (Gfx *)(uintptr_t)*(u32 *)(ro + 0x0);
        d->Secondary = (Gfx *)(uintptr_t)*(u32 *)(ro + 0x4);
        d->Vertices = buildResolve(b, *(u32 *)(ro + 0x8));
        d->numVertices = *(s16 *)(ro + 0xC);
        d->numCollisionVertices = *(s16 *)(ro + 0xE);
        d->CollisionVertices = buildResolve(b, *(u32 *)(ro + 0x10));
        d->PointUsage = buildResolve(b, *(u32 *)(ro + 0x14));
        d->ModelType = *(s16 *)(ro + 0x18);
        d->RwDataIndex = *(u16 *)(ro + 0x1A);
        d->BaseAddr = base;

        /* the 16-byte collision vertices stay in the blob; their LinkedTo
         * u32 slots get truncated node pointers (see Vertex in bondtypes) */
        for (i = 0; i < d->numCollisionVertices; i++) {
            Vertex *cv = &d->CollisionVertices[i];

            if (cv->LinkedTo != 0) {
                cv->LinkedTo = (u32)(uintptr_t)buildResolve(b, cv->LinkedTo);
            }
        }
        break;
    }

    default:
        break;
    }
}

static void portModelRebuild64(struct ModelFileHeader *header, struct mdlctx *ctx)
{
    struct mdlbuild b;
    u32 total;
    s32 i;

    memset(&b, 0, sizeof(b));
    b.ctx = ctx;

    buildCollect(&b, (u8 *)header->RootNode);

    /* size the extension */
    total = ((u32)header->numSwitches * sizeof(void *) + 8 + 7) & ~7u;
    for (i = 0; i < b.count; i++) {
        u8 *node = ctx->base + b.objs[i].nodeOff;
        u16 op = (*(u16 *)node) & 0xFF;
        u32 roOff = *(u32 *)(node + 0x04);
        u32 rosz = roNativeSize(op);

        total += (sizeof(ModelNode) + 7) & ~7u;
        if (roOff != 0 && rosz != 0) {
            u8 *ro = off2ptr(ctx, roOff);
            s32 nchildren = roChildCount(ctx, op, ro);

            total += (rosz + 7) & ~7u;
            if (nchildren > 0) {
                total += ((u32)nchildren * sizeof(ModelRoData_Child) + 7) & ~7u;
            }
        }
    }

    b.ext = portLowAlloc(total);
    if (b.ext == NULL) {
        fprintf(stderr, "port/model64: extension alloc (%u bytes) failed\n", total);
        exit(1);
    }
    memset(b.ext, 0, total);
    b.extSize = total;
    b.cursor = 0;

    /* pass 2: place native objects, completing the offset->pointer map */
    for (i = 0; i < b.count; i++) {
        struct mdlobj *o = &b.objs[i];
        u8 *node = ctx->base + o->nodeOff;
        u16 op = (*(u16 *)node) & 0xFF;
        u32 roOff = *(u32 *)(node + 0x04);
        u32 rosz = roNativeSize(op);

        o->node = extAlloc(&b, sizeof(ModelNode));
        if (roOff != 0 && rosz != 0) {
            u8 *ro = off2ptr(ctx, roOff);
            s32 nchildren = roChildCount(ctx, op, ro);

            o->rodata = extAlloc(&b, rosz);
            if (nchildren > 0) {
                o->children = extAlloc(&b, (u32)nchildren * sizeof(ModelRoData_Child));
            }
        }
    }

    /* pass 3: fill nodes and rodata with resolved pointers */
    for (i = 0; i < b.count; i++) {
        struct mdlobj *o = &b.objs[i];
        u8 *node = ctx->base + o->nodeOff;
        u16 op = (*(u16 *)node) & 0xFF;
        ModelNode *n = o->node;
        u8 *ro = off2ptr(ctx, *(u32 *)(node + 0x04));

        n->Opcode = *(u16 *)(node + 0x00);
        n->Data = o->rodata != NULL ? o->rodata
                                    : buildResolve(&b, *(u32 *)(node + 0x04));
        n->Parent = buildResolve(&b, *(u32 *)(node + 0x08));
        n->Next = buildResolve(&b, *(u32 *)(node + 0x0C));
        n->Prev = buildResolve(&b, *(u32 *)(node + 0x10));
        n->Child = buildResolve(&b, *(u32 *)(node + 0x14));

        if (o->rodata != NULL && ro != NULL) {
            buildFillRodata(&b, op, ro, o->rodata);

            /* relocate Op05/Op07 child record arrays (8 -> 16 bytes) */
            if (o->children != NULL) {
                ModelRoData_Child *dc = o->children;
                s32 nchildren = roChildCount(ctx, op, ro);
                u32 arrOff = (op == MODELNODE_OPCODE_OP05) ? *(u32 *)(ro + 0x4)
                                                           : *(u32 *)(ro + 0xC);
                u8 *src = off2ptr(ctx, arrOff);
                s32 c;

                for (c = 0; c < nchildren && src != NULL; c++, src += 8) {
                    dc[c].NumEntries = src[0];
                    dc[c].unk01 = src[1];
                    dc[c].unk02 = *(u16 *)(src + 2);
                    dc[c].unk04 = buildResolve(&b, *(u32 *)(src + 4));
                }
                if (op == MODELNODE_OPCODE_OP05) {
                    ((ModelRoData_Op05Record *)o->rodata)->Children = dc;
                } else {
                    ((ModelRoData_Op07Record *)o->rodata)->Children = dc;
                }
            }

            /* the promote pass overrides Child for LOD nodes */
            if (op == MODELNODE_OPCODE_LOD) {
                n->Child = ((ModelRoData_LODRecord *)o->rodata)->Affects;
            }
        }
    }

    /* switch table: 4-byte disk slots -> native pointer array */
    {
        ModelNode **sw = extAlloc(&b, (u32)header->numSwitches * sizeof(void *) + 8);

        for (i = 0; i < header->numSwitches; i++) {
            sw[i] = buildResolve(&b, ((u32 *)ctx->base)[i]);
        }
        header->Switches = sw;
    }

    header->RootNode = buildResolve(&b, (u32)((u8 *)header->RootNode - ctx->base) + ctx->vma);

    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/model64: %d nodes rebuilt, ext %u bytes @ %p\n",
                b.count, total, (void *)b.ext);
    }

    free(b.objs);
}
#endif /* IS_64_BIT */

void portPreprocessModelFile(struct ModelFileHeader *header, void *filedata, u32 vma, u32 size)
{
    struct mdlctx ctx;
    u8 *node;
    s32 i;

    if (size == 0) {
        size = 0x100000; /* runaway guard only; real files are far smaller */
    }
    ctx.base = filedata;
    ctx.vma = vma;
    ctx.size = size;
    ctx.dlBitmap = calloc(1, size / 8 / 8 + 1);

    /* switch offsets table at the start of the file */
    for (i = 0; i < header->numSwitches; i++) {
        SWAP32(&((u32 *)filedata)[i]);
    }

    /* texture config records: {u32 TextureID; u8 x7} */
    {
        u8 *tex = (u8 *)header->Textures;

        for (i = 0; i < header->numtextures; i++, tex += 12) {
            SWAP32(tex);
        }
    }

    /* node tree walk mirroring modelPromoteNodeOffsetsToPointers */
    node = (u8 *)header->RootNode;
    while (node != NULL) {
        u16 opcode;
        u32 childOff;

        swapNodeData(&ctx, node);

        opcode = (*(u16 *)node) & 0xFF;
        childOff = *(u32 *)(node + 0x14);

        /* the promote pass overrides Child for LOD nodes */
        if (opcode == MODELNODE_OPCODE_LOD) {
            u8 *ro = off2ptr(&ctx, *(u32 *)(node + 0x04));

            if (ro != NULL) {
                childOff = *(u32 *)(ro + 0x8); /* Affects */
            }
        }

        if (childOff != 0) {
            node = off2ptr(&ctx, childOff);
        } else {
            while (node != NULL) {
                u32 nextOff = *(u32 *)(node + 0x0C);
                u32 parentOff = *(u32 *)(node + 0x08);

                if (nextOff != 0) {
                    node = off2ptr(&ctx, nextOff);
                    break;
                }
                node = off2ptr(&ctx, parentOff);
            }
        }
    }

    mdlRegisterFile(header, filedata);

#if IS_64_BIT
    /* rebuild nodes/rodata in native layout with promoted pointers
     * (replaces the game's sub_GAME_7F075A90, skipped on 64-bit) */
    portModelRebuild64(header, &ctx);
#endif

    free(ctx.dlBitmap);
}
