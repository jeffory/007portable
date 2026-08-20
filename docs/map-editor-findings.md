# Map Editor for the GE007 PC Port — Research Findings

*Research date: 2026-08-20. Three tracks: codebase exploration (level formats + loading;
setup/AI/asset pipeline), and web research on prior art and reusable tooling.*

## Executive summary

- A GE level is **three ROM files** (bg geometry, stan clipping, Usetup gameplay) plus a handful
  of compiled-in per-LEVELID registration tables. The formats are **~90% already specced in
  executable form** inside this repo — no reverse engineering needed to write a compiler.
- **Rare's own devkit host-filesystem loader (indycomm) is still in the code** and is the natural
  disk-loading hook: `fileGetIndex` auto-registers unknown filenames and routes `hw_address==0`
  entries to host loading. Mod loading from a folder is nearly free (one real gap + two latent
  bugs, detailed below).
- **Nobody has built a decomp-port-native GE/PD map editor** (as of Aug 2026). The proven patterns
  to copy are the Perfect Dark PC port's `--moddir` (native-format stage files from a folder — 70+
  community MP arenas shipped that way) and sm64coopdx's DynOS (engine ingests Blender exports at
  runtime).
- The incumbent **GoldenEye Setup Editor 4.3** is closed source (CC BY-NC-ND — can't fork), but
  its output is ROM-native binaries that a `--moddir` loader consumes directly, inheriting 19
  years of community levels and the standard Blender→`.obj` geometry workflow.
- Recommended shape: **mod loader → Python format toolkit → obj geometry import → in-game
  placement editor (dear imgui) → AI text assembler**. MP-first (MP setups need no AI or
  objectives; retail proves "new map = new setup on existing geometry").

---

## 1. What constitutes a level

| Artifact | Named in | Format | Compression |
|---|---|---|---|
| `bg/bg_<tag>_all_p.seg` | `levelinfotable[]` (src/game/bg.c:184) | room/portal container, linked at VA 0x0F000000 | container raw; per-room blobs "1172" |
| `Tbg_<tag>_all_p_stanZ` | same table | clipping tiles | whole file 1172 |
| `Usetup<tag>Z` / `Ump_setup<tag>Z` | `setup_text_pointers[]` (chraidata.c:816) | 10-offset stagesetup | whole file 1172 |

"1172" codec = 2-byte magic `0x11 0x72` + **raw deflate** (gzip-1.2.4 inflate; Python:
`zlib.decompressobj(wbits=-15)`).

Compiled-in tables a new stage must be registered in:

- `levelinfotable[]` — bg.c:184 (bg/stan filenames, levelscale, visibility). **Append-only**:
  `specialportalarray` (bg.c:824) is indexed by *row*, so inserting shifts Aztec/Egypt's special
  portals.
- `setup_text_pointers[]` — chraidata.c:816 (indexed by LEVELID, bound `LEVELID_MAX+1` at
  prop.c:1239). LEVELID space **57..89 is free** (watch fog's composite keys: levelid +
  100/200/300/400/900).
- `memallocstringtable[]` — src/boss.c:103. Per-stage memory tokens `-mgfx/-mvtx/-mt/-ma` (KB).
  Conveniently ends with **3 zero rows** to fill at boot. Note the `stage+400` MP id convention
  (boss.c:382).
- `fog_tables[]` (bgfog.c:119), `music_setup_entries[]` (music_0D2720.c:15),
  `mission_folder_setup_entries[]` / `multi_stage_setups[]` (front.c:435/547 — MP table uses
  `min_player==0` as a hidden sentinel and has 11 commented-out rows, so the UI was sized for
  more).

`assets/obseg/**` holds the **complete round-trippable source form** of all 38 bg, 38 stan, and
57 setup files (Getools-generated C, with Makefiles and linker scripts still in-tree — only the
mips toolchain was deleted). Together with the port's preprocess walkers, any emitter can be
validated byte-for-byte against retail via the existing `PORT_CRC_TRACE` golden harness.

## 2. BG (geometry) format

- **Header** (20 B): `{0, →room_table, →portal_table, →vis_commands, 0}` — hdr[0] and hdr[4] are
  zero in every retail file. Everything up to the first room blob is loaded resident; blobs are
  streamed (`load_bg_file`, bg.c:792+).
- **Room table** (24 B/entry): 3 seg-offsets (point blob, primary GDL, optional secondary GDL) +
  f32 room position. Entry 0 is a dummy; a terminator entry then `{0}` follow the last room.
  **Blob sizes are derived from offset adjacency** → physical layout is pinned: all point blobs
  contiguous, then pri/sec GDLs interleaved per room, then end sentinels.
- **Portals** (8 B/entry): seg-offset to a `{u8 numPoints; pad[3]; coord3d points[n]}` struct
  (absolute f32 coords, mostly 4-point rectangles) + two room bytes + control bytes. Limits:
  200 portals, 20 per room.
- **Visibility command stream**: 8-byte `{u8 op; u8 len; pad; s32 arg}` records (opcodes at
  bg.c:40-65; `0x64`/`0x65` are operand words). **Minimum valid stream is a single `VISOP_END`**
  — retail bg_ame/bg_sevx do exactly that, so a custom map can skip vis scripting entirely and
  rely on portal traversal.
- **Room contents**: s16 room-local `Vtx` (colors carry baked lighting), F3D GDLs that reference
  textures **by 12-bit texnum inside `G_NOOP` markers**, expanded at load by `texLoadFromGdl`
  (tex.c:779). `levelscale` is the file→world divisor shared by bg and stan (retail range
  0.0896–1.206).
- **Streaming**: `bgLoadRoomModelData` (bg.c:2443) allocates from the `mema` arena (the `-ma`
  token — **the binding constraint on map size**, 100–400 KB retail; PC arena is 32 MB so it's
  just a knob). `bgDecompress` is called **unconditionally** on room blobs → they must be
  1172-compressed inside the .seg.

## 3. Stan (clipping) format

`[u32 0][u32 room_ptr[]][0][room-sorted tiles][8B zero][footer "unstric"]`.

Tile: `u32 id:24|room; u16 mid (special|r|g|b nibbles); u16 tail (pointCount hi-nibble …)`, then
up to **10** `{s16 x,y,z; u16 link}` points. Stride comes from a lookup table
(`list_of_tilesizes`, stan.c:78), not arithmetic. `link` = neighbour tile offset in 8-byte units
from `firstTile − 0x80` → tiles must sit at 8-byte-addressable offsets. Tiles must be sorted by
room (room-change detection builds `firststaninroom[139]` — note 139 stan rooms vs 150 bg rooms).
Pad↔tile linkage uses packed name strings (`"p3830a"`), but **pads fall back to positional tile
lookup** when the name doesn't resolve (initpathtablesomething.c:9-37) — an editor can emit
coordinates plus a dummy `plink` and rough clipping degrades gracefully.

## 4. Setup format (the gameplay half)

10 u32 file-relative offsets (`stagesetup`, bondtypes.h:4022): waypoints, waygroups, intro
records, propdefs, patrol paths, ailists, pads (0x2C B), boundpads (0x44 B), pad/boundpad names.
Offsets are rebased to pointers in place at load (`proplvreset2`, prop.c:1216 — which **re-reads
the file from the file layer on every stage load**, so save-and-reload hot-editing is free).

- **49 propdef types** (bondconstants.h:4294) — doors, guards, keys, CCTV, autoguns, glass,
  safes, the objective family, etc. Exact strides + pointer slots for all of them are in the
  *generated* `port/src/propdef_layout.inc` (from `port/tools/gen_propdef_layout.py`, which reads
  layouts out of `gdb ptype /o` against the real headers). Cross-references (TAG, linkedDoor) are
  record-index based — an emitter needs no fixups.
- Prose specs in-tree: `assets/obseg/setup/readme.md` + `readme_propdef.md`.
- **MP setups are tiny and need no AI**: `Ump_setupashZ.c` is 302 lines — pads, boundpads,
  ammo/armour/collectable propdefs, spawn intros, empty everything else. Three retail LEVELIDs
  (Basement/Stack/Library) share **one bg file** and differ only by setup — "new MP map = new
  setup" is retail-proven.

## 5. AI scripting

- 1-byte opcode + fixed params; **253 commands**. `src/bondaicommands.h` is a machine-readable
  spec: 253 `<name>_ID` / `<name>_LENGTH` pairs plus prose docs per command (regex-harvestable
  into an assembler table). `aicommands2.h` has the packed wire structs.
- Bytecode is **byte-order neutral** (never swapped by the port; 16-bit params read via a
  byte-array `ntohs`).
- Text→bytecode already exists twice: the readable dialect in `chraidata.c` and the Getools
  dialect in setup `.c` files — the C preprocessor is the current "compiler" (plus the
  `ai_print.sed` pass wired in CMake). All 18 global AI arrays are `--selftest` CRC-pinned.
- Binding: setup `AIListRecord {ptr, ID}` array; guards reference `GuardRecord.AIListID`
  (chraction.c:274). ID ranges: globals 0x0000+, chr lists 0x0401+, stage-auto-start lists
  0x1000+.
- Labels are runtime scan targets, not offsets → an assembler needs no fixup pass, and a
  disassembler (for verifying against the full retail corpus) comes nearly free.

## 6. Disk loading — the hooks that already exist

The chain `_fileNameLoadToBank → fileGetIndex → fileIndexLoadToBank` (src/game/ob.c) has two
built-in escape hatches:

1. **`hw_address == 0` → `resource_load_from_indy`** (ob.c:74): calls
   `indycommHostCheckFileExists` / `indycommHostLoadFile` (src/game/indy_comms.c:26/77 — the SGI
   devkit host FS), sniffs 1172 vs raw itself. Filenames are plain strings
   (`"bg/bg_dam_all_p.seg"`; 884-entry `file_resource_table`).
2. **`fileGetIndex` auto-registers unknown names** (ob.c:402) with `hw_address = 0` — but only
   ~9 dynamic slots (`OBJ_INDEX_MAX = OBENDSEG+10`, file_resource_id_enums.h:876; trivially
   bumpable).

Findings that change the work:

- **`indy_ready` is already 1 on PC** (`boss.c:179 → indycommInit`), and the transport bottoms
  out in no-op `osReadHost`/`osWriteHost` stubs (port/src/ultra/misc.c:127) — so
  `indycommHostCheckFileExists` currently returns an **uninitialized stack pointer**. Dormant
  only because no compiled-in entry has `hw_address==0`. A moddir implementation replaces
  latent UB, it doesn't just add a feature.
- **`obLoadBGFileBytesAtOffset` (ob.c:137/190) — the only bg reader, used for all room
  streaming — has no host branch**, and worse, guards on `rom_size != 0`, which is 0 for
  disk-backed entries → bg reads would **silently load nothing**. It needs a disk branch keyed
  on `pc_size`.
- Overriding existing filenames = null the entry's `hw_address` **after** `obInit()` (rom sizes
  are computed from hw_address adjacency at ob.c:113 — nulling earlier corrupts the neighbour).
