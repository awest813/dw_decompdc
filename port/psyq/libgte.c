/*
 * PsyQ libgte for the port, implemented on the software GTE.
 *
 * Functions that drive the GTE on the PS1 (loading RT, running MVMVA,
 * RTPS, ...) do the same here, so GTE register side effects match.
 * Items marked VERIFY follow the documented behaviour but have not been
 * checked against the libgte disassembly (asm/main/psyq/libgte, produced
 * by `make regenerate` from the user's disc).
 */
#include <sys/types.h>
#include <libgte.h>

#include "gte.h"

extern const short dw_rsin_tbl[1025];
extern const short dw_ratan_tbl[1025];

#define CMD_RTPS	(GTE_SF | GTE_OP_RTPS)
#define CMD_RTPT	(GTE_SF | GTE_OP_RTPT)
#define CMD_NCLIP	GTE_OP_NCLIP
#define CMD_AVSZ3	GTE_OP_AVSZ3
#define CMD_AVSZ4	GTE_OP_AVSZ4
#define CMD_NCCS	(GTE_SF | GTE_LM | GTE_OP_NCCS)
#define CMD_NCCT	(GTE_SF | GTE_LM | GTE_OP_NCCT)
/* MVMVA, RT, no translation */
#define CMD_RT_V0	(GTE_SF | GTE_MX(0) | GTE_V(0) | GTE_CV(3) | GTE_OP_MVMVA)
#define CMD_RT_IR	(GTE_SF | GTE_MX(0) | GTE_V(3) | GTE_CV(3) | GTE_OP_MVMVA)
#define CMD_RT_IR_NOSF	(GTE_MX(0) | GTE_V(3) | GTE_CV(3) | GTE_OP_MVMVA)

#define FIXED(x)	((x) >> 12)

/* ---- register helpers ---------------------------------------------- */

static void load_mat(gte_s16 dst[3][3], MATRIX *m)
{
	int i, j;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			dst[i][j] = m->m[i][j];
		}
	}
}

static void load_sv(int n, SVECTOR *v)
{
	dw_gte.v[n][0] = v->vx;
	dw_gte.v[n][1] = v->vy;
	dw_gte.v[n][2] = v->vz;
}

static long sxy(int n)
{
	return (long)dw_gte_read_data(12 + n);
}

/* ---- register setup ------------------------------------------------ */

void InitGeom(void)
{
	/* VERIFY: defaults as commonly observed after PsyQ InitGeom(). */
	dw_gte_reset();
	dw_gte.zsf3 = 0x155;
	dw_gte.zsf4 = 0x100;
	dw_gte.h = 1000;
	dw_gte.dqa = (gte_s16)0xEF9E;
	dw_gte.dqb = 0x1400000;
}

void SetRotMatrix(MATRIX *m)
{
	load_mat(dw_gte.rt, m);
}

void SetTransMatrix(MATRIX *m)
{
	dw_gte.tr[0] = m->t[0];
	dw_gte.tr[1] = m->t[1];
	dw_gte.tr[2] = m->t[2];
}

void SetLightMatrix(MATRIX *m)
{
	load_mat(dw_gte.llm, m);
}

void SetColorMatrix(MATRIX *m)
{
	load_mat(dw_gte.lcm, m);
}

void SetBackColor(long rbk, long gbk, long bbk)
{
	dw_gte.bk[0] = rbk << 4;
	dw_gte.bk[1] = gbk << 4;
	dw_gte.bk[2] = bbk << 4;
}

void SetFarColor(long rfc, long gfc, long bfc)
{
	dw_gte.fc[0] = rfc << 4;
	dw_gte.fc[1] = gfc << 4;
	dw_gte.fc[2] = bfc << 4;
}

void SetGeomOffset(long ofx, long ofy)
{
	dw_gte.ofx = ofx << 16;
	dw_gte.ofy = ofy << 16;
}

void SetGeomScreen(long h)
{
	dw_gte.h = (gte_u16)h;
}

