/*
 * Software GTE. See gte.h for scope and references.
 */
#include "gte.h"

DwGte dw_gte;

/* FLAG bit numbers */
enum {
	F_MAC0_NEG = 15, F_MAC0_POS = 16, F_DIV = 17, F_SZ = 18,
	F_B = 19, F_G = 20, F_R = 21, F_IR3 = 22, F_IR2 = 23, F_IR1 = 24,
	F_SX = 14, F_SY = 13, F_IR0 = 12
};

#define SETF(bit) (dw_gte.flag |= (1u << (bit)))

static const gte_s32 ZERO3[3] = { 0, 0, 0 };

/*
 * UNR reciprocal table used by the RTPS/RTPT division:
 * unr[i] = max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101), i = 0..0x100
 */
static gte_u8 unr_table[0x101];
static int unr_ready;

static void init_unr(void)
{
	int i;

	for (i = 0; i < 0x101; i++) {
		int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
		unr_table[i] = (gte_u8)(v < 0 ? 0 : v);
	}
	unr_ready = 1;
}

void dw_gte_reset(void)
{
	char *p = (char *)&dw_gte;
	unsigned i;

	for (i = 0; i < sizeof(dw_gte); i++) {
		p[i] = 0;
	}
	if (!unr_ready) {
		init_unr();
	}
}

/* Check a MAC1..3 intermediate against the 44-bit range and wrap it. */
static gte_s64 mac_chk(int i, gte_s64 v)
{
	if (v > 0x7FFFFFFFFFFLL) {
		SETF(31 - i);	/* 30, 29, 28 */
	} else if (v < -0x80000000000LL) {
		SETF(28 - i);	/* 27, 26, 25 */
	}
	return (gte_s64)((gte_u64)v << 20) >> 20;
}

static void set_mac(int i, gte_s64 v, int shift)
{
	v = mac_chk(i, v);
	dw_gte.mac[i] = (gte_s32)(v >> shift);
}

static gte_s64 mac0_chk(gte_s64 v)
{
	if (v > 0x7FFFFFFFLL) {
		SETF(F_MAC0_POS);
	} else if (v < -0x80000000LL) {
		SETF(F_MAC0_NEG);
	}
	dw_gte.mac[0] = (gte_s32)v;
	return v;
}

static void set_ir(int i, gte_s32 v, int lm)
{
	gte_s32 lo = lm ? 0 : -0x8000;

	if (v < lo) {
		v = lo;
		SETF(25 - i);	/* 24, 23, 22 */
	} else if (v > 0x7FFF) {
		v = 0x7FFF;
		SETF(25 - i);
	}
	dw_gte.ir[i] = (gte_s16)v;
}

static void set_ir_from_mac(int lm)
{
	set_ir(1, dw_gte.mac[1], lm);
	set_ir(2, dw_gte.mac[2], lm);
	set_ir(3, dw_gte.mac[3], lm);
}

static void set_ir0(gte_s32 v)
{
	if (v < 0) {
		v = 0;
		SETF(F_IR0);
	} else if (v > 0x1000) {
		v = 0x1000;
		SETF(F_IR0);
	}
	dw_gte.ir[0] = (gte_s16)v;
}

static gte_u16 sat_z(gte_s32 v)
{
	if (v < 0) {
		SETF(F_SZ);
		return 0;
	}
	if (v > 0xFFFF) {
		SETF(F_SZ);
		return 0xFFFF;
	}
	return (gte_u16)v;
}

static gte_s16 sat_xy(gte_s32 v, int bit)
{
	if (v < -0x400) {
		SETF(bit);
		return -0x400;
	}
	if (v > 0x3FF) {
		SETF(bit);
		return 0x3FF;
	}
	return (gte_s16)v;
}

static gte_u8 sat_col(gte_s32 v, int bit)
{
	if (v < 0) {
		SETF(bit);
		return 0;
	}
	if (v > 0xFF) {
		SETF(bit);
		return 0xFF;
	}
	return (gte_u8)v;
}

