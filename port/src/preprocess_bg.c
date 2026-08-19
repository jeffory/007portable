/**
 * @file preprocess_bg.c
 * Load-time byteswaps for stage geometry files (PORT_PREPROCESS):
 *
 *  - bg .seg header block (header words + room table + portal table +
 *    portal point lists + env-data commands + per-room visibility floats),
 *    loaded raw by obLoadBGFileBytesAtOffset in load_bg_file().
 *  - per-room decompressed blobs: vertex tables (Vtx halfwords) and
 *    primary/secondary GDLs (pure Gfx streams, blanket u32 swap).
 *  - stan clipping file: prefix, room pointer array and tiles.
 *
 * Layout knowledge mirrors src/game/bg.c load_bg_file()/bgLoadRoom*() and
 * src/game/stan.c stanDetermineEOF()/stanBuildRoomData(); see also
 * tools/bg_binary_to_c.py for the on-disk format.
 */
#include <ultra64.h>
#include <platform_info.h>
#include "port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 swap32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static u16 swap16(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

static void swap32Run(void *ptr, s32 count)
{
    u32 *w = ptr;
    while (count-- > 0) {
        *w = swap32(*w);
        w++;
    }
}

/* The 0x40-byte header probe read onto the stack: all words. */
void portSwapBgHeaderProbe(void *buf)
{
    swap32Run(buf, 0x40 / 4);
}

/**
 * Full header block [0, size): header words 0-4, room table (6 u32 records,
 * through the terminator whose pPriMappingBin is 0), portal table (u32
 * offset + 4 control bytes per record, offset word only), each portal's
 * point list (u8 count + f32 xyz triples), env-data commands (u8 type +
 * s32 data) and, if present, the per-room f32 visibility table.
 */
void portSwapBgFile(void *data, u32 size)
{
    u8 *base = data;
    u32 *hdr = data;
    u32 offRooms, offPortals, offEnv, offFlt;
    s32 rooms = 0;
    u32 off;

    swap32Run(hdr, 5);
    offRooms = hdr[1] & 0x00FFFFFF;
    offPortals = hdr[2] & 0x00FFFFFF;
    offEnv = hdr[3] ? (hdr[3] & 0x00FFFFFF) : 0;
    offFlt = (hdr[3] && hdr[4]) ? (hdr[4] & 0x00FFFFFF) : 0;

    if (offRooms != 0 && offRooms < size) {
        u32 *rec = (u32 *)(base + offRooms);
        s32 idx = 0;
        for (;;) {
            swap32Run(rec, 6);
            /* record 0 is a dummy; the game's room count loop starts at 1
             * (bg.c "for (i = 1; ...pPriMappingBin != NULL...)") */
            if (idx >= 1 && rec[1] == 0) {
                break;
            }
            if (idx >= 1) {
                rooms++;
            }
            idx++;
            rec += 6;
            if ((u8 *)rec + 24 > base + size) {
                break;
            }
        }
    }

    if (offPortals != 0 && offPortals < size) {
        u32 *rec = (u32 *)(base + offPortals);
        while ((u8 *)rec + 8 <= base + size) {
            *rec = swap32(*rec);
            if (*rec == 0) {
                break;
            }
            off = *rec & 0x00FFFFFF;
            if (off != 0 && off + 4 <= size) {
                u8 npts = base[off]; /* u8 numPoints, 3 pad bytes */
                if (off + 4 + (u32)npts * 12 <= size) {
                    swap32Run(base + off + 4, npts * 3); /* f32 xyz per point */
                }
            }
            rec += 2; /* 8-byte records */
        }
    }

#if IS_64_BIT
    /* The portal table is 8-byte records {u32 segoffset; u8 rooms[2];
     * u8 control[2]} indexed in place through the native struct
     * bg_portal_data_entry, whose pointer member makes it 16 bytes on a
     * 64-bit host. Relocate it into a native-layout array and patch the
     * header's portal offset so that bg.c's
     *   g_BgPortals = BG_SEG_TO_PTR(data, offsets[2])
     * (u32 wraparound math) lands exactly on the relocated array. */
    if (offPortals != 0 && offPortals < size) {
        /* native-layout mirror of bg_portal_data_entry (bg.h) */
        struct portalnat { void *offset_portal; u8 r1, r2, c1, c2; };
        u32 *rec = (u32 *)(base + offPortals);
        s32 count = 0;
        struct portalnat *nat;
        s32 i;

        while ((u8 *)&rec[count * 2] + 8 <= base + size && rec[count * 2] != 0) {
            count++;
        }
        nat = portLowAlloc((u32)(count + 1) * sizeof(struct portalnat));
        for (i = 0; i < count; i++) {
            /* keep the segment offset as a VALUE in the pointer slot; the
             * game's own rebase loop converts it via BG_SEG_TO_PTR */
            nat[i].offset_portal = (void *)(uintptr_t)rec[i * 2];
            nat[i].r1 = ((u8 *)&rec[i * 2 + 1])[0];
            nat[i].r2 = ((u8 *)&rec[i * 2 + 1])[1];
            nat[i].c1 = ((u8 *)&rec[i * 2 + 1])[2];
            nat[i].c2 = ((u8 *)&rec[i * 2 + 1])[3];
        }
        nat[count].offset_portal = NULL;

        /* craft offsets[2] so base + off + 0xF1000000 (u32) == nat */
        hdr[2] = (u32)(uintptr_t)nat - (u32)(uintptr_t)base - 0xF1000000u;

        if (getenv("PORT_LOAD_TRACE") != NULL) {
            fprintf(stderr, "port/bg64: %d portals relocated to %p\n", count, (void *)nat);
        }
    }
#endif

    if (offEnv != 0 && offEnv < size) {
        u8 *rec = base + offEnv;
        while (rec + 8 <= base + size && rec[0] != 0) { /* u8 type, pad[3] */
            swap32Run(rec + 4, 1);                       /* s32 data */
            rec += 8;
        }
    }

    /* f32 per room (indexed [room + 1] by sub_GAME_7F0B4F9C). */
    if (offFlt != 0 && offFlt < size) {
        s32 n = rooms + 2;
        if (offFlt + (u32)n * 4 > size) {
            n = (s32)((size - offFlt) / 4);
        }
        swap32Run(base + offFlt, n);
    }
}