- **Byte order decision: mod files should be big-endian ROM-format.** Every load site swaps
  unconditionally (portSwapBgFile/StanFile/SetupFile, text banks, room vtx/GDL) — BE keeps
  retail and custom files on one code path, keeps the golden harness valid for mods, and is
  exactly what the Setup Editor ecosystem produces.

## 7. Limits and budgets

| Limit | Value | Where |
|---|---|---|
| bg rooms | 150 | `MAXROOMCOUNT`, bondconstants.h:4574 |
| stan rooms | 139 | stan.h:59 |
| portals / per-room | 200 / 20 | bg.h:21/18 |
| points per stan tile | 10 | stan.c:78 size LUT |
| texnums in a GDL | 4096 (12-bit) | tex.c:826 |
| dynamic file slots | 9 | OBJ_INDEX_MAX |
| room geometry arena | `-ma` token (100–400 KB retail) | boss.c:103, bg.c:2470 |

All statically sized and trivially enlargeable on PC (arena is already 32 MB; the port already
forces `-mt4096` for textures).

Custom textures: `g_Textures[]` is compiled-in with 24-bit offsets into the images segment;
cleanest injection is a **texnum ≥ 2700 side table** spliced at the existing range guard in
`texLoad` (image.c:~2451), decoding PNGs from the mod dir. Near-term: reuse retail texnums
(zero work). Text banks are plain C string arrays → trivial. Custom prop models are the hardest
emitter in the project (node-tree blob at VA 0x05000000) — defer; 341 retail props + 47 bodies
are the palette. Custom music: out of scope (MIDI + instrument banks); pick retail tracks.