static void push_color(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		dw_gte.rgb[0][i] = dw_gte.rgb[1][i];
		dw_gte.rgb[1][i] = dw_gte.rgb[2][i];
	}
	dw_gte.rgb[2][0] = sat_col(dw_gte.mac[1] >> 4, F_R);
	dw_gte.rgb[2][1] = sat_col(dw_gte.mac[2] >> 4, F_G);
	dw_gte.rgb[2][2] = sat_col(dw_gte.mac[3] >> 4, F_B);
	dw_gte.rgb[2][3] = dw_gte.rgbc[3];
}

static void push_sz(gte_u16 z)
{
	dw_gte.sz[0] = dw_gte.sz[1];
	dw_gte.sz[1] = dw_gte.sz[2];
	dw_gte.sz[2] = dw_gte.sz[3];
	dw_gte.sz[3] = z;
}

static void push_sxy(gte_s16 x, gte_s16 y)
{
	dw_gte.sx[0] = dw_gte.sx[1];
	dw_gte.sy[0] = dw_gte.sy[1];
	dw_gte.sx[1] = dw_gte.sx[2];
	dw_gte.sy[1] = dw_gte.sy[2];
	dw_gte.sx[2] = x;
	dw_gte.sy[2] = y;
}

static int clz16(gte_u16 v)
{
	int n = 0;

	if (v == 0) {
		return 16;
	}
	while (!(v & 0x8000)) {
		v <<= 1;
		n++;
	}
	return n;
}

static int clz32(gte_u32 v)
{
	int n = 0;

	if (v == 0) {
		return 32;
	}
	while (!(v & 0x80000000u)) {
		v <<= 1;
		n++;
	}
	return n;
}

/* Hardware (UNR) division: returns ((H * 0x20000 / SZ3) + 1) / 2, max 0x1FFFF. */
static gte_u32 unr_divide(gte_u32 h, gte_u32 sz3)
{
	int shift;
	gte_u32 n, d, u;
	gte_u64 r;

	if (!unr_ready) {
		init_unr();
	}
	if (h >= sz3 * 2) {
		SETF(F_DIV);
		return 0x1FFFF;
	}
	shift = clz16((gte_u16)sz3);
	n = h << shift;
	d = sz3 << shift;
	u = unr_table[(d - 0x7FC0) >> 7] + 0x101;
	d = (0x2000080 - (d * u)) >> 8;
	d = (0x0000080 + (d * u)) >> 8;
	r = (((gte_u64)n * d) + 0x8000) >> 16;
	return r > 0x1FFFF ? 0x1FFFF : (gte_u32)r;
}

/* MAC[i] = (T * 0x1000 + M[row] . V) >> shift, with per-step overflow checks. */
static gte_s64 dot3(int i, gte_s32 t, const gte_s16 *m, const gte_s16 *v)
{
	gte_s64 a = mac_chk(i, (gte_s64)t * 0x1000 + (gte_s64)m[0] * v[0]);

	a = mac_chk(i, a + (gte_s64)m[1] * v[1]);
	return mac_chk(i, a + (gte_s64)m[2] * v[2]);
}

static void mat_vec(const gte_s16 m[3][3], const gte_s32 *t,
		    const gte_s16 *v, int shift, int lm)
{
	int i;

	for (i = 0; i < 3; i++) {
		dw_gte.mac[i + 1] = (gte_s32)(dot3(i + 1, t[i], m[i], v) >> shift);
	}
	set_ir_from_mac(lm);
}

