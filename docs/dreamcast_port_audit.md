# Digimon World (PS1) → Sega Dreamcast: port feasibility audit and plan

Audit of this repository at commit `51c4b3e` (2026-09-23). Unless marked
**estimate**, every number here was measured from this repository. §A lists
the commands. Nothing has run on Dreamcast hardware yet. The SH-4 result in
§3.3 is a compile-only test.

---

## 1. Summary

| Question | Answer |
|---|---|
| Is a native Dreamcast port feasible? | **Yes.** The decomp is effectively complete, and the code is unusually friendly to a Dreamcast port. |
| Could the Dreamcast run it well? | **Yes, with plenty of headroom.** The game targets 20 FPS at 320×240 and emits at most about 2,000 primitives per frame. The Dreamcast has 8× the RAM and several times the CPU throughput. A locked 20 FPS at 640×480 is a realistic target. |
| How hard is it? | **About 7/10.** Almost all of the difficulty is in rebuilding the PS1 SDK (PsyQ) on Dreamcast hardware, mainly graphics and audio. The game code itself needs little work. |
| How long will it take? | **Estimate:** 8–15 person-months full-time for one experienced developer to reach release quality. Roughly 2–3 months to a booting title screen and 4–6 months to a playable overworld. Solo at about 10 hours a week, expect 2.5–5 years. |
| Biggest risks | Matching PS1 GPU behaviour on PowerVR2 (CLUT textures, subtractive blending, 2× colour modulation). Audio fidelity (libsnd/SPU → AICA). Undefined behaviour in the matching C once GCC compiles it. The **SHOP overlay has not been decompiled**. Legal and distribution limits. |

### Why this is easier than a typical PS1 port

1. **All game code is C.** There are about 2,770 functions and **0** `INCLUDE_ASM`. The main executable and 15 overlays rebuild byte-for-byte (`make compare`).
2. **The code already compiles for SH-4.** In a test build, 120 of 125 source files compiled to SH-4 object code with GCC 13 `-O2` and no source changes (§3.3).
3. **The data model matches the PS1.** Both CPUs are 32-bit, little-endian, with signed `char`. There is no floating-point code, and only one `long long` in the codebase. About 800 pointer↔integer casts are harmless on the Dreamcast. A 64-bit PC port would have to audit every one of them.
4. **PS1 primitive packets work nearly unchanged.** PsyQ's `P_TAG` stores 24-bit addresses. Dreamcast main RAM is exactly 16 MB (`0x8C000000–0x8CFFFFFF`), so a 24-bit offset reaches every RAM address. Only `nextPrim()` changes: it ORs in `0x8C000000` instead of `0x80000000`.
5. **The game never reads back the framebuffer and never renders to a texture.** All 15 `StoreImage` calls and all 6 `MoveImage` calls touch CLUT or texture areas of VRAM, never the display buffers. That removes the hardest PowerVR problem.
6. **All 3D models pass through one hook.** Every TMD model draw goes through the `GS_TMD_MAP` function table (`initializeGsTMDMap()` in `src/main/graphics.c`). A faster or more accurate native model renderer can replace it later without touching game logic.
7. **The performance bar is low.** The game runs at 20 FPS (`VSync(3)`) at 320×240 over pre-rendered backgrounds. Music is sequenced (SEQ + VAB), not streamed XA. Only the four FMVs stream audio.
8. **The data is small.** The disc holds 819 files, about 350 MB, of which 153 MB is FMV. It fits on a CD-R with room to spare.

---

## 2. Scope of the audit

- Every C file in `src/`, the headers in `include/`, the splat configs in `config/`, the `Makefile`, and the build and compare tools.
- A count of every PsyQ SDK call, every VRAM operation, primitive type, blend mode, and controller mask the game uses.
- The game's own file table (`src/main/file_table.c`), used to size the disc data.
- A compile of the whole codebase for SH-4 with GCC 13, a symbol-resolution pass over the resulting objects, and a pass with GCC's undefined-behaviour warnings enabled.

---

## 3. Findings

### 3.1 Decomp completeness

