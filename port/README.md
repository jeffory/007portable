# GoldenEye 007 PC port (work in progress)

A native PC port of this decompilation, modeled on the
[Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark):
the game logic in `src/game/` compiles unmodified (up to `#ifdef TARGET_N64`
seams); the N64 plumbing (threads, TLB code pager, PI DMA, VI, scheduler)
is replaced by the small platform layer in this directory.

**M6 (x86_64) status:** the port also builds and runs as native 64-bit
(`cmake -B build-64 -G Ninja`, no toolchain file). Strategy: every
allocation the game can point a u32 at lives below 4GB (mmap MAP_32BIT
arena/ROM/game stack + `-no-pie` statics), so the shared code's pervasive
`(u32)` pointer truncation round-trips; 32-bit-layout asset blobs whose
overlay structs contain pointers are rebuilt in native layout at load
(model node trees, setup record arrays, stan file, bg portal table), and
a few structs are pinned to their 32-bit layout with u32 pointer slots
(Gfx words, Vtx, ModelAnimation, fontchar, bg_room_data). Selftest,
menus and `--stage dam` (world geometry) work on 64-bit; still TODO
there: setup prop definitions (stage objects/guards are disabled — the
loader prints a PORT_TODO warning) and audio (disabled unless
`PORT_64_AUDIO=1`; alBnkfNew and friends still promote 32-bit offsets
through native-width ALBank structs). The 32-bit build remains the
fully-featured one.

## Controls

Mouselook is on by default (`PORT_MOUSELOOK=0` disables, F1 releases the
mouse): the mouse looks/turns, WASD moves, LMB fires, RMB aims, R
reloads (GE's B action, so it also opens doors), Space also fires,
X/F = A, C = B, Enter = Start, Q/E = shoulder buttons, arrows = analog
stick, IJKL = raw C buttons. With mouselook off the game runs its stock
1.1 control style and WASD drives the analog stick (tank-style, like the
N64). Gamepads work via SDL_GameController with hotplug.

All key/mouse bindings are configurable: a commented
`~/.config/ge007/input.ini` is written on first run (respects
`XDG_CONFIG_HOME`; `PORT_INPUT_CONFIG=<path>` overrides). Keys are SDL
scancode names (`W`, `Left Shift`, `Return`, ...) or `Mouse1`..`Mouse5`,
up to 4 per action, and the file also holds mouse `sensitivity` and
`invert`. Delete it to regenerate the defaults. `PORT_INPUT_TRACE=1`
logs pressed keys and the resulting pad state once a second.

**Status: milestone M4 — audio works.** Music and sound effects
synthesize through GoldenEye's own libultra sequencer/synthesizer,
compiled natively and driving a software implementation of the RSP audio
microcode (vendored from sm64-port), output via SDL. Set
`PORT_AUDIO_DUMP=<file>` to capture raw PCM (22050 Hz stereo s16),
`SDL_AUDIODRIVER=dummy` for headless runs.

**Post-M6 fixes — audio whine and character textures.** The constant
high-pitched tone under music was an envelope-semantics mismatch: Rare's
aspMain treats the `aSetVolume(A_RATE)` pair as a signed Q16.16 volume
*increment* per 8 samples (env.c `_getVol`), while the vendored sm64
mixer used it as a *multiplier* — steady-state voices pass the
"snap-to-target" sentinel (`-0x8000`), which overflowed into a garbage
8-sample volume staircase on every envelope init (a ~2.7 kHz harmonic
comb). `aEnvMixerImpl` is now a scalar additive implementation. Broken
character skins (cyan faces, orange hair on the cast screens and guards)
were an endianness bug: the non-zlib texture decoders in
`src/game/image.c` pack RGBA16/IA16/RGBA32 texels with native-endian
stores, which the RDP (and fast3d) read as big-endian bytes; the port
now byteswaps the finished texel words at the end of
`texInflateNonZlib`. Debug: `PORT_NO_REVERB=1` disables the fx path,
`PORT_ENV_TRACE=<n>` logs envelope-mixer parameters.

The "checkerboard" on the folder Bond photos (previously misdiagnosed as
authentic dithered alpha) was `texSwapAltRowBytes`: the game pre-swizzles
odd rows of mip-mapped textures (32-bit word pair exchange) so a dxt=0
LoadBlock leaves TMEM in the layout the RDP sampler un-swizzles at fetch
time. fast3d reads texture RAM linearly, so every LOD'd texture — the
photos, the paperclip, the folder crest, cliff/terrain textures — drew
with odd rows rotated half a TMEM word. The swizzle is now a no-op on PC.
Debug: `PORT_TEX_TRACE=1` logs each texture's format on load,
`PORT_TEX_DUMP=<num>` writes a decoded texture to /tmp,
`PORT_DUMP_COUNT=<n>` widens the PORT_VTX_DUMP tri/rect dumps.

**Post-M6 fix — full natural boot works end to end.** Three bugs killed
the screen after the Rare logo: the game's `while (alCSPGetState(...));`
spins relied on the N64 audio thread (the port now pumps synthesis via
`portAudioPump()` inside those waits); GCC's unsigned enums broke the
cast screen's `head >= 0` / `body < 0` sentinel checks (signed values
forced on PC in bondconstants.h); and the attract demo played unswapped
big-endian input recordings, freezing the scene with the keyboard dead
(the PC build loops the attract sequence instead — swapping the ramrom
demo format is a PORT_TODO in ramromreplay.c). Legal screen → logos →
gunbarrel → cast → GoldenEye logo → file select → mission select now run
unattended and respond to input. fast3d gained env-gated render
debugging: `PORT_MAX_FLUSHES=<n>` (drop draw batches after n per frame),
`PORT_GL_TRACE=<n>` + `PORT_GL_TRACE_AFTER=<s>` (per-draw GL state and
non-black pixel counts), `PORT_FINAL_PROBE=<n>` (backbuffer content at
swap time), `PORT_RED_TEST=<alpha>` (present-path check),
`PORT_TASK_TRACE=1` (gfx task rate).