/* One RTPS step. The depth cue (MAC0/IR0) is only computed for `last`. */
static void rtp(int n, int sf, int lm, int last)
{
	int shift = sf ? 12 : 0;
	const gte_s16 *v = dw_gte.v[n];
	gte_s64 x, y, z;
	gte_s32 zf;
	gte_u32 div;
	gte_s64 sx, sy;

	x = dot3(1, dw_gte.tr[0], dw_gte.rt[0], v);
	y = dot3(2, dw_gte.tr[1], dw_gte.rt[1], v);
	z = dot3(3, dw_gte.tr[2], dw_gte.rt[2], v);
	dw_gte.mac[1] = (gte_s32)(x >> shift);
	dw_gte.mac[2] = (gte_s32)(y >> shift);
	dw_gte.mac[3] = (gte_s32)(z >> shift);
	set_ir(1, dw_gte.mac[1], lm);
	set_ir(2, dw_gte.mac[2], lm);

	/*
	 * IR3 quirk: the value saturates on MAC3, but FLAG.22 is decided by
	 * (MAC3 unshifted >> 12) against -0x8000..0x7FFF.
	 */
	zf = (gte_s32)(z >> 12);
	if (zf < -0x8000 || zf > 0x7FFF) {
		SETF(F_IR3);
	}
	{
		gte_s32 lo = lm ? 0 : -0x8000;
		gte_s32 m3 = dw_gte.mac[3];

		dw_gte.ir[3] = (gte_s16)(m3 < lo ? lo : (m3 > 0x7FFF ? 0x7FFF : m3));
	}

	push_sz(sat_z(zf));
	div = unr_divide(dw_gte.h, dw_gte.sz[3]);

	sx = mac0_chk((gte_s64)div * dw_gte.ir[1] + dw_gte.ofx);
	sy = mac0_chk((gte_s64)div * dw_gte.ir[2] + dw_gte.ofy);
	push_sxy(sat_xy((gte_s32)(sx >> 16), F_SX), sat_xy((gte_s32)(sy >> 16), F_SY));

	if (last) {
		gte_s64 dq = mac0_chk((gte_s64)div * dw_gte.dqa + dw_gte.dqb);

		set_ir0((gte_s32)(dq >> 12));
	}
}

static void op_nclip(void)
{
	gte_s64 s = (gte_s64)dw_gte.sx[0] * dw_gte.sy[1] +
		    (gte_s64)dw_gte.sx[1] * dw_gte.sy[2] +
		    (gte_s64)dw_gte.sx[2] * dw_gte.sy[0] -
		    (gte_s64)dw_gte.sx[0] * dw_gte.sy[2] -
		    (gte_s64)dw_gte.sx[1] * dw_gte.sy[0] -
		    (gte_s64)dw_gte.sx[2] * dw_gte.sy[1];

	mac0_chk(s);
}

static void op_avsz(int four)
{
	gte_s64 s;

	if (four) {
		s = (gte_s64)dw_gte.zsf4 *
		    ((gte_s32)dw_gte.sz[0] + dw_gte.sz[1] + dw_gte.sz[2] + dw_gte.sz[3]);
	} else {
		s = (gte_s64)dw_gte.zsf3 *
		    ((gte_s32)dw_gte.sz[1] + dw_gte.sz[2] + dw_gte.sz[3]);
	}
	mac0_chk(s);
	dw_gte.otz = sat_z((gte_s32)(s >> 12));
}

static void op_mvmva(int sf, int mx, int vsel, int cv, int lm)
{
	int shift = sf ? 12 : 0;
	gte_s16 garbage[3][3];
	const gte_s16 (*m)[3];
	const gte_s32 *t;
	gte_s16 v[3];
	int i;

	switch (mx) {
	case 0:
		m = dw_gte.rt;
		break;
	case 1:
		m = dw_gte.llm;
		break;
	case 2:
		m = dw_gte.lcm;
		break;
	default:
		garbage[0][0] = (gte_s16)-(dw_gte.rgbc[0] << 4);
		garbage[0][1] = (gte_s16)(dw_gte.rgbc[0] << 4);
		garbage[0][2] = dw_gte.ir[0];
		garbage[1][0] = garbage[1][1] = garbage[1][2] = dw_gte.rt[0][2];
		garbage[2][0] = garbage[2][1] = garbage[2][2] = dw_gte.rt[1][1];
		m = (const gte_s16 (*)[3])garbage;
		break;
	}

	if (vsel == 3) {
		v[0] = dw_gte.ir[1];
		v[1] = dw_gte.ir[2];
		v[2] = dw_gte.ir[3];
	} else {
		v[0] = dw_gte.v[vsel][0];
		v[1] = dw_gte.v[vsel][1];
		v[2] = dw_gte.v[vsel][2];
	}

	switch (cv) {
	case 0:
		t = dw_gte.tr;
		break;
	case 1:
		t = dw_gte.bk;
		break;
	case 2:
		t = dw_gte.fc;
		break;
	default:
		t = ZERO3;
		break;
	}

	if (cv == 2) {
		/*
		 * Hardware bug: the far-colour term and the first column only
		 * affect the flags; the result is (M12*V2 + M13*V3) >> shift.
		 */
		for (i = 0; i < 3; i++) {
			gte_s64 a = mac_chk(i + 1, (gte_s64)t[i] * 0x1000 + (gte_s64)m[i][0] * v[0]);
			gte_s64 b;

			set_ir(i + 1, (gte_s32)(a >> shift), 0);
			b = mac_chk(i + 1, (gte_s64)m[i][1] * v[1]);
			b = mac_chk(i + 1, b + (gte_s64)m[i][2] * v[2]);
			dw_gte.mac[i + 1] = (gte_s32)(b >> shift);
		}
		set_ir_from_mac(lm);
		return;
	}

	mat_vec(m, t, v, shift, lm);
}