/* ---- matrix stack -------------------------------------------------- */

#define MATRIX_STACK_DEPTH 20

static struct {
	gte_s16 rt[3][3];
	gte_s32 tr[3];
} matrix_stack[MATRIX_STACK_DEPTH];
static int matrix_sp;

void PushMatrix(void)
{
	int i, j;

	if (matrix_sp >= MATRIX_STACK_DEPTH) {
		return;
	}
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			matrix_stack[matrix_sp].rt[i][j] = dw_gte.rt[i][j];
		}
		matrix_stack[matrix_sp].tr[i] = dw_gte.tr[i];
	}
	matrix_sp++;
}

void PopMatrix(void)
{
	int i, j;

	if (matrix_sp <= 0) {
		return;
	}
	matrix_sp--;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			dw_gte.rt[i][j] = matrix_stack[matrix_sp].rt[i][j];
		}
		dw_gte.tr[i] = matrix_stack[matrix_sp].tr[i];
	}
}

/* ---- vector and matrix products ------------------------------------ */

VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{
	SetRotMatrix(m);
	load_sv(0, v0);
	dw_gte_cmd(CMD_RT_V0);
	v1->vx = dw_gte.mac[1];
	v1->vy = dw_gte.mac[2];
	v1->vz = dw_gte.mac[3];
	return v1;
}

SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{
	SetRotMatrix(m);
	load_sv(0, v0);
	dw_gte_cmd(CMD_RT_V0);
	v1->vx = dw_gte.ir[1];
	v1->vy = dw_gte.ir[2];
	v1->vz = dw_gte.ir[3];
	return v1;
}

/*
 * RT * v for a 32-bit vector, split into 15-bit halves:
 * (RT * hi) << 3 + (RT * lo) >> 12. VERIFY rounding against libgte.
 */
static void apply_rt_long(long x, long y, long z, long *out)
{
	long hi[3], lo[3];
	int i;

	dw_gte.ir[1] = (gte_s16)(x >> 15);
	dw_gte.ir[2] = (gte_s16)(y >> 15);
	dw_gte.ir[3] = (gte_s16)(z >> 15);
	dw_gte_cmd(CMD_RT_IR_NOSF);
	for (i = 0; i < 3; i++) {
		hi[i] = dw_gte.mac[i + 1];
	}

	dw_gte.ir[1] = (gte_s16)(x & 0x7FFF);
	dw_gte.ir[2] = (gte_s16)(y & 0x7FFF);
	dw_gte.ir[3] = (gte_s16)(z & 0x7FFF);
	dw_gte_cmd(CMD_RT_IR);
	for (i = 0; i < 3; i++) {
		lo[i] = dw_gte.mac[i + 1];
	}

	for (i = 0; i < 3; i++) {
		out[i] = (hi[i] << 3) + lo[i];
	}
}

VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	long out[3];

	SetRotMatrix(m);
	apply_rt_long(v0->vx, v0->vy, v0->vz, out);
	v1->vx = out[0];
	v1->vy = out[1];
	v1->vz = out[2];
	return v1;
}

VECTOR *ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
	MATRIX t;
	long out[3];

	TransposeMatrix(m, &t);
	SetRotMatrix(&t);
	apply_rt_long(v0->vx, v0->vy, v0->vz, out);
	v1->vx = out[0];
	v1->vy = out[1];
	v1->vz = out[2];
	return v1;
}

/* m2 = m0 * m1, column by column through MVMVA (RT=m0, v=IR). */
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	short r[3][3];
	int i, j;

	SetRotMatrix(m0);
	for (j = 0; j < 3; j++) {
		dw_gte.ir[1] = m1->m[0][j];
		dw_gte.ir[2] = m1->m[1][j];
		dw_gte.ir[3] = m1->m[2][j];
		dw_gte_cmd(CMD_RT_IR);
		for (i = 0; i < 3; i++) {
			r[i][j] = dw_gte.ir[i + 1];
		}
	}
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			m2->m[i][j] = r[i][j];
		}
	}
	return m2;
}

MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1)
{
	return MulMatrix0(m0, m1, m0);
}

/* m2 = m0 * m1 including translation: t2 = m0 * t1 + t0. VERIFY. */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
	long t[3];
	long t0x = m0->t[0], t0y = m0->t[1], t0z = m0->t[2];

	SetRotMatrix(m0);
	apply_rt_long(m1->t[0], m1->t[1], m1->t[2], t);
	MulMatrix0(m0, m1, m2);
	m2->t[0] = t[0] + t0x;
	m2->t[1] = t[1] + t0y;
	m2->t[2] = t[2] + t0z;
	return m2;
}

MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1)
{
	short t[3][3];
	int i, j;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			t[i][j] = m0->m[j][i];
		}
	}
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			m1->m[i][j] = t[i][j];
		}
	}
	return m1;
}

/* m = m * diag(v): column j is scaled by v[j]. VERIFY saturation. */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v)
{
	int i;

	for (i = 0; i < 3; i++) {
		m->m[i][0] = (short)FIXED(m->m[i][0] * v->vx);
		m->m[i][1] = (short)FIXED(m->m[i][1] * v->vy);
		m->m[i][2] = (short)FIXED(m->m[i][2] * v->vz);
	}
	return m;
}

MATRIX *TransMatrix(MATRIX *m, VECTOR *v)
{
	m->t[0] = v->vx;
	m->t[1] = v->vy;
	m->t[2] = v->vz;
	return m;
}

/* ---- trigonometry --------------------------------------------------- */

/* sin for 0..0xFFF (4096 = 360 degrees) */
static int sin_1(int a)
{
	if (a < 0x800) {
		return a <= 0x400 ? dw_rsin_tbl[a] : dw_rsin_tbl[0x800 - a];
	}
	a -= 0x800;
	return a <= 0x400 ? -dw_rsin_tbl[a] : -dw_rsin_tbl[0x800 - a];
}

int rsin(int a)
{
	if (a < 0) {
		return -sin_1(-a & 0xFFF);
	}
	return sin_1(a & 0xFFF);
}

int rcos(int a)
{
	if (a < 0) {
		a = -a;
	}
	return sin_1((a + 0x400) & 0xFFF);
}

long ratan2(long y, long x)
{
	int xneg = x < 0;
	int yneg = y < 0;
	long v;

	if (x == 0 && y == 0) {
		return 0;
	}
	if (xneg) {
		x = -x;
	}
	if (yneg) {
		y = -y;
	}
	/* Keep (n << 10) within 32 bits. */
	while ((x | y) & ~0x1FFFFFL) {
		x >>= 1;
		y >>= 1;
	}
	if (y < x) {
		v = dw_ratan_tbl[(y << 10) / x];
	} else {
		v = 1024 - dw_ratan_tbl[(x << 10) / y];
	}
	if (xneg) {
		v = 2048 - v;
	}
	if (yneg) {
		v = -v;
	}
	return v;
}