| Item | Value |
|---|---|
| C function definitions | 2,772 (main 1,118 · STD 457 · BTL 388 · VS 357 · FISH 111 · KAR 55 · TRN 45 · TRN2 40 · EVL 31 · DOOA 28 · EAB 24 · DOO2 18 · MURD 15 · DGET 13 · MOV 8 · ENDI 1 · 63 `static`) |
| Remaining `INCLUDE_ASM` / `INCLUDE_RODATA` | **0** |
| Lines of C (including large data tables) | 174,867 |
| Overlays built and byte-matched | 15: BTL, DGET, DOO2, DOOA, EAB, ENDI, EVL, FISH, KAR, MOV, MURD, STD, TRN, TRN2, VS (1.22 MB total) |
| **Overlay not decompiled** | **`SHOP_REL.BIN`**: 32,272 bytes, loaded at `0x80080800`. `src/main/utils.c` and `src/main/file_table.c` reference it, but it is absent from the `Makefile`, `config/`, and `tools/cmp_bins.sh`. The port needs at least a functional (non-matching) decomp of it before shops can work. |
| PsyQ SDK code | Kept as assembly, which is standard for decomps: 142.7 KB, about 36,500 instructions across 12 libraries. The port reimplements this layer instead of using it, but the assembly is useful as a reference for exact behaviour. |
| Embedded assets | Overlays begin with TIM/TMD data (for example, BTL's texture plus four models) that splat extracts from the user's disc. The port build must keep extracting this data from the user's disc; it cannot live in the repo. |
| Housekeeping | `src/main/swap.c` duplicates `swapByte`, `swapShort`, and `swapInt` from `utils.c` and is not built. These are the only duplicate global symbols in the codebase. |

### 3.2 PS1 SDK surface the game depends on

The SH-4 link analysis found **about 194 distinct PsyQ functions** called by the game, plus 766 BSS globals that are currently generated as assembly. Grouped by library:

| Library | Used | What Digimon World does with it | Dreamcast replacement | Difficulty |
|---|---|---|---|---|
| **libgs** | 61 | Double-buffered ordering tables, coordinate hierarchy (`GsCOORDINATE2`), lighting, `GsSortObject4` (42 call sites), 27 TMD packet builders (18 `GsTMDfast*` + 9 `GsTMDdiv*`), sprites, box fills | Reimplement in C | Medium-high |
| **libgte** + inline GTE | 29 functions + 11 inline op types (181 inline uses in 20 files) | Matrices, `RotTransPers*`, `RotNclip*`, `NormalColorCol*`, `ratan2`, `rsin`/`rcos`, raw `gte_rtps`/`gte_rtpt`/`gte_stsxy` | Software GTE in C that reproduces the fixed-point math and the FLAG register exactly (game code tests `flag & 0x80000000`) | Medium |
| **libgpu** | ~23 real functions (plus ~35 header macros that compile as-is) | `LoadImage` (62), `StoreImage` (15), `MoveImage` (6), `ClearImage` (13), `DrawSync` (134), TIM parsing, tpage/CLUT helpers | Shadow VRAM plus the PowerVR renderer | High (renderer) |
| **libsnd** | 21 | SEQ music, VAB banks (`.VHB`), key-on/off for SFX and voices, 240 Hz tick | Reimplement the sequencer | High (with the SPU) |
| **libspu** | 6 | Reverb setup, `SpuGetKeyStatus` | Software SPU or AICA | High (with libsnd) |
| **libcd** + **libds** | 16 + 7 | Sector-addressed reads (`CdControl(CdlSetloc)` + `CdRead`), `CdSearchFile`, FMV streaming (`St*`) | Raw-sector image on the Dreamcast disc | Low-medium |
| **libpress** | 6 | MDEC decoding for FMVs | Software MDEC | Medium |
| **libmcrd** | 11 | Save and load, 1 block | VMU | Low-medium |
| **libetc** | 5 | `PadRead`, `VSync`, `getScratchAddr`, `ResetCallback`, `PadInit` | KOS maple and vblank | Low |
| **libc** | ~15 | String functions, `rand`, `sprintf`, `malloc` | newlib plus a **PsyQ-compatible `rand`** (§3.4) | Low |

### 3.3 SH-4 compile test

- **Toolchain:** `sh4-linux-gnu-gcc` 13.3 with `-O2 -m4 -ml -ffreestanding -fno-strict-aliasing -fwrapv -std=gnu99 -nostdinc -DLANGUAGE_C` and the PsyQ headers.
- **Stubs:** A forced-include header turned the Metrowerks GTE intrinsics (`__evaluate`, `__asm_start`, `__I_lwc2`, and so on) into no-ops. This test proves the code compiles; the output does not run.
- **Toolchain differences:** KallistiOS uses the same GCC backend. Its usual `-m4-single-only` ABI makes no difference here because the game has no floating-point code.

| Result | Detail |
|---|---|
| **120 / 125 files compiled** | SH-4 output: 588 KB `.text`, 704 KB `.data`, 95 KB `.bss` |
| 4 files failed on matching hacks | Block-scope prototype redeclarations whose parameter types conflict with the real definition: `tamer.c` (`setEntityPosition`), `overworld_status_boxes.c` (`renderBoxBar`), `efe.c` (`renderParticleFlash`), `map.c` (`isFiveTileWidePathOpen`, `isLinearPathBlocked`, `isTileWithinScreenArea`). Each is a small fix behind `#ifdef`. |
| 1 file failed as expected | `_psstart.c` is the PS1 startup code, which the port replaces anyway. |
| Unresolved symbols | About 194 PsyQ functions (§3.2); 766 BSS/SBSS globals generated from `config/symbols.txt`; the 15 `*_START` overlay symbols; `abort`; 4 GCC helper routines |

### 3.4 Undefined behaviour that GCC will treat differently

A matching decomp only has to match what Metrowerks produced. GCC may legally compile the same undefined behaviour differently. Counts are from GCC 13 on SH-4:

| Warning | Count | Why it matters | Action |
|---|---|---|---|
| `-Waggressive-loop-optimizations` | **5** | **GCC will miscompile these.** Loops index past the declared size of `extern` arrays: `STD_D_8007C7B0` (`std_effect.c:1085, 1143`; `std_hud.c:1264, 1271`) and `BTL_D_80075CA0` (`battle_effect.c:6300`). GCC may cut the loop short or delete it. These are genuine out-of-bounds writes in the original game (`battle_effect.c` documents the BTL one as a known bug). | Don't resize the arrays: that would change behaviour. Keep the original data layout so the writes land where they did on the PS1, and build with `-fno-aggressive-loop-optimizations`. |
| `-Warray-bounds` | 94 | Mostly undersized array declarations and struct overreach (21 of them inside `libgpu.h` macros). This works on the PS1 only because of the fixed memory layout. | Fix the declarations and keep the original data order (§3.5). |
| `-Wreturn-type` | 110 | Non-`void` functions that fall off the end, some marked `// NOLINT undefined behavior intentional` (`_sin` in `math.c`, `readFile` in `file.c`). The MIPS caller received whatever was left in `v0`; on SH-4 it gets whatever is in `r0`. | Check every caller that uses the result. Most ignore it. |
| `-W(maybe-)uninitialized` | 69 | Same problem: Metrowerks' register allocation happened to supply the value. | Audit each site. |
| `rand()` range | 1 site, high impact | `random()` (`math.c`) returns `(limit * rand()) >> 15`, which assumes PsyQ's `RAND_MAX` of `0x7FFF`. newlib's is `0x7FFFFFFF`, so **every random roll in the game would break**. | Ship a PsyQ-compatible `rand`/`srand`. That also reproduces PS1 RNG sequences, which helps regression testing. |
| Mismatched `extern` types | 29 globals | Differences in signedness, array vs. pointer, or `PACKET[]` vs. `uint8_t[2][81920]`. Harmless with an identical memory layout. | Build with `-fno-strict-aliasing`. |
| Data alignment | 153 objects | *Found while bringing up the port.* The original build puts nearly every top-level variable on a 4-byte boundary. GCC uses the type's alignment, so 153 of the game's byte and short arrays landed on 1- or 2-byte boundaries. That changes which variables are neighbours, and any word access the original code makes through a cast faults on SH-4. **qemu does not trap misaligned accesses**, so only real hardware would show the fault. | Fixed in the port build: `-fdata-sections` plus `port/tools/fix_obj.py`, which raises every data section to 4-byte alignment. Now no game-defined object is misaligned. |
| `setjmp`/`longjmp` | 42 sites | The script interpreter uses these as normal early exits, with no stack tricks. Locals changed between `setjmp` and `longjmp` must be `volatile` under GCC `-O2`. | Audit `script_interp.c` around `setjmp_retry`. |

Recommended port build flags: `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-aggressive-loop-optimizations -fno-toplevel-reorder -fno-common -fno-zero-initialized-in-bss`. `port/Makefile` uses exactly these. Once the audit above is done, promote these warnings to errors.

### 3.5 Memory-map assumptions

- **Overlays.** Sixteen overlays share fixed load windows:

  | Address | Overlays |
  |---|---|
  | `0x80010000` | MOV |
  | `0x80052AE0` | BTL, STD, VS |
  | `0x80053800` | KAR |
  | `0x80060000` | EAB, ENDI, EVL |
  | `0x80070000` | DOO2, FISH |
  | `0x8007C000` | MURD |
  | `0x80080000` | DOOA |
  | `0x80080800` | DGET, SHOP |
  | `0x80088800` | TRN, TRN2 |

  Symbol names are prefixed per overlay, so **all overlays can be linked statically into one Dreamcast executable** (about 1.2 MB). One PS1 behaviour must be kept: reloading an overlay resets its `.data` and "bss" to the file image. The port should restore a pristine copy in `loadDynamicLibrary()` (`src/main/utils2.c:287`).
- **Hard-coded addresses.** 17 `#define`s point at fixed PS1 addresses (Appendix B). They cover data inside the BTL/STD overlay image, two ordering tables at `0x8008C000`, an MMD buffer at `0x80020000`, a TMD buffer at `0x80038000`, and `shop_START`. Replace them with symbols or static buffers. Check each one for deliberate aliasing, such as a buffer placed over an overlay that isn't loaded.
- **Scratchpad.** The 1 KB scratchpad at `0x1F800000` is reached through `getScratchAddr()`: the `EFE_*_SCRATCH` and `FISH_SCRATCH` structs and the `GsSortObject4` work area. Map it to a static 1 KB buffer, or to the SH-4's operand-cache RAM.
- **BSS and SBSS.** `tools/gen_bss.py` emits 766 globals as `.zero` blocks sized from address gaps. The port must keep them **in the original order, at the original relative addresses**, so that any out-of-bounds neighbour access (§3.4) behaves as it did on the PS1. Done: `port/tools/gen_glue.py` lays out the 712 that the game references in one block.

### 3.6 Graphics

- **Frame loop.** `gameLoop()` in `src/main/main.c` is the classic libgs double-buffered loop: `GsClearOt` → `tickObjects`/`renderObjects` → `DrawSync` → `VSync(3)` → `GsSwapDispBuff` → `GsDrawOt`. The display is 320×240 (`GsInitGraph(320, 240, …)`) at 20 FPS.
- **Primitive budget.** The packet buffer `GS_WORK_BASES` is 2 × 80 KB, which caps a frame at roughly 2,000 `POLY_FT4`-sized primitives.
- **Primitive types.** Sprites and polygons: `POLY_FT4` (386 references), `GsSPRITE` (64), `POLY_F4` (30), `POLY_GT4` (17), `POLY_FT3`, `POLY_GT3`, `POLY_G4`. Lines and fills: `LINE_F2/F3/F4`, `GsBOXF`. Draw-mode packets: `DR_TPAGE`, `DR_OFFSET`. Unused: `DR_AREA`, `DR_TWIN`, `DR_STP`, and the mask bit.
- **3D.** `GsSortObject4` with TMD models, dispatched through `GS_TMD_MAP` (18 "fast" and 9 "div" variants).
- **Maps.** Pre-rendered background tiles are streamed into VRAM as the camera scrolls (`updateTileRow`/`updateTileColumn` → `LoadImage`) and drawn as `POLY_FT4`s. Day/night transitions rewrite CLUTs.
- **Blend modes.** All four PS1 modes appear. Mode 0 is 50/50. Mode 1 is additive. Mode 2 is **subtractive**, used at about 20 sites (for example `getTPage(1, 2, 832, 256)`). Mode 3 adds ¼ of the source. `FADE_MODE` also sets the mode at runtime.
- **VRAM tricks.** Every `StoreImage` reads **CLUTs**, for example `(0,488,16,24)` and `(272,480,16,1)`. The game fades or tints them on the CPU and writes them back, for evolution, door transitions, and day/night in `map.c`. The `MoveImage` calls copy texture animation frames inside VRAM: eyes and mouths in `anim.c`, medals, fishing, and the STD HUD. **None touches the framebuffer.**
- **Gameplay reads GTE results.** Effect code tests the GTE FLAG error bit, and `worldPosToScreenPos` and `entityIsOffScreen` use projected coordinates. So the software GTE has to be bit-accurate, not just close.

### 3.7 Audio

- **libsnd:** `SsSeqOpen`/`SsSeqPlay`/`SsSeqStop` for background music, `SsVabOpenHeadSticky` and `SsVabTransBody` for sample banks (`.VHB` files), `SsUtKeyOnV`/`SsUtKeyOffV` for sound effects and Digimon voices, and `SsSetTickMode(SS_TICK240)`.
- **libspu:** reverb setup and `SpuGetKeyStatus`.
- **SPU RAM layout:** hard-coded in `src/main/sound.c` (`VHB_SOUNDBUFFER_START`).
- **No XA or CD-DA music.** XA audio exists only inside the FMV files.

### 3.8 Movies

- **Files:** the MOV overlay plays four STR files: `OP1` (3,637 frames), `OP2` (3,266), `ED2` (2,654), and `EDR` (3,447). That is 13,004 frames, about 14 minutes at a typical 15 FPS, and 153 MB.
- **Format:** 24-bit MDEC output, streamed with `CdRead2(... CdlModeRT ...)` and interleaved XA audio.
- **Disc image requirement:** the XA sectors are Mode 2 Form 2. The disc builder must read them from the user's raw BIN, because a plain ISO rip drops the audio.

### 3.9 Input, save, and timing

- **CD:** Reads are sector-addressed through `FILE_TABLE`, which holds a start sector and size for each of the 819 files, plus `CdSearchFile`. The asynchronous file queue polls `CdReadSync(1)` once per frame and uses no interrupt callbacks, so it is easy to emulate.
- **Controller:** `PadRead` feeds `POLLED_INPUT`. The masks in use are:
  - ×/○/△/□, the D-pad, and Start.
  - **L1** (`0x4`) and **R1** (`0x8`).
  - **Select** (`0x100`) at 2 sites (`main.c`, `std_hud.c`).
  - L2 (`0x1`) only in what looks like a leftover debug camera in `std_main.c`. R2 appears unused.
  - Scripts can also test arbitrary masks through `isKeyDown()`.
- **Memory card:** libmcrd with a one-block save (8 KB) in `main_menu.c`.
- **Timing:** `VSync(3)` gives 20 FPS. `VSync(-1)` counters drive fishing. libsnd ticks at 240 Hz.

---

## 4. Hardware comparison

| | PlayStation | Dreamcast | Notes |
|---|---|---|---|
| CPU | MIPS R3000A, 33.9 MHz, ~30 MIPS, no FPU | Hitachi SH-4, 200 MHz, 2-way superscalar, ~360 MIPS; FPU with 4×4 matrix instructions; 16 KB I-cache + 16 KB D-cache | **Estimate:** 6–10× the integer throughput |
| 3D math | GTE coprocessor (fixed-point) | No equivalent; emulate in C, optionally using the FPU | **Estimate:** about 50–150 SH-4 cycles per GTE op |
| Main RAM | 2 MB | 16 MB | 8× |
| Video RAM | 1 MB, shared by framebuffers, textures, and CLUTs | 8 MB (PowerVR2) | |
| Sound RAM | 512 KB | 2 MB | |
| GPU | 2D rasterizer, ~180K textured polygons/s (Sony's figure), affine texturing, 4- and 8-bit CLUT textures | PowerVR2 CLX2: tile-based deferred rendering, perspective-correct, bilinear filtering, 4- and 8-bit paletted textures (1,024-entry palette RAM), fixed-function, no shaders | |
| Blending | 0.5B+0.5F, B+F, **B−F**, B+F/4 | Source/destination factor blending; **no subtraction** | See §5.3 |
| Sound | SPU: 24 ADPCM voices, ADSR, hardware reverb | AICA: 64 voices (PCM16, PCM8, Yamaha ADPCM), slow ARM7, effects DSP | PS1 ADPCM is not a native AICA format |
| Video decode | MDEC hardware | None; the tile accelerator has a YUV→texture converter | Software MDEC |
| Disc | 2× CD, 300 KB/s | GD-ROM, about 1 GB. Homebrew boots from CD-R on MIL-CD-capable consoles, or from an optical drive emulator (GDEMU, MODE, …) | |
| Saves | Memory Card, 128 KB (15 blocks) | VMU, 128 KB (200 blocks) | One 8 KB PS1 block needs about 16 VMU blocks plus a header |

Historical precedent: **Bleemcast!** (2001) emulated PS1 games on the Dreamcast at 640×480. A native port has far more headroom than emulation.

---

## 5. Can the Dreamcast run it well?

### 5.1 CPU budget (estimate)

At 20 FPS, each frame has 50 ms, about 10 million SH-4 cycles.

| Work | Estimated cost per frame | Share of frame |
|---|---|---|
| Game logic, compiled natively | — | 10–20 % |
| GTE emulation: a few thousand vertices × ~100 cycles | 0.3–1 M cycles | 3–10 % |
| Ordering table → PowerVR translation: ≤2,000 primitives × 200–400 cycles | 0.4–0.8 M cycles | 4–8 % |
| Texture-cache conversion (bursty) | ~0.3 M cycles per 256×256 page; CLUT fades can re-convert visible tiles every frame unless PVR palettes are used | spikes |
| Software SPU, if chosen (§6, D9) | — | 10–20 % |
| **Total** | | **35–65 %** |

That is comfortable.

### 5.2 GPU budget

About 2,000 primitives per frame at 20 FPS is roughly 40,000 polygons per second. Commercial Dreamcast games sustain well over 1 million per second, so this uses **less than 5 % of the PowerVR2's capacity**. Fill rate at 640×480 is not a concern either, even with the extra overdraw from emulating subtractive blending.

### 5.3 Rendering fidelity is the real work

| PS1 behaviour | Dreamcast approach | Fidelity |
|---|---|---|
| CLUT textures (4/8-bit) stored in shared VRAM | Keep a 1 MB shadow VRAM in main RAM. Build a texture cache keyed by (tpage, CLUT, mode, rectangle) that converts to twiddled ARGB1555/ARGB4444 textures. Invalidate dirty rectangles on `LoadImage`/`MoveImage`/`ClearImage`. Put frequently changing palettes (map tiles, fades) in PVR palette banks: 64×16 or 4×256 entries per frame. | Exact |
| Ordering table (painter's algorithm) | Assign increasing depth in ordering-table traversal order. Opaque and punch-through polygons write depth; the translucent list runs in **pre-sort** (submission-order) mode. | Exact |
| Blend modes 0, 1, 3 | Native blend factors. For mode 3, pre-scale the vertex colour by ¼ and blend ONE/ONE. | Exact or near-exact |
| **Blend mode 2 (subtract)** | In the pre-sorted translucent list, draw: invert-destination quad → additive primitives → invert-destination quad. This gives `1 − ((1 − B) + F) = B − F` with the same clamping. Fallback: multiply by `DST × (1 − SRC)`. | Exact in theory (**needs validation**); the fallback is approximate |
| Per-texel STP semi-transparency (only texels with the STP bit blend; colour 0 is transparent) | Split each primitive into a punch-through pass for opaque texels and a translucent pass for STP texels | Exact |
| Colour modulation: `0x80` = 1.0, up to about 2× brightening | PowerVR modulation clamps at 1.0. Add a second additive pass for colours above `0x80`, or accept slight darkening. | **Medium risk.** Measure how often lit TMD models exceed `0x80`. |
| Lines, sprites, box fills | Draw as quads | Exact |
| Affine texture warping and vertex jitter | Reproduce by default (constant w). Optionally add PGXP-style precision through the `GS_TMD_MAP` hook. | Exact, or improved |
| 15-bit colour and dithering | PowerVR renders at 32 bits internally and outputs RGB565 | Slightly smoother (cosmetic) |
| Resolution | Render at 640×480 with 2× coordinates. Backgrounds are 320×240 art: offer bilinear or nearest filtering. The pre-rendered backgrounds are 4:3, so widescreen is not practical. | Sharper 3D |

### 5.4 Frame rate

Game logic advances once per rendered frame at `VSync(3)`. Running at 60 FPS would mean decoupling logic from rendering (interpolation) across more than 2,700 functions, which is not worth it for version 1.

- **Target:** locked 20 FPS with 60 Hz output, which runs at the same game speed as an NTSC PS1.
- **PAL consoles:** use 60 Hz mode. At 50 Hz the game would slow down, as it does on a PAL PS1.

### 5.5 Memory budget (estimate)

| Use | Size |
|---|---|
| KallistiOS plus the executable with every overlay linked in | ~3 MB |
| Pristine copies of overlay data, for the reload semantics | ~1.3 MB |
| PS1-equivalent heap, buffers, and BSS | ~2 MB |
| Shadow VRAM | 1 MB |
| Audio samples (software SPU) | 0.5 MB |
| File and streaming buffers | ~1 MB |
| **Total** | **about 8–9 MB of 16 MB** |

PowerVR VRAM: double-buffered 640×480 16-bit framebuffers take about 1.2 MB and tile-accelerator buffers 1–2 MB. That leaves 4–5 MB for the texture cache. Expanding an entirely 4-bit PS1 VRAM to 16-bit would take 4 MB, which is the worst case, so an LRU cache fits.

### 5.6 Storage and loading

- **Size:** about 350 MB of original files plus the executable fits on a CD-R.
- **Speed:** the Dreamcast drive reads CD-Rs faster than the PS1's 2× drive, and optical drive emulators load almost instantly.
- **Main risk:** seek latency on real GD-ROM and CD drives. The game's asynchronous file queue already hides most of it.

**Verdict: yes.** Performance is not the constraint. The effort goes into reproducing PS1 behaviour faithfully.

---

## 6. Recommended architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ Game code: src/, unchanged apart from #ifdef'd portability fixes │
├──────────────────────────────────────────────────────────────────┤
│ port/psyq: a PsyQ-compatible API                                 │
│   libgte (+ inline GTE)  libgs  libgpu  libcd/libds  libsnd      │
│   libspu  libpress  libmcrd  libetc  libc shims                  │
├───────────────────────────────┬──────────────────────────────────┤
│ port/backend/dc (KallistiOS)  │ port/backend/pc (SDL2, optional) │
│  PowerVR renderer, AICA       │  same interfaces, for fast       │
│  stream, maple pad, VMU,      │  debugging and for diffing       │
│  GD-ROM/CD file system        │  against an emulator             │
└───────────────────────────────┴──────────────────────────────────┘
```

| # | Decision | Rationale |
|---|---|---|
| D1 | Statically link every overlay. `loadDynamicLibrary()` restores the overlay's pristine data image instead of reading from disc. | 16 MB of RAM makes this trivial, and there are no symbol collisions. It preserves the "fresh `.data` on reload" behaviour. |
| D2 | Keep PS1 primitive packets and ordering tables byte-identical, and patch `nextPrim()` to `\| 0x8C000000`. | Every `setPolyFT4` and `addPrim` call site in the game works untouched. |
| D3 | Build the renderer in two tiers. **(a)** A generic walker that translates ordering-table packets to PowerVR. It handles everything and ships first. **(b)** Later, a native TMD path through `GS_TMD_MAP`, using the SH-4 FPU for transforms, for perspective-correct, sub-pixel-precise models. | Correctness first, then quality. |
| D4 | Shadow VRAM plus a texture cache, with PVR palettes for frequently changing CLUTs (§5.3). | The PowerVR has no shaders, so palette lookup must happen at upload time or in palette RAM. |
| D5 | Emulate subtraction with invert–add–invert; split STP texels into two passes (§5.3). | Exact PS1 blending on fixed-function hardware. |
| D6 | Write a software GTE in C, bit-accurate including FLAG and saturation, and swap `mwinline_n.h` for a C header. | Gameplay and effects read GTE results and flags. |
| D7 | Put the PS1 data track on the Dreamcast disc as a 2048-byte-sector image (plus raw Form 2 sectors for FMVs). `CdRead` becomes `seek` + `read` in a worker thread. | `FILE_TABLE` sector numbers keep working with no data conversion. |
| D8 | Reimplement libsnd's SEQ sequencer, driven at 240 Hz by a timer thread. | The game calls a small, well-documented subset. |
| D9 | **Audio output: a software SPU on the SH-4 (recommended first)**, mixing 24 ADPCM voices with ADSR, Gaussian interpolation, and reverb into a KallistiOS audio stream. | Near-exact sound, samples stay in their original format, and the CPU budget allows it (§5.1). Fallback: map voices onto AICA hardware channels, converting VAG to PCM or AICA ADPCM when a bank loads. That costs about 1–3 % CPU but gives approximate envelopes and no reverb unless someone programs the AICA DSP. |
| D10 | FMVs: software MDEC (VLC + IDCT) feeding the PowerVR YUV converter, plus XA-ADPCM decoding. | No offline transcoding, and the user still supplies only their own disc. |
| D11 | Emulate the PS1 memory-card file system on VMU files. Convert the save icon and optionally show the partner Digimon on the VMU screen. | Only 11 libmcrd calls, all in `main_menu.c`. |
| D12 | Develop the platform layer against a PC backend first, then bring up the Dreamcast backend. | Iteration is much faster, and PC builds can be compared against an emulator. |

---

## 7. Work plan and estimates

### 7.1 Milestones

| Milestone | Scope | Exit criteria | Estimated time from start (full-time) |
|---|---|---|---|
| **M0 Foundations** | Dreamcast build, static overlays, BSS emitted as C, address remaps, fixes for the 5 compile failures and the loop UB, PsyQ-compatible `rand`, stubs for all ~194 SDK functions | A Dreamcast ELF links, boots, and reaches the first `VSync` | 3–5 weeks |
| **M1 Boots** | CD raw-image reads, VSync/pad, shadow VRAM plus a VRAM debug viewer, GTE, minimal ordering-table translator (`POLY_FT4`, sprites) | Title screen and main menu render | +4–6 weeks (≈2–3 months total) |
| **M2 Overworld** | Full libgs (TMD, coordinate hierarchy, lighting), texture cache and CLUT animation, all blend modes, map-tile streaming | Walk around File City with your partner, day/night working, no sound | +6–10 weeks (≈4–6 months) |
| **M3 Audio** | libsnd sequencer, software SPU, VAB handling, reverb, SFX and voices | Music and SFX match the PS1 by ear | +5–9 weeks (runs in parallel with M2 on a team) |
| **M4 Full game** | Battles, VS, tournaments, training, fishing, KAR, evolution sequences, FMVs, VMU saves, **SHOP overlay decomp** | Complete playthrough possible | +6–10 weeks |
| **M5 Release** | Disc builder (user's BIN/CUE → CDI/GDI), controller mapping, VGA/NTSC/PAL60, performance tuning, full QA | Two clean full playthroughs; locked 20 FPS | +5–10 weeks |

### 7.2 Workstream estimates (person-weeks, one experienced developer, full-time)

| Workstream | Low | High | Difficulty |
|---|---:|---:|---|
| Build and foundations: KallistiOS build, overlay static link + data restore, BSS emitter, 17 address remaps, UB triage, libc shims | 3 | 5 | Medium |
| GTE: inline ops + 29 libgte functions, bit-accurate | 2 | 3 | Medium |
| libgs: 61 functions, including 27 TMD packet builders, coordinate hierarchy, lighting | 3 | 6 | Medium-high |
| libgpu API + shadow VRAM (Load/Store/Move/ClearImage, TIM) | 1 | 2 | Low-medium |
| **PowerVR renderer**: ordering-table walk, primitive translation, texture cache, CLUTs, blending, ordering, scaling | 6 | 10 | **High** |
| libetc, pad, VSync, timers, scratchpad | 0.5 | 1 | Low |
| libcd/libds + asynchronous file queue | 1 | 2 | Low-medium |
| **Audio**: libsnd sequencer + libspu + software SPU (or AICA) + VAB + reverb | 5 | 9 | **High** |
| FMVs: MDEC, STR/XA demux, A/V sync | 2 | 4 | Medium |
| Memory card → VMU | 1 | 2 | Low-medium |
| SHOP overlay functional decomp (32 KB) | 1 | 3 | Medium |
| Dreamcast front end: input mapping, video modes, disc builder | 2 | 4 | Medium |
| QA: full playthroughs, minigames, emulator-diff tooling, performance | 5 | 10 | Medium |
| **Total** | **32.5** | **61** | ≈ **8–15 person-months** |

**Assumptions:**

- The developer already knows PS1 internals (GPU packets, GTE, SPU) and Dreamcast/KallistiOS.
- QA is included. Enhancements are not: widescreen, 60 FPS, upscaled backgrounds.
- Someone new to either platform should add 30–50 %.
- Part-time at about 10 hours a week, multiply calendar time by about 4.
- A 2–3 person team working on renderer, audio, and platform in parallel could finish in about 5–8 calendar months.

### 7.3 Ways to go faster

- **Reuse an existing PsyQ reimplementation.** PsyCross ("Psy-X"), used by the REDRIVER2 PC port, already implements much of libgpu, libgte (with PGXP), libcd, libspu, and libetc on PC. Its GTE and packet code could be reused, but its OpenGL/shader renderer cannot run on the PowerVR, and it may not cover libgs or libsnd. Check its current coverage and license first.
- **Decompile the SDK itself.** The repo's own splat run produces PsyQ assembly for libgs, libsnd, and libspu. A functional decomp of those libraries gives exact reference behaviour. Other PS1 decomps have done this for parts of the SDK, though library versions differ.
- **Use the PC backend (D12)** to bisect behaviour against an emulator on the same save states.
- **Consult the psx-spx hardware documentation** for GTE, GPU, SPU, and MDEC details. It is documentation, so it carries no licence constraints.

---

## 8. Risks and other concerns

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| UB in the matching C behaves differently under GCC (§3.4) | High | Medium: random-looking bugs | Fix the 5 loop and 94 bounds sites; audit returns and uninitialized values; strict warnings; conservative flags; emulator-diff testing |
| GPU mismatches: subtractive blending, over-bright modulation, CLUT timing | Medium | Medium: visual glitches | Two-pass techniques (§5.3), a VRAM viewer, screenshot diffs against an emulator |
| Audio fidelity: envelopes, reverb, sequencer timing | Medium | Medium | Software SPU (D9); A/B comparison against emulator recordings |
| **SHOP overlay missing** | Certain | High: shops won't work | Add a splat config and functional decomp. Also worth contributing upstream as a matching decomp. |
| Texture-cache thrashing during CLUT fades | Medium | Low-medium: frame drops in transitions | PVR palette banks, CLUT hashing, per-frame conversion budget |
| Upstream churn: this repo is a fork of `jype0/dw_decomp`, which is still renaming symbols and types | High | Medium: merge conflicts | Put all port code in `port/`; keep `src/` changes small and `#ifdef`-guarded; keep `make compare` passing; upstream fixes that preserve the match |
| Only NTSC-U (`SLUS_010.32`) is supported | Certain | Low | Document it; PAL and JP would need their own decomps |
| Controller: the Dreamcast pad has no Select | Certain | Low | Map Select to a combination (for example, hold Y+Start), make mapping configurable; L1/R1 → analog triggers |
| Legal: the port is derived from decompiled code and needs Bandai Namco's assets | — | High | Never distribute game data. Build from the user's own disc (BIN/CUE) with a disc-builder tool, as the decomp already does. Source-only releases are the most common and safest practice for decomp ports. This is not legal advice. |
| Licence compatibility: the repo is MIT | Medium | Medium | KallistiOS is BSD-style and compatible. Mednafen/Beetle and PCSX code is GPL and would make the port GPL. DuckStation's current licence is non-commercial and no-derivatives, so don't use its code. psx-spx documentation is safe to implement from. |
| Real-hardware quirks: CD-R boot needs a MIL-CD-capable console or an optical drive emulator; GD-ROM seek times | Medium | Low | Target optical drive emulators and CDI first; test on real drives before release |
| Faults that emulators hide: qemu (and possibly Dreamcast emulators) accept misaligned accesses that SH-4 hardware rejects | Medium | Medium: crashes only on hardware | Keep the 4-byte data alignment (§3.4). Test on hardware early. Add an address-error handler in the KallistiOS build that reports the faulting PC. |
| Test surface: a 30–60 hour game with many subsystems and minigames | High | Medium | Deterministic RNG (§3.4); scripted input replays; save-state comparisons against an emulator; community beta |
| 50 Hz PAL output | Low | Low | Default to 60 Hz; 50 Hz slows the game as on a PAL PS1 |

---

## 9. First steps (next 2–4 weeks)

Progress is tracked in [`port/README.md`](../port/README.md). As of 2026-09-23, steps 1–5 are done, except that the Dreamcast link itself is still pending (step 1).

1. ~~Add `port/` and a separate Makefile so the matching build stays untouched~~ Done: `port/Makefile`. **Still to do:** install the KallistiOS toolchain (dc-chain, GCC 13 or later) and do the first real Dreamcast link. The cloud environment used so far blocks the GNU and sourceware mirrors that dc-chain downloads from, so the port was verified with an `sh4-linux-gnu` GCC and qemu instead. `make PLATFORM=dc` follows KallistiOS's own link line (checked against its `environ` scripts and `kos-cc`) but has never been run.
2. ~~Fix the 4 conflicting block-scope prototypes behind an `#ifdef`~~ Done, behind `DW_PORT`. The 5 aggressive-loop sites are handled by compiler flags and layout, not source edits (§3.4).
3. ~~Emit the BSS/SBSS globals in address order~~ Done: `port/tools/gen_glue.py` lays out the 712 that the game references at their original relative addresses.
4. ~~Replace `mwinline_n.h` with a software GTE~~ Done: `port/psyq/gte.c`, `port/include/mwinline_n.h`, and `port/psyq/libgte.c`, with unit tests on the host and on SH-4.
5. ~~Stub the remaining PsyQ functions, link, and reach `main()`~~ Done: 157 generated logging stubs. The game's `main()` runs on SH-4 (under qemu) through its whole init sequence and stops at the first disc read. The first `VSync` needs step 6.
6. Implement raw-image `CdRead`, `LoadImage` into shadow VRAM, and an on-screen VRAM viewer. Load the title TIMs.
7. Write a minimal ordering-table → PowerVR translator for `POLY_FT4` and sprites, and reach the title screen (M1).
8. Start the SHOP overlay: add a `config/shop.yaml` splat config and run m2c for a first functional pass.

---

## Appendix A: How the measurements were made

```sh
# Remaining assembly stubs (result: 0)
grep -r 'INCLUDE_ASM\|INCLUDE_RODATA' src | wc -l

# PsyQ usage: identifiers in src/ intersected with the declarations in
# external/psyq_headers/mw_lib41/include/<lib>.h (git submodule)

# SH-4 compile test (Ubuntu package gcc-sh4-linux-gnu, GCC 13.3).
# mwstub.h #defines __evaluate/__asm_start/__asm_end/__I_* as no-ops and
# __declspec(x) as empty.
for f in $(find src -name '*.c' ! -name swap.c); do
  sh4-linux-gnu-gcc -c -O2 -m4 -ml -ffreestanding -fno-strict-aliasing -fwrapv \
    -std=gnu99 -nostdinc -DLANGUAGE_C -include mwstub.h \
    -Iexternal/psyq_headers/mw_lib41/include -Iinclude -o out/$(basename $f).o $f
done
# Unresolved symbols = (nm U) - (nm defined), then split into PsyQ API /
# config/symbols*.txt BSS globals / other.

# UB inventory: the same compile without -w, plus
# -Wuninitialized -Wmaybe-uninitialized -Wreturn-type -Warray-bounds=1
# -Waggressive-loop-optimizations

# Disc size: sum of the size column in FILE_TABLE (src/main/file_table.c)
```

## Appendix B: Hard-coded PS1 addresses to remap

| File | Symbol | Address | Points at |
|---|---|---|---|
| `src/main/btl.c:21-25` | `BTL_FINISHER_TIM`, `_FINISHER_MODEL`, `_CONFUSION_MODEL`, `_STUN_MODEL`, `_BUFF_MODEL` | `0x80052AE0`, `0x80053800`, `0x80054838`, `0x80054D00`, `0x80055328` | Asset data at the start of the BTL/STD overlay image |
| `src/std/std_setup.c:24-28` | `STD_*`, the same five | same | same |
| `src/std/std_hud.c:26` | `STD_TMD_BUFFER` | `0x80038000` | Scratch buffer in the overlay arena |
| `src/main/evl.c:12`, `src/dooa/dooa.c:29` | `EVL_MMD_BUFFER`, `DOOA_MMD_BUFFER` | `0x80020000` | Scratch buffer in the overlay arena |
| `src/dooa/dooa.c:32-33`, `src/murd/murd.c:23` | `DOOA_ORDERING_TABLE_0/1`, `MURD_ORDERING_TABLE_0` | `0x8008C000`, `0x8008E000` | Ordering tables placed in free overlay memory |
| `src/main/utils.c:13` | `shop_START` | `0x80080800` | SHOP overlay load address |