/* MAC = MAC + (FC - MAC) * IR0, then MAC >>= shift, IR = MAC. */
static void depth_cue(const gte_s64 pre[3], int shift, int lm)
{
	int i;

	for (i = 0; i < 3; i++) {
		gte_s64 d = mac_chk(i + 1, (gte_s64)dw_gte.fc[i] * 0x1000 - pre[i]);

		set_ir(i + 1, (gte_s32)(d >> shift), 0);
	}
	for (i = 0; i < 3; i++) {
		set_mac(i + 1, (gte_s64)dw_gte.ir[i + 1] * dw_gte.ir[0] + pre[i], shift);
	}
	set_ir_from_mac(lm);
}

/* [R*IR1, G*IR2, B*IR3] * 16 */
static void color_times_ir(const gte_u8 *col, gte_s64 out[3])
{
	int i;

	for (i = 0; i < 3; i++) {
		out[i] = mac_chk(i + 1, (gte_s64)col[i] * dw_gte.ir[i + 1] * 16);
	}
}

static void finish_no_cue(const gte_s64 pre[3], int shift, int lm)
{
	int i;

	for (i = 0; i < 3; i++) {
		dw_gte.mac[i + 1] = (gte_s32)(pre[i] >> shift);
	}
	set_ir_from_mac(lm);
}

/* BK + LCM * IR */
static void bk_lcm_step(int shift, int lm)
{
	gte_s16 ir[3];

	ir[0] = dw_gte.ir[1];
	ir[1] = dw_gte.ir[2];
	ir[2] = dw_gte.ir[3];
	mat_vec(dw_gte.lcm, dw_gte.bk, ir, shift, lm);
}

/* Normal colour: LLM * V, then BK + LCM * IR. */
static void light_steps(int n, int shift, int lm)
{
	mat_vec(dw_gte.llm, ZERO3, dw_gte.v[n], shift, lm);
	bk_lcm_step(shift, lm);
}

static void op_ncs(int n, int shift, int lm)
{
	light_steps(n, shift, lm);
	push_color();
}

static void op_nccs(int n, int shift, int lm)
{
	gte_s64 pre[3];

	light_steps(n, shift, lm);
	color_times_ir(dw_gte.rgbc, pre);
	finish_no_cue(pre, shift, lm);
	push_color();
}

static void op_ncds(int n, int shift, int lm)
{
	gte_s64 pre[3];

	light_steps(n, shift, lm);
	color_times_ir(dw_gte.rgbc, pre);
	depth_cue(pre, shift, lm);
	push_color();
}

static void op_dpcs(const gte_u8 *col, int shift, int lm)
{
	gte_s64 pre[3];
	int i;

	for (i = 0; i < 3; i++) {
		pre[i] = (gte_s64)col[i] * 0x10000;
	}
	depth_cue(pre, shift, lm);
	push_color();
}