/* Decompressed room vertex table: 16-byte Vtx, coords/flag/st are
 * halfwords (first 12 bytes), colors stay raw bytes. */
void portSwapBgRoomVertices(void *ptr, s32 bytes)
{
    u8 *v = ptr;
    s32 n = bytes / 16;
    while (n-- > 0) {
        s32 i;
        for (i = 0; i < 12; i += 2) {
            *(u16 *)(v + i) = swap16(*(u16 *)(v + i));
        }
        v += 16;
    }
}

/* Decompressed room GDL blobs are pure Gfx command streams. */
void portSwapBgRoomGdl(void *ptr, s32 bytes)
{
    swap32Run(ptr, bytes / 4);
}

/**
 * Stan clipping file, called before stanDetermineEOF() rebases anything:
 * 2 prefix words, the NULL-terminated room pointer array, then tiles until
 * a zero header word. Tile word 0 keeps N64 byte order except its first
 * halfword (the id-hi/flag bits every byte-punning reader reads as u16);
 * byte 2 (id-lo digit) and byte 3 (room) are position-stable. mid/tail
 * halfwords and all point halfwords are swapped.
 */
void *portSwapStanFile(void *file)
{
    static const u8 tilesizes[] = {
        0x20, 0x20, 0x20, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x00
    };
    u32 *w = file;
    u32 *slots;
    s32 nslots;
    u8 *tile;
    u8 *tilesStart;
    s32 ntiles = 0;

    swap32Run(w, 1); /* stanfile tag; the room pointer array below covers
                        ptr_firstroom itself (it is the array's first slot) */
    w += 1;
    slots = w;
    while (*w != 0) {
        *w = swap32(*w);
        w++;
    }
    nslots = (s32)(w - slots);
    w++; /* skip the NULL terminator; tiles follow */

    tile = (u8 *)w;
    tilesStart = tile;
    while (*(u32 *)tile != 0) {
        u16 tail;
        s32 pc, sz, i;

        *(u16 *)(tile + 0) = swap16(*(u16 *)(tile + 0)); /* id-hi/visit bit */
        *(u16 *)(tile + 4) = swap16(*(u16 *)(tile + 4)); /* mid */
        *(u16 *)(tile + 6) = swap16(*(u16 *)(tile + 6)); /* tail */
        tail = *(u16 *)(tile + 6);
        pc = (tail >> 12) & 0xF;
        sz = tilesizes[pc];
        if (sz == 0) {
            fprintf(stderr, "port: portSwapStanFile: bad tile size nibble %d\n", pc);
            break;
        }
        for (i = 8; i < sz; i += 2) {
            *(u16 *)(tile + i) = swap16(*(u16 *)(tile + i));
        }
        tile += sz;
        ntiles++;
    }
    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port: stan swapped, %d tiles\n", ntiles);
    }

#if IS_64_BIT
    /* The file is [tag][room OFFSET slots, 4 bytes each][0][tiles...], but
     * stan.c walks the slot array through StanPrefixRecord/void** — 8-byte
     * strides — and stanDetermineEOF then expects tiles right after the
     * terminator. Rebuild the whole file in native layout: 8-byte tag+pad,
     * 8-byte slots (still holding block-relative OFFSETS: the game's own
     * +delta rebase runs afterwards), 8-byte terminator, tiles verbatim. */
    {
        u32 tileBytes = (u32)(tile - tilesStart) + 8; /* include terminator */
        u32 natTilesOff = 8 + (u32)(nslots + 1) * 8;
        u32 total = natTilesOff + tileBytes;
        u8 *nat = portLowAlloc(total);
        u32 blobTilesOff = (u32)(tilesStart - (u8 *)file);
        s32 i;

        if (nat == NULL) {
            fprintf(stderr, "port/stan64: alloc %u failed\n", total);
            exit(1);
        }
        *(s32 *)nat = *(s32 *)file;      /* stanfile tag */
        *(u32 *)(nat + 4) = 0;
        for (i = 0; i < nslots; i++) {
            /* blob-relative offset -> native-block-relative offset */
            *(uintptr_t *)(nat + 8 + i * 8) =
                (uintptr_t)(slots[i] - blobTilesOff + natTilesOff);
        }
        *(uintptr_t *)(nat + 8 + nslots * 8) = 0;
        memcpy(nat + natTilesOff, tilesStart, tileBytes);

        if (getenv("PORT_LOAD_TRACE") != NULL) {
            fprintf(stderr, "port/stan64: rebuilt %d rooms + %u tile bytes at %p\n",
                    nslots, tileBytes, (void *)nat);
        }
        return nat;
    }
#else
    return file;
#endif
}
