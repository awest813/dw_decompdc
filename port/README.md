# Digimon World port layer

A non-matching build of the decompiled game for new platforms, starting
with the Sega Dreamcast. The plan, estimates, and risks are in
[`docs/dreamcast_port_audit.md`](../docs/dreamcast_port_audit.md).

The matching PS1 build (top-level `Makefile`) is not affected. Game
sources only change behind `#ifdef DW_PORT`, or through macros that expand
to the original tokens when `DW_PORT` is not defined.

## Status (milestone M0: foundations)

| Area | State |
|---|---|
| Game code | All 124 game C files and all 15 decompiled overlays compile for SH-4 and link into one static executable. |
| Runs on SH-4 | Under qemu, the game's own `main()` runs its full init sequence (heap, sound, graphics, 3D, pad, CD, memory card, first texture upload) and stops at the first disc read, because the CD functions are still stubs. |
| GTE | Software GTE: every COP2 command, register-accurate FLAG and saturation, UNR division. The game's inline GTE macros and 29 libgte functions run on it. Unit-tested on the host and on SH-4. |
| Other PsyQ SDK functions | 157 functions are generated stubs that log their first call and return 0 (`build/port-*/glue/report.txt` lists them). |
| Overlays | Linked statically. Loading an overlay restores its data from a startup snapshot, as reloading from disc does on the PS1. |
| Data layout | Every game variable is 4-byte aligned, as in the original build. Game BSS globals sit at their original relative addresses. |
| Dreamcast (KallistiOS) | `make PLATFORM=dc` is written but has never been run: the cloud environment used so far blocks the GNU/sourceware mirrors that the KallistiOS toolchain builder downloads from. |
| SHOP overlay | Not decompiled yet. Loading it logs a message and does nothing. |

## Building

Requirements for the checks below:

- `gcc-sh4-linux-gnu`: SH-4 GCC. It is Linux-targeted, but it generates the same SH-4 code as the Dreamcast toolchain.
- `qemu-user-static`: provides `qemu-sh4-static`, to run SH-4 binaries.
- Python 3 with PyYAML (`requirements.txt`).
- The `external/psyq_headers` submodule: `git submodule update --init external/psyq_headers`.

No game data is needed to build.

```sh
make -C port            # compile + link build/port-check/dw.elf (SH-4)
make -C port run        # run it under qemu for 10 s and show the SDK call log
make -C port test       # unit tests: host, plus SH-4 through qemu
make -C port V=1        # show full commands
```

Dreamcast, once a KallistiOS environment is available (untested):

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C port PLATFORM=dc
```

## Layout

```
port/
  Makefile           build rules for PLATFORM=check (default) and PLATFORM=dc
  include/           headers searched before the PsyQ ones
    mwinline_n.h       DMPSX inline GTE macros → software GTE
    libetc.h           getScratchAddr() → dw_scratchpad
    setjmp.h           jmp_buf that fits the game's 48-byte SCRIPT_JMP_BUF
    __rts_info_t__.h   Metrowerks linker symbols used by initializeHeap()
  psyq/              reimplemented PsyQ SDK
    gte.c, gte.h       GTE (COP2) emulation
    gte_tables.c       rsin/ratan tables
    libgte.c           libgte API
    libc.c             PsyQ-compatible rand()
  platform/
    port_core.c        scratchpad, overlay area, overlay snapshots,
                       setjmp support, stub logging, main()
  tools/
    fix_obj.py         per-object data alignment and overlay data grouping
    gen_glue.py        BSS layout + stubs for still-unresolved symbols
  tests/             unit tests
include/dw/psx_addr.h  PSX_ADDR(): fixed PS1 addresses → port overlay area
```

## How the game code is adapted

- **Compiler flags.** `port/Makefile` builds the game with flags that keep the original semantics where GCC would otherwise exploit undefined behaviour: `-fno-strict-aliasing`, `-fwrapv`, `-fno-aggressive-loop-optimizations`, `-fno-toplevel-reorder`, `-fno-zero-initialized-in-bss`, and others (audit §3.4).
- **Header overrides.** `port/include` comes first on the include path, ahead of the PsyQ headers. Wrappers such as `libetc.h` use `#include_next` and then patch individual macros.
- **Fixed addresses.** `dw_psx_arena` is a copy of the PS1 overlay load area (`0x80010000`–`0x80090000`). Overlay files are still read into it, because the loader's `*_START` symbols point there. The 17 hard-coded buffer and asset addresses (audit Appendix B) map into it through `PSX_ADDR()`.
- **Data alignment.** The original build puts every top-level variable on a 4-byte boundary. GCC would align byte and short arrays to 1 or 2, which moves neighbours and turns the original code's word accesses into faults on SH-4. Game code is therefore compiled with `-fdata-sections`, and `tools/fix_obj.py` raises every data section to 4-byte alignment.
- **Overlays.** Their code and data are linked statically. `tools/fix_obj.py` renames each overlay object's data sections to `dw_ovl_<name>_data`/`_bss`, so the linker groups them per overlay. `loadDynamicLibrary()` calls `dw_overlay_reset()` to restore that overlay's startup data.
- **BSS globals.** In the matching build, 712 game globals only exist in `config/symbols.txt`. `gen_glue.py` lays them out in one block at their original relative addresses, so out-of-bounds accesses (which the original game has) land on the same neighbours.
- **Entry point.** The game's `main()` is compiled as `dw_game_main()`. `port/platform/port_core.c` owns `main()`, snapshots the overlays, then calls it.
- **Four prototype hacks.** Block-scope prototypes whose parameter types conflict with the real definitions are hidden from the port build (`tamer.c`, `overworld_status_boxes.c`, `efe.c`, `map.c`). Every caller already passes values of the narrower type, so behaviour is unchanged.

## Testing caveat

qemu accepts misaligned memory accesses that SH-4 hardware rejects with an address error. A clean qemu run therefore does not prove the port is free of them. Test on real hardware early.

## Accuracy notes

Code marked `VERIFY` follows documented behaviour but has not been compared with the original PsyQ code. That comparison needs the disassembly produced by `make regenerate`, which requires the game disc. Specifically:

- The `libgte` matrix and vector helpers.
- `InitGeom()` defaults.
- The trig tables.
- The initial `rand()` seed.

## Next steps (milestone M1: title screen)

1. First Dreamcast link with a real KallistiOS toolchain.
2. CD: serve `CdSearchFile`/`CdControl`/`CdRead` from a raw 2048-byte-sector image of the data track, so `FILE_TABLE` sector numbers keep working.
3. `VSync`, `PadRead`, and timers on KallistiOS.
4. Shadow VRAM (1024×512×16-bit) for `LoadImage`/`StoreImage`/`MoveImage`/`ClearImage`, plus a VRAM viewer.
5. libgs: ordering tables, `GsSortSprite`/`GsSortBoxFill`, TMD sorting.
6. An ordering-table → PowerVR translator. Primitive links hold 24-bit addresses, which works on the Dreamcast because its RAM is exactly 16 MB at `0x8C000000`.