void dw_gte_cmd(gte_u32 cmd)
{
	int sf = (cmd >> 19) & 1;
	int shift = sf ? 12 : 0;
	int lm = (cmd >> 10) & 1;
	gte_s64 pre[3];
	int i;

	dw_gte.flag = 0;

	switch (cmd & 0x3F) {
	case GTE_OP_RTPS:
		rtp(0, sf, lm, 1);
		break;
	case GTE_OP_RTPT:
		rtp(0, sf, lm, 0);
		rtp(1, sf, lm, 0);
		rtp(2, sf, lm, 1);
		break;
	case GTE_OP_NCLIP:
		op_nclip();
		break;
	case GTE_OP_AVSZ3:
		op_avsz(0);
		break;
	case GTE_OP_AVSZ4:
		op_avsz(1);
		break;
	case GTE_OP_MVMVA:
		op_mvmva(sf, (cmd >> 17) & 3, (cmd >> 15) & 3, (cmd >> 13) & 3, lm);
		break;
	case GTE_OP_OP: {
		gte_s32 d1 = dw_gte.rt[0][0], d2 = dw_gte.rt[1][1], d3 = dw_gte.rt[2][2];
		gte_s32 i1 = dw_gte.ir[1], i2 = dw_gte.ir[2], i3 = dw_gte.ir[3];

		set_mac(1, (gte_s64)i3 * d2 - (gte_s64)i2 * d3, shift);
		set_mac(2, (gte_s64)i1 * d3 - (gte_s64)i3 * d1, shift);
		set_mac(3, (gte_s64)i2 * d1 - (gte_s64)i1 * d2, shift);
		set_ir_from_mac(lm);
		break;
	}
	case GTE_OP_SQR:
		for (i = 1; i <= 3; i++) {
			set_mac(i, (gte_s64)dw_gte.ir[i] * dw_gte.ir[i], shift);
		}
		set_ir_from_mac(lm);
		break;
	case GTE_OP_NCS:
		op_ncs(0, shift, lm);
		break;
	case GTE_OP_NCT:
		op_ncs(0, shift, lm);
		op_ncs(1, shift, lm);
		op_ncs(2, shift, lm);
		break;
	case GTE_OP_NCCS:
		op_nccs(0, shift, lm);
		break;
	case GTE_OP_NCCT:
		op_nccs(0, shift, lm);
		op_nccs(1, shift, lm);
		op_nccs(2, shift, lm);
		break;
	case GTE_OP_NCDS:
		op_ncds(0, shift, lm);
		break;
	case GTE_OP_NCDT:
		op_ncds(0, shift, lm);
		op_ncds(1, shift, lm);
		op_ncds(2, shift, lm);
		break;
	case GTE_OP_CC:
		bk_lcm_step(shift, lm);
		color_times_ir(dw_gte.rgbc, pre);
		finish_no_cue(pre, shift, lm);
		push_color();
		break;
	case GTE_OP_CDP:
		bk_lcm_step(shift, lm);
		color_times_ir(dw_gte.rgbc, pre);
		depth_cue(pre, shift, lm);
		push_color();
		break;
	case GTE_OP_DPCS:
		op_dpcs(dw_gte.rgbc, shift, lm);
		break;
	case GTE_OP_DPCT:
		for (i = 0; i < 3; i++) {
			gte_u8 col[4];

			col[0] = dw_gte.rgb[0][0];
			col[1] = dw_gte.rgb[0][1];
			col[2] = dw_gte.rgb[0][2];
			col[3] = dw_gte.rgb[0][3];
			op_dpcs(col, shift, lm);
		}
		break;
	case GTE_OP_INTPL:
		for (i = 0; i < 3; i++) {
			pre[i] = (gte_s64)dw_gte.ir[i + 1] * 0x1000;
		}
		depth_cue(pre, shift, lm);
		push_color();
		break;
	case GTE_OP_DCPL:
		color_times_ir(dw_gte.rgbc, pre);
		depth_cue(pre, shift, lm);
		push_color();
		break;
	case GTE_OP_GPF:
		for (i = 1; i <= 3; i++) {
			set_mac(i, (gte_s64)dw_gte.ir[0] * dw_gte.ir[i], shift);
		}
		set_ir_from_mac(lm);
		push_color();
		break;
	case GTE_OP_GPL:
		for (i = 1; i <= 3; i++) {
			set_mac(i, (gte_s64)dw_gte.mac[i] * ((gte_s64)1 << shift) +
				       (gte_s64)dw_gte.ir[0] * dw_gte.ir[i], shift);
		}
		set_ir_from_mac(lm);
		push_color();
		break;
	default:
		/* Undefined opcodes: no effect beyond clearing FLAG. */
		break;
	}

	if (dw_gte.flag & 0x7F87E000u) {
		dw_gte.flag |= 0x80000000u;
	}
}