---

## 8. Prior art (web research)

### GoldenEye Setup Editor (SubDrag → Carnivorous)
- https://github.com/carnivoroussociety/GoldEditor — current 4.3 (Mar 2025). **Closed source**
  (repo holds only README + LICENSE, CC BY-NC-ND) — interoperate, don't fork.
- Edits everything: setups, AI (its own pseudocode editor), clipping, portals, visibility, text,
  models; imports **full custom levels from .obj** using group conventions
  `Room01..` (hex, max 0x96), `ClipXX`, `Portal_XX_YY`; generates BG + clipping + portals and
  injects into an extended ROM. Texture constraints: 8-bit BMP, ≤32×64/64×32, ~2184 tris/room.
- Community-documented steep learning curve; Windows-only; ROM-injection workflow.
- **Goldfinger 64** (20-level campaign) proves the pipeline: Hammer/XSI/Blender → obj → Setup
  Editor. Tutorial: https://www.moddb.com/mods/goldfinger-64/tutorials/goldeneye-007-hacking-create-new-levels

### Perfect Dark PC port (fgsfdsfgs/perfect_dark) — closest prior art
- https://github.com/fgsfdsfgs/perfect_dark/wiki/Modding — `--moddir <dir>` + `modconfig.txt`
  remaps bgfile/tilesfile/padsfile/setupfile/mpsetupfile by name; loads **native-format files
  from disk**, no ROM injection. 70+ community MP arenas shipped on the port this way.
