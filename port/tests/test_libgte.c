/*
 * Tests for the port's libgte (port/psyq/libgte.c).
 *
 * Built in the PsyQ header world (32-bit long), so it runs on SH-4 through
 * qemu rather than on a 64-bit host. Expected values are exact identities
 * or hand-computed.
 */
#include <stdio.h>
#include <sys/types.h>
#include <libgte.h>

static int failures;

#define CHECK_EQ(actual, expected)                                          \
	do {                                                                \
		long a_ = (long)(actual);                                   \
		long e_ = (long)(expected);                                 \
		if (a_ != e_) {                                             \
			printf("%s:%d: %s = %ld, expected %ld\n",          \
			       __FILE__, __LINE__, #actual, a_, e_);        \
			failures++;                                         \
		}                                                           \
	} while (0)

static void check_matrix(MATRIX *m, int m00, int m01, int m02, int m10,
			 int m11, int m12, int m20, int m21, int m22, int line)
{
	int want[9];
	int i;

	want[0] = m00; want[1] = m01; want[2] = m02;
	want[3] = m10; want[4] = m11; want[5] = m12;
	want[6] = m20; want[7] = m21; want[8] = m22;
	for (i = 0; i < 9; i++) {
		if (m->m[i / 3][i % 3] != want[i]) {
			printf("line %d: m[%d][%d] = %d, expected %d\n", line,
			       i / 3, i % 3, m->m[i / 3][i % 3], want[i]);
			failures++;
		}
	}
}

static void test_trig(void)
{
	CHECK_EQ(rsin(0), 0);
	CHECK_EQ(rsin(1024), 4096);
	CHECK_EQ(rsin(2048), 0);
	CHECK_EQ(rsin(3072), -4096);
	CHECK_EQ(rsin(-1024), -4096);
	CHECK_EQ(rsin(4096 + 1024), 4096);
	CHECK_EQ(rcos(0), 4096);
	CHECK_EQ(rcos(1024), 0);
	CHECK_EQ(rcos(2048), -4096);
	CHECK_EQ(rcos(-2048), -4096);
	CHECK_EQ(rsin(512), rcos(512));

	CHECK_EQ(ratan2(0, 0), 0);
	CHECK_EQ(ratan2(1, 1), 512);
	CHECK_EQ(ratan2(1, 0), 1024);
	CHECK_EQ(ratan2(0, -1), 2048);
	CHECK_EQ(ratan2(-1, 0), -1024);
	CHECK_EQ(ratan2(100000000, 100000000), 512);

	CHECK_EQ(SquareRoot0(0), 0);
	CHECK_EQ(SquareRoot0(1000000), 1000);
	CHECK_EQ(SquareRoot0(1000001), 1000);
	CHECK_EQ(SquareRoot0(0x7FFFFFFF), 46340);
}

static void test_rotation(void)
{
	SVECTOR r = { 0, 0, 0, 0 };
	MATRIX m;

	RotMatrix(&r, &m);
	check_matrix(&m, 4096, 0, 0, 0, 4096, 0, 0, 0, 4096, __LINE__);
	RotMatrixYXZ(&r, &m);
	check_matrix(&m, 4096, 0, 0, 0, 4096, 0, 0, 0, 4096, __LINE__);
	RotMatrixZYX(&r, &m);
	check_matrix(&m, 4096, 0, 0, 0, 4096, 0, 0, 0, 4096, __LINE__);

	/* 90 degrees about Z */
	r.vz = 1024;
	RotMatrix(&r, &m);
	check_matrix(&m, 0, -4096, 0, 4096, 0, 0, 0, 0, 4096, __LINE__);
	RotMatrixZYX(&r, &m);
	check_matrix(&m, 0, -4096, 0, 4096, 0, 0, 0, 0, 4096, __LINE__);

	/* 90 degrees about X */
	r.vz = 0;
	r.vx = 1024;
	RotMatrix(&r, &m);
	check_matrix(&m, 4096, 0, 0, 0, 0, -4096, 0, 4096, 0, __LINE__);

	/* 90 degrees about Y */
	r.vx = 0;
	r.vy = 1024;
	RotMatrixYXZ(&r, &m);
	check_matrix(&m, 0, 0, 4096, 0, 4096, 0, -4096, 0, 0, __LINE__);
}

static void test_products(void)
{
	MATRIX id = { { { 4096, 0, 0 }, { 0, 4096, 0 }, { 0, 0, 4096 } }, { 0, 0, 0 } };
	MATRIX a = { { { 4096, 0, 0 }, { 0, 0, -4096 }, { 0, 4096, 0 } }, { 10, 20, 30 } };
	MATRIX out;
	SVECTOR sv = { 100, -200, 300, 0 };
	SVECTOR svo;
	VECTOR lv = { 100000, -200000, 3, 0 };
	VECTOR lvo;
	VECTOR scale = { 8192, 4096, 2048, 0 };

	MulMatrix0(&id, &a, &out);
	check_matrix(&out, 4096, 0, 0, 0, 0, -4096, 0, 4096, 0, __LINE__);

	ApplyMatrix(&a, &sv, &lvo);
	CHECK_EQ(lvo.vx, 100);
	CHECK_EQ(lvo.vy, -300);
	CHECK_EQ(lvo.vz, -200);

	ApplyMatrixSV(&a, &sv, &svo);
	CHECK_EQ(svo.vy, -300);

	/* Values beyond 16 bits go through the split long-vector path. */
	ApplyMatrixLV(&id, &lv, &lvo);
	CHECK_EQ(lvo.vx, 100000);
	CHECK_EQ(lvo.vy, -200000);
	CHECK_EQ(lvo.vz, 3);

	ApplyTransposeMatrixLV(&a, &lv, &lvo);
	CHECK_EQ(lvo.vx, 100000);
	CHECK_EQ(lvo.vy, 3);
	CHECK_EQ(lvo.vz, 200000);

	/* CompMatrix: t2 = m0 * t1 + t0 */
	CompMatrix(&a, &a, &out);
	check_matrix(&out, 4096, 0, 0, 0, -4096, 0, 0, 0, -4096, __LINE__);
	CHECK_EQ(out.t[0], 20);
	CHECK_EQ(out.t[1], 20 - 30);
	CHECK_EQ(out.t[2], 30 + 20);

	out = id;
	ScaleMatrix(&out, &scale);
	check_matrix(&out, 8192, 0, 0, 0, 4096, 0, 0, 0, 2048, __LINE__);

	TransposeMatrix(&a, &out);
	check_matrix(&out, 4096, 0, 0, 0, 0, 4096, 0, -4096, 0, __LINE__);
}

static void test_projection(void)
{
	MATRIX m = { { { 4096, 0, 0 }, { 0, 4096, 0 }, { 0, 0, 4096 } }, { 0, 0, 1000 } };
	SVECTOR v0 = { 100, -50, 0, 0 };
	SVECTOR v1 = { -100, -50, 0, 0 };
	SVECTOR v2 = { 0, 100, 0, 0 };
	SVECTOR v3 = { 10, 10, 0, 0 };
	long sxy0, sxy1, sxy2, sxy3, p, flag, otz, opz;

	InitGeom();
	SetGeomOffset(160, 120);
	SetGeomScreen(1000);
	SetRotMatrix(&m);
	SetTransMatrix(&m);

	otz = RotTransPers(&v0, &sxy0, &p, &flag);
	CHECK_EQ(sxy0 & 0xFFFF, 260);
	CHECK_EQ(sxy0 >> 16, 70);
	CHECK_EQ(otz, 1000 >> 2);
	/* The default depth cue saturates IR0 (FLAG.12), which is not an error. */
	CHECK_EQ(flag & 0x80000000, 0);

	RotTransPers4(&v0, &v1, &v2, &v3, &sxy0, &sxy1, &sxy2, &sxy3, &p, &flag);
	CHECK_EQ(sxy1 & 0xFFFF, 60);
	CHECK_EQ(sxy2 >> 16, 220);
	CHECK_EQ(sxy3 & 0xFFFF, 170);

	/* v0, v1, v2 wind clockwise on screen: NCLIP is negative. */
	opz = RotNclip3(&v0, &v1, &v2, &sxy0, &sxy1, &sxy2, &p, &otz, &flag);
	CHECK_EQ(opz < 0, 1);
	opz = RotNclip3(&v0, &v2, &v1, &sxy0, &sxy1, &sxy2, &p, &otz, &flag);
	CHECK_EQ(opz > 0, 1);
	CHECK_EQ(otz, (0x155 * 3000) >> 12);
}

int main(void)
{
	test_trig();
	test_rotation();
	test_products();
	test_projection();

	if (failures) {
		printf("test_libgte: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_libgte: all tests passed\n");
	return 0;
}