Previously: milestone M3 — the Dam is playable and guards fight
back. `./build-port/ge007-port --stage dam` boots straight into the
mission: level geometry, textures, lighting and depth render; guards
render (models, heads, weapons) and run their authentic AI — gunfire
alerts the base, they hunt Bond down and return fire, and they take
bullet damage. Weapon fire, reload and Bond's health all work.
bg/stan/setup files are byteswapped at load and the built-in AI scripts
compile byte-exact vs the retail ROM. Debug: `--verbose` prints
per-30-frame triangle counts; `PORT_NO_Z` / `PORT_NO_FOG` isolate
depth/fog; arrow keys = stick, Return = START, X/C = A/B, Space = Z
(fire). Known cosmetics: guard vertical position drifts during long
chases; aim-sight rings oversized.

Previously: The full intro renders through the PD port's `fast3d`
(GL 3.0): legal screen, 3D Nintendo and Rare logos, the gunbarrel (with
blood), the GoldenEye title card, and the interactive file-select menu
(khaki folders, Bond photos, gold 007 crests, Copy/Erase, smooth
gunbarrel backdrop). The photos' checkerboard look is authentic
dithered-alpha texture data that the N64's VI filter used to blur into
translucency. No audio yet (M4). Gameplay needs the M3 level-data
preprocess. Debug helpers: `PORT_AUTOSTART=1` (synthetic
START presses), `PORT_LOAD_TRACE`, `PORT_MDL_TRACE`, `PORT_VTX_DUMP`
with `PORT_DUMP_AFTER=<seconds>`.

## Building (Fedora)

```sh
sudo dnf install cmake ninja-build gcc \
     glibc-devel.i686 libgcc.i686 libatomic.i686 \
     sdl2-compat-devel.i686 mesa-libGL-devel.i686 libglvnd-devel.i686 mesa-dri-drivers.i686

cmake -B build-port -G Ninja --toolchain port/cmake/i686-toolchain.cmake
ninja -C build-port
```

The build is 32-bit (i686) by design: the game's asset formats embed 32-bit
offsets that are rebased in place as pointers, and one macro
(`BG_SEG_TO_PTR`) depends on u32 wraparound. 64-bit is milestone M6.

Without the SDL2 devel packages the build falls back to a headless null
backend (enough for `--selftest` and CI). Without `glibc-devel.i686` it
compiles but cannot link.

## Running

Put the US ROM at `data/ge007.u.z64`
(z64 byte order, sha1 `abe01e4aeb033b6c0836819f549c791b26cfde83` — the same
file the N64 build byte-matches against). Then:

```sh
./build-port/ge007-port --selftest    # ROM sha1 + romCopy + 1172 decompress check
./build-port/ge007-port --stub-stage  # boss loop breathing, no stage load
./build-port/ge007-port               # attempts real boot (M1: may crash in stage load)
```

Saves go to `data/eeprom.bin`. 32-bit GL on Wayland may need
`SDL_VIDEODRIVER=x11`.

## Layout

| Path | What |
|---|---|
| `port/src/main.c` | entry point; replaces `boot.s`/`init.c`/`mainproc` |
| `port/src/sched.c` | cooperative pump replacing `src/sched.c` (retrace/done message economy) |
| `port/src/ultra/` | the ~60 `os*` entry points (message queues, timers, PI→ROM-file, VI/SI stubs, file-backed EEPROM) |
| `port/src/rom.c` | loads + sha1-verifies the ROM; all "DMA" reads index into it |
| `port/src/rom_symbols.ld` | generated: ROM offsets/segment offsets for ~1000 asset symbols (`port/tools/gen_rom_symbols.py`) |
| `port/src/asm_replacements/` | C ports of the 3 remaining game `.s` files (xorshift PRNGs, sin/cos→libm) |
| `port/src/preprocess.c` | load-time byteswaps (assets are big-endian); grep `PORT_PREPROCESS` for sites |
| `port/src/stubs/chraidata_stub.c` | stands in for `src/game/chraidata.c` (IDO-only macro tricks); M3 pre-expands the real file with IDO cpp |
| `port/fast3d/` | (M2) display-list renderer vendored from the PD port, retargeted to F3D numbering + Rare's `G_TRI4` |

## Rules of engagement with the N64 build

- The IDO byte-matching build must stay green: `make VERSION=US COMPARE=1`.
- All port-only code lives under `port/` (the N64 Makefile globs `src/`).
- Shared-source changes use `#ifdef TARGET_N64 / #else` only (IDO defines
  `TARGET_N64`, so its preprocessed output is unchanged), or are provably
  token-identical (e.g. removing IDO-ism `& ## NAME` pastes).

## Milestones

M1 boot scaffold (this) → M2 menus render (fast3d + asset byteswap) →
M3 Dam playable (bg/prop/setup preprocess) → M4 audio (C synthesis + Acmd
mixer) → M5 input/saves polish → M6 x86_64.