- **No editor was built** — authoring stays in the Windows Setup Editor; the port only consumes
  output. This pattern transplants directly to GE (shared BG format lineage).

### sm64coopdx / DynOS — strongest "engine ingests editor output" model
- https://github.com/coop-deluxe/sm64coopdx — DynOS parses **fast64 (Blender) C exports** (geo,
  models, collision, level scripts) from a mod folder at runtime, caching to .bin. Plus a Lua
  API with ~4000 generated bindings.

### Ship of Harkinian / 2Ship
- O2R (zip) mod archives, filename load order — clean packaging, but custom *maps* were never
  solved there either; the ecosystem is texture/model/audio swaps.

### The gap
No decomp-port-native GE map editor, mod loader, or DynOS-equivalent exists (searched Aug 2026).
The GE decomp hit 100% in 2025; GoldenRecomp exists (static recomp, WIP). The space is open.

## 9. Reusable tools & libraries

| Tool | License | Use |
|---|---|---|
| dear imgui | MIT | in-game editor UI (GL3.0-compat context + single swap point at gfx_sdl2.cpp:380 verified compatible) |
| stb_image | PD/MIT | PNG → RGBA16 mod textures |
| Python zlib | — | 1172 codec (wbits=-15) |
| libgfxd (glankk) | MIT | F3D DL decode oracle for cross-checking our emitter |
| getools (burnsba) | GPL/MIT | C# GE setup models, bin↔JSON, web SVG level viewer — reference |
| fast64 | GPL | Blender F3D export — reference now, potential GE backend later |
| TrenchBroom + ericw-tools | GPL | rejected front-end (brush CSG → triangulation/portal mapping downstream); Blender/obj is the proven GE path |
| **this repo** | — | preprocess walkers = format spec; `portRebuildSetupFile64` = a working setup re-emitter; PORT_CRC_TRACE + run_goldens.sh = byte-exact validation harness |

## 10. Recommended architecture

**Hybrid, MP-first.** Geometry in Blender (`.obj`, community group conventions); placement and
gameplay in an **in-game imgui editor** with a ~2 s save-&-reload loop; AI as text (`.geai`,
Getools mnemonics, Python assembler verified by round-tripping the full retail corpus); all mod
files big-endian ROM-format in a `--moddir` folder.

Phases (full detail in the implementation plan):

