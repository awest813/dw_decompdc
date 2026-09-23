#ifndef DW_PSX_ADDR_H
#define DW_PSX_ADDR_H

/*
 * Fixed PS1 addresses used by game code: buffers in the overlay load area
 * and overlay data referenced by address. The matching build uses the raw
 * address. The port (DW_PORT) maps it into its copy of the overlay area,
 * which covers 0x80010000..0x80090000.
 */
#ifdef DW_PORT
extern unsigned char dw_psx_arena[];
#define PSX_ADDR(addr) (dw_psx_arena + ((addr) - 0x80010000))

/*
 * Reloading an overlay on the PS1 resets its data to the file image. The
 * port links every overlay statically, so it restores a snapshot instead.
 */
void dw_overlay_reset(int lib);
#else
/* Bare, so the matching build sees exactly the original tokens. */
#define PSX_ADDR(addr) addr
#endif

#endif /* DW_PSX_ADDR_H */
