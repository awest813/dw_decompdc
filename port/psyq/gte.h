/*
 * Software emulation of the PlayStation Geometry Transformation Engine
 * (coprocessor 2), used by the non-matching port build (DW_PORT).
 *
 * The model follows the psx-spx "GTE" chapter: 44-bit MAC accumulation
 * with overflow flags, IR/SZ/SX/SY/colour saturation, the UNR division
 * used by RTPS/RTPT, and the documented hardware quirks (the RTPS IR3
 * flag and the MVMVA far-colour bug). Game code reads FLAG bits and
 * projected coordinates, so results must be bit-exact, not approximate.
 *
 * This file deliberately uses only built-in C types so it can be built
 * for the PS1-header world (game code), the host (unit tests) and the
 * Dreamcast alike. It assumes int is 32 bits and long long is 64 bits.
 */
#ifndef DW_PORT_GTE_H
#define DW_PORT_GTE_H

typedef signed char gte_s8;
typedef unsigned char gte_u8;
typedef short gte_s16;
typedef unsigned short gte_u16;
typedef int gte_s32;
typedef unsigned int gte_u32;
typedef long long gte_s64;
typedef unsigned long long gte_u64;

typedef struct {
	/* Data registers (cop2r0..31) */
	gte_s16 v[3][3];	/* V0..V2: [n][0]=x, [1]=y, [2]=z */
	gte_u8 rgbc[4];		/* R, G, B, CODE */
	gte_u16 otz;
	gte_s16 ir[4];		/* IR0..IR3 */
	gte_s16 sx[3], sy[3];	/* screen XY FIFO */
	gte_u16 sz[4];		/* screen Z FIFO */
	gte_u8 rgb[3][4];	/* colour FIFO */
	gte_u32 res1;
	gte_s32 mac[4];		/* MAC0..MAC3 */
	gte_s32 lzcs;
	gte_s32 lzcr;

	/* Control registers (cop2r32..63) */
	gte_s16 rt[3][3];	/* rotation matrix */
	gte_s32 tr[3];		/* translation vector */
	gte_s16 llm[3][3];	/* light matrix */
	gte_s32 bk[3];		/* background colour */
	gte_s16 lcm[3][3];	/* light colour matrix */
	gte_s32 fc[3];		/* far colour */
	gte_s32 ofx, ofy;	/* screen offset (16.16) */
	gte_u16 h;		/* projection plane distance */
	gte_s16 dqa;		/* depth queueing coefficient */
	gte_s32 dqb;		/* depth queueing offset */
	gte_s16 zsf3, zsf4;	/* average Z scale factors */
	gte_u32 flag;
} DwGte;

extern DwGte dw_gte;

/* COP2 command encoding helpers (bit layout of the "cop2 imm25" word). */
#define GTE_SF		(1u << 19)
#define GTE_LM		(1u << 10)
#define GTE_MX(n)	((gte_u32)(n) << 17)	/* 0=RT 1=LLM 2=LCM 3=garbage */
#define GTE_V(n)	((gte_u32)(n) << 15)	/* 0..2=Vn 3=IR */
#define GTE_CV(n)	((gte_u32)(n) << 13)	/* 0=TR 1=BK 2=FC 3=none */

enum {
	GTE_OP_RTPS = 0x01, GTE_OP_NCLIP = 0x06, GTE_OP_OP = 0x0C,
	GTE_OP_DPCS = 0x10, GTE_OP_INTPL = 0x11, GTE_OP_MVMVA = 0x12,
	GTE_OP_NCDS = 0x13, GTE_OP_CDP = 0x14, GTE_OP_NCDT = 0x16,
	GTE_OP_NCCS = 0x1B, GTE_OP_CC = 0x1C, GTE_OP_NCS = 0x1E,
	GTE_OP_NCT = 0x20, GTE_OP_SQR = 0x28, GTE_OP_DCPL = 0x29,
	GTE_OP_DPCT = 0x2A, GTE_OP_AVSZ3 = 0x2D, GTE_OP_AVSZ4 = 0x2E,
	GTE_OP_RTPT = 0x30, GTE_OP_GPF = 0x3D, GTE_OP_GPL = 0x3E,
	GTE_OP_NCCT = 0x3F
};

/* Execute a COP2 command word (the low 25 bits of a "cop2" opcode). */
void dw_gte_cmd(gte_u32 cmd);

/* mfc2/mtc2 (data) and cfc2/ctc2 (control) register access. */
gte_u32 dw_gte_read_data(int reg);
void dw_gte_write_data(int reg, gte_u32 value);
gte_u32 dw_gte_read_ctrl(int reg);
void dw_gte_write_ctrl(int reg, gte_u32 value);

/* Reset all registers to zero. */
void dw_gte_reset(void);

#endif /* DW_PORT_GTE_H */