- **A — mod loader** (`--moddir`): implement indycommHost PC-side in `port/src/moddir.c`, fix the
  `obLoadBGFileBytesAtOffset` gap, stage manifest filling pre-reserved registration-table rows,
  `OBJ_INDEX_MAX` bump. Loads Setup Editor ecosystem output directly. Identity-tested by serving
  retail bytes through every new path (CRC/statehash streams must match ROM-served baselines).
- **B — Python format toolkit** (`tools/mapformats/`): 1172 codec + bg/stan/setup parse/emit with
  JSON IR; validated by 133-file byte round-trip + CRC parity through the moddir.
- **C — geometry import**: F3D vocabulary study on retail rooms (byte re-emit as the gate), then
  obj→bg (s16 quantization, G_NOOP texnums, VISOP_END vis stream) and ClipXX→stan (adjacency
  links, positional-fallback safety net). Acceptance: a committed 2-room box map, MP-playable.
- **D — in-game editor**: imgui overlay (render before `SDL_GL_SwapWindow`; third SDL event
  watch, pattern proven by `inputEventWatch`), native BE setup emitter in C (~500 lines off
  `portRebuildSetupFile64` + `propdef_layout.inc`), palette/inspector for MP's ~8 propdef types,
  Save & Reload through the normal stage-load path, fly camera.
- **E — scripting + assets**: `.geai` assembler/disassembler, PNG texture side table (texnum
  ≥2700), text-bank tool, `ge007-mod` CLI (new/build/watch/pack) + mp-arena template. Lua
  deferred deliberately (huge surface, breaks the determinism/statehash culture).

**MVP = A + D**: edit MP setups over retail geometry, in-game — the retail-proven
"three stages share one bg" path. Full custom geometry = +B +C.

Target workflow:

```
ge007-mod new mymap --template mp-arena      # playable box arena immediately
ge007 --moddir mods/mymap --stage mymap
# Blender: model rooms → export geometry.obj (RoomXX/ClipXX/Portal_XX_YY)
ge007-mod build mymap                        # obj→bg+stan, .geai→ailists, text→banks
ge007 --moddir mods/mymap --stage mymap --editor   # place, tune, Save & Reload (~2s)
ge007-mod pack mymap                         # shareable zip
```

Friction removed vs the Setup Editor: no Windows-only closed tool, no ROM injection, no import
wizard, instant in-engine WYSIWYG preview. Friction remaining (honest): Blender competence for
geometry (unavoidable; mitigated by templates and setup-only maps on retail bg), AI in text
(mitigated by familiar mnemonics + templates), reload is a stage reload rather than live
mutation.

## 11. Sources

GoldEditor: https://github.com/carnivoroussociety/GoldEditor ·
Setup Editor wiki: https://goldeneye.fandom.com/wiki/Goldeneye_Setup_Editor ·
New-levels tutorial (PDF): http://n64vault.wdfiles.com/local--files/pd-guides:all-new-levels-general-setup-editor-guide/GEEditV2TutorialAllNewLevelsUpdated.pdf ·
Goldfinger 64: https://www.moddb.com/mods/goldfinger-64 ·
PD port modding: https://github.com/fgsfdsfgs/perfect_dark/wiki/Modding ·
PD community arenas: https://metalgamesolid.com/games/perfect-dark-pc-port-expands-with-new-mods-and-arenas/ ·
sm64coopdx: https://github.com/coop-deluxe/sm64coopdx ·
DynOS writeup: https://djoslin.info/projects/sm64ex-coop/ ·
SoH: https://github.com/HarbourMasters/Shipwright ·
fast64: https://github.com/Fast-64/fast64 ·
libgfxd: https://github.com/glankk/libgfxd ·
getools: https://github.com/burnsba/getools ·
TrenchBroom: https://github.com/TrenchBroom/TrenchBroom ·
ericw-tools: https://github.com/ericwa/ericw-tools ·
N64 Vault mirror: https://turoksanctum.com/n64vault/blank-test-home/goldeneye/ ·
n64decomp/007: https://github.com/n64decomp/007 ·
GoldenRecomp: https://github.com/kholdfuzion/GoldenRecomp
