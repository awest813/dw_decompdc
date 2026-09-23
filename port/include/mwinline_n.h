/*
 * Port replacement for PsyQ's DMPSX inline GTE macros (include/mwinline_n.h).
 *
 * The original macros emit Metrowerks inline-assembly intrinsics. These
 * versions perform the same register transfers and commands on the
 * software GTE. Only the macros the game uses (plus a few close
 * relatives) are provided; any other macro fails to compile, which is
 * intentional.
 *
 * Pointer arguments are accessed as 32-bit words, like lwc2/swc2. MIPS
 * requires the same alignment, so the game's pointers are already
 * suitably aligned.
 */
#ifndef DW_PORT_MWINLINE_N_H
#define DW_PORT_MWINLINE_N_H

#include "../psyq/gte.h"

static inline void dw_gte_ldv(int n, const void *p)
{
	const gte_u32 *w = (const gte_u32 *)p;

	dw_gte_write_data(n * 2, w[0]);
	dw_gte_write_data(n * 2 + 1, w[1]);
}

static inline void dw_gte_st(int reg, void *p)
{
	*(gte_u32 *)p = dw_gte_read_data(reg);
}

static inline void dw_gte_set_rot(const void *m)
{
	const gte_u32 *w = (const gte_u32 *)m;
	int i;

	for (i = 0; i < 5; i++) {
		dw_gte_write_ctrl(i, w[i]);
	}
}

static inline void dw_gte_set_trans(const void *m)
{
	const gte_u32 *w = (const gte_u32 *)m;

	dw_gte_write_ctrl(5, w[5]);
	dw_gte_write_ctrl(6, w[6]);
	dw_gte_write_ctrl(7, w[7]);
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
/* mfc2 SZ3; sra 2; sw */
#define gte_stszotz(r0)		(*(gte_s32 *)(r0) = (gte_s32)dw_gte_read_data(19) >> 2)

#endif /* DW_PORT_MWINLINE_N_H */
