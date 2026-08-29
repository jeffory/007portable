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
mouse): mouse deltas are injected directly into the view angles
(1 px = `sensitivity` × 0.05°, no virtual-stick quantization or turn
speed cap; `PORT_DIRECT_AIM=0` falls back to the stick translation, and
aim mode always uses the stick path so the crosshair tracks the mouse).
WASD moves, LMB fires, RMB or Left Shift aims, E uses / R reloads
(both are GE's B action — one button covers reload, doors and switches;
C also works), Space also fires, X/F = A, Enter = Start, Q = L shoulder,
arrows = analog stick, IJKL = raw C buttons. In the frontend menus the
mouse moves the hand cursor directly and left-click selects. The scrollwheel cycles weapons (up = next,
down = previous; `]`/`[` do the same) and number keys 1–9/0 direct-select
weapon slots 1–10 — a slot is the Nth stop of the native A-button cycle
(held weapons in ascending weapon-id order), and an empty slot does
nothing. With mouselook off the game runs its stock
1.1 control style and WASD drives the analog stick (tank-style, like the
N64). Gamepads work via SDL_GameController with hotplug.

**The control style follows whichever device you last used.** Mouselook
needs style 1.2 Solitaire — the stick is the mouse's look axis, so the C
buttons have to move — and the port forces 1.2 while the mouse is the
active device. The moment you touch a gamepad it hands the style back to
the game's own setting (1.1 Honey unless you changed it in the options),
where the left stick moves and turns like the N64 and the right stick
(the C buttons) looks and sidesteps; moving the mouse takes it back.
Without that handover a pad player's forward push landed on the *look*
axis and walking pitched the view at the sky.

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

**Fog and coverage alpha.** The RSP overwrites vertex shade alpha with
the fog factor when `G_FOG` is set, and GE's BG room geometry relies on
that: its alpha combiner is `(COMBINED*SHADE)`, and the result reaches
the blender only through `ALPHA_CVG_SEL`, which substitutes coverage for
it. fast3d had no coverage model, so the fog factor (0 across all but
the last ~1% of clip depth, `gSPFogPosition(995,1000)`) went straight
into GL blending and dissolved the whole world — the port worked around
it by keeping the untouched vertex alpha. fast3d now models
`ALPHA_CVG_SEL` (a shader option that forces a covered pixel opaque
after alpha compare, which still sees the combiner value as on
hardware), so the RSP overwrite is accurate and on by default;
`PORT_RSP_FOG_ALPHA=0` restores the old vertex-alpha fallback. The two
paths render identically across dam/surface/facility/frigate — no pass
in six levels feeds shade alpha into a fogged translucent draw — so the
change is faithfulness, not a visible fix.

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
chases. (The aim sight was re-measured at window scales 1x-3x and draws
1 texel per game pixel in its 32x32 rect, exactly as the RDP would — the
old "oversized rings" note predates the texture-swizzle fixes and no
longer reproduces.)

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

## Testing (golden suite, deterministic)

`port/tests/run_goldens.sh` runs the golden regression suite headlessly
(Xvfb + llvmpipe; needs the ROM) in ~30s. Everything runs under
**`PORT_DETERMINISTIC=1`**: a virtual clock advances exactly one retrace
period per scheduler pump, so runs are bit-reproducible and execute flat
out (no real-time pacing). Every comparison is exact: asset-preprocess
CRCs (`PORT_CRC_TRACE=<file|1>`), golden frames (`PORT_FRAME_DUMP=<dir>`
+ `PORT_FRAME_DUMP_AT=n[,n...]` + `PORT_FRAME_DUMP_EXIT=1`), the boot
music PCM byte-for-byte, and gameplay state hashes
(`PORT_STATE_HASH=<tick>[,tick...]` prints a CRC of the RNG seed, Bond's
position/view and every chr's action+position; `PORT_STATE_HASH_EXIT=1`
exits after the last one).

`PORT_POS_TRACE=<n>` prints Bond's state every n ticks — control style,
vertical look angle, position, feet height, the ground height under him
and his vertical velocity. It is the quickest way to tell a movement bug
apart from a *look* bug: a walk that pitches `verta` instead of changing
`pos` is a control-style problem, a `stanH` that jumps and decays is the
floor.

Input sessions can be recorded and replayed at the pad level:
`PORT_INPUT_RECORD=<file>` captures the final pad values of every poll
(all sources merged — keyboard, `PORT_AUTOSTART`, gamepad),
`PORT_INPUT_REPLAY=<file>` feeds them back in place of all input. Under
the deterministic clock a replayed session reproduces the exact same
state hashes. The stream is 4 controllers x `{u16be button; s8 x; s8 y}`
per poll, so a session can also be synthesized outright — which is how
gameplay bugs get a deterministic repro without a human at the keyboard.
`PORT_INPUT_TRACE=1` logs the replayed pads too, so a replay the game
ignores is distinguishable from one that never loaded.

`PORT_TEST_VPAD=1` attaches an SDL *virtual* gamepad a couple of seconds
in, drives a canned script through it (START into the stage, turn, fire,
walk on the right stick, then walk on the left stick) and unplugs it
again — the hotplug paths and the pad control style get exercised
without any hardware.

Re-pin with `run_goldens.sh capture` — only after intentionally changing
output. Frame goldens are tied to this machine's Mesa/llvmpipe
(`goldens/CAPTURED_WITH.txt`); CRC/PCM/state-hash goldens are
renderer-independent. Run the suite before every push.

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