long SquareRoot0(long a)
{
	unsigned long x = (unsigned long)a;
	unsigned long r = 0;
	unsigned long bit = 1UL << 30;

	/* VERIFY: PsyQ uses a table; this is floor(sqrt(a)). */
	if (a <= 0) {
		return 0;
	}
	while (bit > x) {
		bit >>= 2;
	}
	while (bit) {
		if (x >= r + bit) {
			x -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (long)r;
}

/*
 * Rotation matrices (4096 = 1.0). RotMatrix is Rx * Ry * Rz, written in
 * the angle-sum form used by PsyQ so that the fixed-point rounding
 * matches. VERIFY against libgte.
 */
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
	int c0 = rcos(r->vx), c1 = rcos(r->vy), c2 = rcos(r->vz);
	int s0 = rsin(r->vx), s1 = rsin(r->vy), s2 = rsin(r->vz);
	int s2p0 = rsin(r->vz + r->vx), s2m0 = rsin(r->vz - r->vx);
	int c2p0 = rcos(r->vz + r->vx), c2m0 = rcos(r->vz - r->vx);
	int s2c0 = (s2p0 + s2m0) / 2;
	int c2s0 = (s2p0 - s2m0) / 2;
	int s2s0 = (c2m0 - c2p0) / 2;
	int c2c0 = (c2m0 + c2p0) / 2;

	m->m[0][0] = (short)FIXED(c2 * c1);
	m->m[1][0] = (short)(s2c0 + FIXED(c2s0 * s1));
	m->m[2][0] = (short)(s2s0 - FIXED(c2c0 * s1));
	m->m[0][1] = (short)-FIXED(s2 * c1);
	m->m[1][1] = (short)(c2c0 - FIXED(s2s0 * s1));
	m->m[2][1] = (short)(c2s0 + FIXED(s2c0 * s1));
	m->m[0][2] = (short)s1;
	m->m[1][2] = (short)-FIXED(c1 * s0);
	m->m[2][2] = (short)FIXED(c1 * c0);
	return m;
}

/* Ry * Rx * Rz. VERIFY. */
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
	int c0 = rcos(r->vx), c1 = rcos(r->vy), c2 = rcos(r->vz);
	int s0 = rsin(r->vx), s1 = rsin(r->vy), s2 = rsin(r->vz);
	int s0s2 = FIXED(s0 * s2), s0c2 = FIXED(s0 * c2);

	m->m[0][0] = (short)(FIXED(c1 * c2) + FIXED(s1 * s0s2));
	m->m[0][1] = (short)(FIXED(s1 * s0c2) - FIXED(c1 * s2));
	m->m[0][2] = (short)FIXED(s1 * c0);
	m->m[1][0] = (short)FIXED(c0 * s2);
	m->m[1][1] = (short)FIXED(c0 * c2);
	m->m[1][2] = (short)-s0;
	m->m[2][0] = (short)(FIXED(c1 * s0s2) - FIXED(s1 * c2));
	m->m[2][1] = (short)(FIXED(s1 * s2) + FIXED(c1 * s0c2));
	m->m[2][2] = (short)FIXED(c1 * c0);
	return m;
}

/* Rz * Ry * Rx. VERIFY. */
MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m)
{
	int c0 = rcos(r->vx), c1 = rcos(r->vy), c2 = rcos(r->vz);
	int s0 = rsin(r->vx), s1 = rsin(r->vy), s2 = rsin(r->vz);
	int s1s0 = FIXED(s1 * s0), s1c0 = FIXED(s1 * c0);

	m->m[0][0] = (short)FIXED(c2 * c1);
	m->m[0][1] = (short)(FIXED(c2 * s1s0) - FIXED(s2 * c0));
	m->m[0][2] = (short)(FIXED(c2 * s1c0) + FIXED(s2 * s0));
	m->m[1][0] = (short)FIXED(s2 * c1);
	m->m[1][1] = (short)(FIXED(s2 * s1s0) + FIXED(c2 * c0));
	m->m[1][2] = (short)(FIXED(s2 * s1c0) - FIXED(c2 * s0));
	m->m[2][0] = (short)-s1;
	m->m[2][1] = (short)FIXED(c1 * s0);
	m->m[2][2] = (short)FIXED(c1 * c0);
	return m;
}

/* ---- perspective transforms ---------------------------------------- */

long RotTransPers(SVECTOR *v0, long *sxy0, long *p, long *flag)
{
	load_sv(0, v0);
	dw_gte_cmd(CMD_RTPS);
	*sxy0 = sxy(2);
	*p = dw_gte.ir[0];
	*flag = (long)dw_gte.flag;
	return dw_gte.sz[3] >> 2;
}

long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
		   long *sxy0, long *sxy1, long *sxy2, long *p, long *flag)
{
	load_sv(0, v0);
	load_sv(1, v1);
	load_sv(2, v2);
	dw_gte_cmd(CMD_RTPT);
	*sxy0 = sxy(0);
	*sxy1 = sxy(1);
	*sxy2 = sxy(2);
	*p = dw_gte.ir[0];
	*flag = (long)dw_gte.flag;
	return dw_gte.sz[3] >> 2;
}

long RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
		   long *sxy0, long *sxy1, long *sxy2, long *sxy3,
		   long *p, long *flag)
{
	gte_u32 f;

	load_sv(0, v0);
	load_sv(1, v1);
	load_sv(2, v2);
	dw_gte_cmd(CMD_RTPT);
	*sxy0 = sxy(0);
	*sxy1 = sxy(1);
	*sxy2 = sxy(2);
	f = dw_gte.flag;
	load_sv(0, v3);
	dw_gte_cmd(CMD_RTPS);
	*sxy3 = sxy(2);
	*p = dw_gte.ir[0];
	*flag = (long)(dw_gte.flag | f);
	return dw_gte.sz[3] >> 2;
}

/* VERIFY: outputs are only written for front-facing (opz > 0) polygons. */
long RotNclip3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
	       long *sxy0, long *sxy1, long *sxy2, long *p, long *otz,
	       long *flag)
{
	long opz;

	load_sv(0, v0);
	load_sv(1, v1);
	load_sv(2, v2);
	dw_gte_cmd(CMD_RTPT);
	*flag = (long)dw_gte.flag;
	dw_gte_cmd(CMD_NCLIP);
	opz = dw_gte.mac[0];
	if (opz > 0) {
		*sxy0 = sxy(0);
		*sxy1 = sxy(1);
		*sxy2 = sxy(2);
		*p = dw_gte.ir[0];
		dw_gte_cmd(CMD_AVSZ3);
		*otz = dw_gte.otz;
	}
	return opz;
}

/* VERIFY: as RotNclip3, fourth vertex only for front-facing polygons. */
long RotNclip4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
	       long *sxy0, long *sxy1, long *sxy2, long *sxy3,
	       long *p, long *otz, long *flag)
{
	long opz;
	gte_u32 f;

	load_sv(0, v0);
	load_sv(1, v1);
	load_sv(2, v2);
	dw_gte_cmd(CMD_RTPT);
	f = dw_gte.flag;
	dw_gte_cmd(CMD_NCLIP);
	opz = dw_gte.mac[0];
	*flag = (long)f;
	if (opz > 0) {
		*sxy0 = sxy(0);
		*sxy1 = sxy(1);
		*sxy2 = sxy(2);
		load_sv(0, v3);
		dw_gte_cmd(CMD_RTPS);
		*sxy3 = sxy(2);
		*p = dw_gte.ir[0];
		*flag = (long)(dw_gte.flag | f);
		dw_gte_cmd(CMD_AVSZ4);
		*otz = dw_gte.otz;
	}
	return opz;
}

/* ---- lighting ------------------------------------------------------- */

static void load_rgbc(CVECTOR *c)
{
	dw_gte.rgbc[0] = c->r;
	dw_gte.rgbc[1] = c->g;
	dw_gte.rgbc[2] = c->b;
	dw_gte.rgbc[3] = c->cd;
}

static void store_rgb(int n, CVECTOR *c)
{
	c->r = dw_gte.rgb[n][0];
	c->g = dw_gte.rgb[n][1];
	c->b = dw_gte.rgb[n][2];
	c->cd = dw_gte.rgb[n][3];
}

void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2)
{
	load_sv(0, v0);
	load_rgbc(v1);
	dw_gte_cmd(CMD_NCCS);
	store_rgb(2, v2);
}

void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, CVECTOR *v3,
		     CVECTOR *v4, CVECTOR *v5, CVECTOR *v6)
{
	load_sv(0, v0);
	load_sv(1, v1);
	load_sv(2, v2);
	load_rgbc(v3);
	dw_gte_cmd(CMD_NCCT);
	store_rgb(0, v4);
	store_rgb(1, v5);
	store_rgb(2, v6);
}