static gte_u32 pack16(gte_s16 lo, gte_s16 hi)
{
	return (gte_u32)(gte_u16)lo | ((gte_u32)(gte_u16)hi << 16);
}

static gte_u32 orgb(void)
{
	gte_u32 out = 0;
	int i;

	for (i = 0; i < 3; i++) {
		gte_s32 c = dw_gte.ir[i + 1] >> 7;

		c = c < 0 ? 0 : (c > 0x1F ? 0x1F : c);
		out |= (gte_u32)c << (i * 5);
	}
	return out;
}

gte_u32 dw_gte_read_data(int reg)
{
	switch (reg & 31) {
	case 0: return pack16(dw_gte.v[0][0], dw_gte.v[0][1]);
	case 1: return (gte_u32)(gte_s32)dw_gte.v[0][2];
	case 2: return pack16(dw_gte.v[1][0], dw_gte.v[1][1]);
	case 3: return (gte_u32)(gte_s32)dw_gte.v[1][2];
	case 4: return pack16(dw_gte.v[2][0], dw_gte.v[2][1]);
	case 5: return (gte_u32)(gte_s32)dw_gte.v[2][2];
	case 6:
		return (gte_u32)dw_gte.rgbc[0] | ((gte_u32)dw_gte.rgbc[1] << 8) |
		       ((gte_u32)dw_gte.rgbc[2] << 16) | ((gte_u32)dw_gte.rgbc[3] << 24);
	case 7: return dw_gte.otz;
	case 8: case 9: case 10: case 11:
		return (gte_u32)(gte_s32)dw_gte.ir[reg - 8];
	case 12: case 13: case 14:
		return pack16(dw_gte.sx[reg - 12], dw_gte.sy[reg - 12]);
	case 15: return pack16(dw_gte.sx[2], dw_gte.sy[2]);
	case 16: case 17: case 18: case 19:
		return dw_gte.sz[reg - 16];
	case 20: case 21: case 22: {
		const gte_u8 *c = dw_gte.rgb[reg - 20];

		return (gte_u32)c[0] | ((gte_u32)c[1] << 8) |
		       ((gte_u32)c[2] << 16) | ((gte_u32)c[3] << 24);
	}
	case 23: return dw_gte.res1;
	case 24: case 25: case 26: case 27:
		return (gte_u32)dw_gte.mac[reg - 24];
	case 28: case 29: return orgb();
	case 30: return (gte_u32)dw_gte.lzcs;
	default: return (gte_u32)dw_gte.lzcr;
	}
}

void dw_gte_write_data(int reg, gte_u32 value)
{
	switch (reg & 31) {
	case 0: case 2: case 4:
		dw_gte.v[reg / 2][0] = (gte_s16)value;
		dw_gte.v[reg / 2][1] = (gte_s16)(value >> 16);
		break;
	case 1: case 3: case 5:
		dw_gte.v[reg / 2][2] = (gte_s16)value;
		break;
	case 6:
		dw_gte.rgbc[0] = (gte_u8)value;
		dw_gte.rgbc[1] = (gte_u8)(value >> 8);
		dw_gte.rgbc[2] = (gte_u8)(value >> 16);
		dw_gte.rgbc[3] = (gte_u8)(value >> 24);
		break;
	case 7: dw_gte.otz = (gte_u16)value; break;
	case 8: case 9: case 10: case 11:
		dw_gte.ir[reg - 8] = (gte_s16)value;
		break;
	case 12: case 13: case 14:
		dw_gte.sx[reg - 12] = (gte_s16)value;
		dw_gte.sy[reg - 12] = (gte_s16)(value >> 16);
		break;
	case 15:
		push_sxy((gte_s16)value, (gte_s16)(value >> 16));
		break;
	case 16: case 17: case 18: case 19:
		dw_gte.sz[reg - 16] = (gte_u16)value;
		break;
	case 20: case 21: case 22:
		dw_gte.rgb[reg - 20][0] = (gte_u8)value;
		dw_gte.rgb[reg - 20][1] = (gte_u8)(value >> 8);
		dw_gte.rgb[reg - 20][2] = (gte_u8)(value >> 16);
		dw_gte.rgb[reg - 20][3] = (gte_u8)(value >> 24);
		break;
	case 23: dw_gte.res1 = value; break;
	case 24: case 25: case 26: case 27:
		dw_gte.mac[reg - 24] = (gte_s32)value;
		break;
	case 28:
		dw_gte.ir[1] = (gte_s16)((value & 0x1F) << 7);
		dw_gte.ir[2] = (gte_s16)(((value >> 5) & 0x1F) << 7);
		dw_gte.ir[3] = (gte_s16)(((value >> 10) & 0x1F) << 7);
		break;
	case 29: break;
	case 30:
		dw_gte.lzcs = (gte_s32)value;
		dw_gte.lzcr = clz32((gte_s32)value < 0 ? ~value : value);
		break;
	default: break;
	}
}

