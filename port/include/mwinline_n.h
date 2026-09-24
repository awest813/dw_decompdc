/*
 * Port replacement for PsyQ's DMPSX inline GTE macros (include/mwinline_n.h).
 *
 * The original macros emit Metrowerks inline-assembly intrinsics. These
 * versions perform the same register transfers and commands on the
 * software GTE. Only the macros the game uses (plus a few close
 * relatives) are provided; any other macro fails to compile, which is
 * intentional.
 *
 * Memory is accessed in 16-bit halves. lwc2/swc2 need 4-byte alignment on
 * MIPS, but GCC only guarantees 2 for SVECTOR/DVECTOR objects, and SH-4
 * faults on misaligned 32-bit accesses (qemu does not).
 */
#ifndef DW_PORT_MWINLINE_N_H
#define DW_PORT_MWINLINE_N_H

#include "../psyq/gte.h"

/* Little-endian 32-bit word from two 16-bit halves. */
static inline gte_u32 dw_gte_load32(const void *p, int word)
{
	const gte_u16 *h = (const gte_u16 *)p + word * 2;

	return (gte_u32)h[0] | ((gte_u32)h[1] << 16);
}

static inline void dw_gte_ldv(int n, const void *p)
{
	dw_gte_write_data(n * 2, dw_gte_load32(p, 0));
	dw_gte_write_data(n * 2 + 1, dw_gte_load32(p, 1));
}

static inline void dw_gte_st(int reg, void *p)
{
	gte_u32 v = dw_gte_read_data(reg);
	gte_u16 *h = (gte_u16 *)p;

	h[0] = (gte_u16)v;
	h[1] = (gte_u16)(v >> 16);
}

/* MATRIX: short m[3][3] at offset 0, long t[3] at offset 20. */
static inline void dw_gte_set_rot(const void *m)
{
	int i;

	for (i = 0; i < 5; i++) {
		dw_gte_write_ctrl(i, dw_gte_load32(m, i));
	}
}

static inline void dw_gte_set_trans(const void *m)
{
	dw_gte_write_ctrl(5, dw_gte_load32(m, 5));
	dw_gte_write_ctrl(6, dw_gte_load32(m, 6));
	dw_gte_write_ctrl(7, dw_gte_load32(m, 7));
}

/* Loads */
#define gte_ldv0(r0)		dw_gte_ldv(0, (r0))
#define gte_ldv1(r0)		dw_gte_ldv(1, (r0))
#define gte_ldv2(r0)		dw_gte_ldv(2, (r0))
#define gte_ldv3(r0, r1, r2)	do { dw_gte_ldv(0, (r0)); dw_gte_ldv(1, (r1)); dw_gte_ldv(2, (r2)); } while (0)
#define gte_ldlzc(r0)		dw_gte_write_data(30, (gte_u32)(long)(r0))
#define gte_SetRotMatrix(r0)	dw_gte_set_rot((r0))
#define gte_SetTransMatrix(r0)	dw_gte_set_trans((r0))

/* Commands (same encodings as the DMPSX macros) */
#define gte_rtps()		dw_gte_cmd(0x0180001)
#define gte_rtpt()		dw_gte_cmd(0x0280030)
#define gte_nclip()		dw_gte_cmd(0x1400006)
#define gte_avsz3()		dw_gte_cmd(0x158002d)
#define gte_avsz4()		dw_gte_cmd(0x168002e)

/* Stores */
#define gte_stsxy(r0)		dw_gte_st(14, (r0))
#define gte_stsxy0(r0)		dw_gte_st(12, (r0))
#define gte_stsxy1(r0)		dw_gte_st(13, (r0))
#define gte_stsxy2(r0)		dw_gte_st(14, (r0))
#define gte_stsxy3(r0, r1, r2)	do { dw_gte_st(12, (r0)); dw_gte_st(13, (r1)); dw_gte_st(14, (r2)); } while (0)
#define gte_stsz(r0)		dw_gte_st(19, (r0))
#define gte_stotz(r0)		dw_gte_st(7, (r0))
#define gte_stopz(r0)		dw_gte_st(24, (r0))
#define gte_stlzc(r0)		dw_gte_st(31, (r0))
/* mfc2 SZ3; sra 2; sw (SZ3 is unsigned 16-bit, so this is SZ3 / 4) */
#define gte_stszotz(r0)		(*(gte_s32 *)(r0) = (gte_s32)(dw_gte_read_data(19) >> 2))

#endif /* DW_PORT_MWINLINE_N_H */