static void write_mat(gte_s16 m[3][3], int reg, gte_u32 value)
{
	gte_s16 *flat = &m[0][0];

	if (reg == 4) {
		flat[8] = (gte_s16)value;
		return;
	}
	flat[reg * 2] = (gte_s16)value;
	flat[reg * 2 + 1] = (gte_s16)(value >> 16);
}

static gte_u32 read_mat(gte_s16 m[3][3], int reg)
{
	gte_s16 *flat = &m[0][0];

	if (reg == 4) {
		return (gte_u32)(gte_s32)flat[8];
	}
	return pack16(flat[reg * 2], flat[reg * 2 + 1]);
}

gte_u32 dw_gte_read_ctrl(int reg)
{
	reg &= 31;
	if (reg <= 4) return read_mat(dw_gte.rt, reg);
	if (reg <= 7) return (gte_u32)dw_gte.tr[reg - 5];
	if (reg <= 12) return read_mat(dw_gte.llm, reg - 8);
	if (reg <= 15) return (gte_u32)dw_gte.bk[reg - 13];
	if (reg <= 20) return read_mat(dw_gte.lcm, reg - 16);
	if (reg <= 23) return (gte_u32)dw_gte.fc[reg - 21];
	switch (reg) {
	case 24: return (gte_u32)dw_gte.ofx;
	case 25: return (gte_u32)dw_gte.ofy;
	case 26: return (gte_u32)(gte_s32)(gte_s16)dw_gte.h;	/* reads sign-extended */
	case 27: return (gte_u32)(gte_s32)dw_gte.dqa;
	case 28: return (gte_u32)dw_gte.dqb;
	case 29: return (gte_u32)(gte_s32)dw_gte.zsf3;
	case 30: return (gte_u32)(gte_s32)dw_gte.zsf4;
	default: return dw_gte.flag;
	}
}

void dw_gte_write_ctrl(int reg, gte_u32 value)
{
	reg &= 31;
	if (reg <= 4) {
		write_mat(dw_gte.rt, reg, value);
	} else if (reg <= 7) {
		dw_gte.tr[reg - 5] = (gte_s32)value;
	} else if (reg <= 12) {
		write_mat(dw_gte.llm, reg - 8, value);
	} else if (reg <= 15) {
		dw_gte.bk[reg - 13] = (gte_s32)value;
	} else if (reg <= 20) {
		write_mat(dw_gte.lcm, reg - 16, value);
	} else if (reg <= 23) {
		dw_gte.fc[reg - 21] = (gte_s32)value;
	} else {
		switch (reg) {
		case 24: dw_gte.ofx = (gte_s32)value; break;
		case 25: dw_gte.ofy = (gte_s32)value; break;
		case 26: dw_gte.h = (gte_u16)value; break;
		case 27: dw_gte.dqa = (gte_s16)value; break;
		case 28: dw_gte.dqb = (gte_s32)value; break;
		case 29: dw_gte.zsf3 = (gte_s16)value; break;
		case 30: dw_gte.zsf4 = (gte_s16)value; break;
		default:
			dw_gte.flag = value & 0x7FFFF000u;
			if (dw_gte.flag & 0x7F87E000u) {
				dw_gte.flag |= 0x80000000u;
			}
			break;
		}
	}
}
