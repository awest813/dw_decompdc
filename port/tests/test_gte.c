/*
 * Unit tests for the software GTE (port/psyq/gte.c).
 *
 * Expected values are derived by hand from the psx-spx GTE description.
 * The tests run on the host and, through qemu, on SH-4 (see port/Makefile).
 */
#include <stdio.h>

#include "../psyq/gte.h"

static int failures;

#define CHECK_EQ(actual, expected)                                          \
	do {                                                                \
		long long a_ = (long long)(actual);                         \
		long long e_ = (long long)(expected);                       \
		if (a_ != e_) {                                             \
			printf("%s:%d: %s = %lld, expected %lld\n",        \
			       __FILE__, __LINE__, #actual, a_, e_);        \
			failures++;                                         \
		}                                                           \
	} while (0)

#define FLAG_BIT(n) ((dw_gte.flag >> (n)) & 1)

#define CMD_RTPS (GTE_SF | GTE_OP_RTPS)
#define CMD_RTPT (GTE_SF | GTE_OP_RTPT)

static void identity_rt(void)
{
	dw_gte.rt[0][0] = dw_gte.rt[1][1] = dw_gte.rt[2][2] = 0x1000;
}

static void set_v(int n, int x, int y, int z)
{
	dw_gte.v[n][0] = (gte_s16)x;
	dw_gte.v[n][1] = (gte_s16)y;
	dw_gte.v[n][2] = (gte_s16)z;
}

static void test_rtps_basic(void)
{
	dw_gte_reset();
	identity_rt();
	dw_gte.tr[2] = 1000;
	dw_gte.h = 1000;
	set_v(0, 100, -50, 0);
	dw_gte_cmd(CMD_RTPS);

	CHECK_EQ(dw_gte.mac[1], 100);
	CHECK_EQ(dw_gte.mac[3], 1000);
	CHECK_EQ(dw_gte.sz[3], 1000);
	CHECK_EQ(dw_gte.sx[2], 100);
	CHECK_EQ(dw_gte.sy[2], -50);
	CHECK_EQ(dw_gte.flag, 0);

	/* Screen offset is 16.16 fixed point. */
	dw_gte.ofx = 160 << 16;
	dw_gte.ofy = 120 << 16;
	dw_gte_cmd(CMD_RTPS);
	CHECK_EQ(dw_gte.sx[2], 260);
	CHECK_EQ(dw_gte.sy[2], 70);
	/* FIFO shifted */
	CHECK_EQ(dw_gte.sx[1], 100);
	CHECK_EQ(dw_gte.sz[2], 1000);
}

static void test_rtps_saturation(void)
{
	/* SX saturates to 0x3FF and sets FLAG.14 plus the error bit. */
	dw_gte_reset();
	identity_rt();
	dw_gte.tr[2] = 1000;
	dw_gte.h = 1000;
	set_v(0, 2000, 0, 0);
	dw_gte_cmd(CMD_RTPS);
	CHECK_EQ(dw_gte.sx[2], 0x3FF);
	CHECK_EQ(FLAG_BIT(14), 1);
	CHECK_EQ(FLAG_BIT(31), 1);

	/* Division overflow: H >= SZ3 * 2 gives 0x1FFFF and FLAG.17. */
	dw_gte_reset();
	identity_rt();
	dw_gte.tr[2] = 100;
	dw_gte.h = 1000;
	set_v(0, 100, 0, 0);
	dw_gte_cmd(CMD_RTPS);
	CHECK_EQ(FLAG_BIT(17), 1);
	CHECK_EQ(dw_gte.sx[2], (0x1FFFF * 100) >> 16);

	/* Negative Z saturates SZ3 to 0 (FLAG.18). */
	dw_gte_reset();
	identity_rt();
	dw_gte.tr[2] = -500;
	dw_gte.h = 1000;
	dw_gte_cmd(CMD_RTPS);
	CHECK_EQ(dw_gte.sz[3], 0);
	CHECK_EQ(FLAG_BIT(18), 1);
	CHECK_EQ(FLAG_BIT(17), 1);
}

static void test_rtps_ir3_quirk(void)
{
	/*
	 * sf=0: MAC3 = 9 << 12 saturates IR3 to 0x7FFF, but FLAG.22 is
	 * decided on MAC3 >> 12 = 9, which is in range.
	 */
	dw_gte_reset();
	dw_gte.tr[2] = 9;
	dw_gte.h = 1000;
	dw_gte_cmd(GTE_OP_RTPS);
	CHECK_EQ(dw_gte.ir[3], 0x7FFF);
	CHECK_EQ(FLAG_BIT(22), 0);
	CHECK_EQ(dw_gte.sz[3], 9);
}

static void test_mac_overflow(void)
{
	dw_gte_reset();
	dw_gte.rt[0][0] = 0x7FFF;
	dw_gte.tr[0] = 0x7FFFFFFF;
	dw_gte.tr[2] = 1000;
	dw_gte.h = 1000;
	set_v(0, 0x7FFF, 0, 0);
	dw_gte_cmd(CMD_RTPS);
	CHECK_EQ(FLAG_BIT(30), 1);
	CHECK_EQ(FLAG_BIT(31), 1);
}

static void test_rtpt_fifo(void)
{
	dw_gte_reset();
	identity_rt();
	dw_gte.tr[2] = 1000;
	dw_gte.h = 1000;
	set_v(0, 1, 2, 0);
	set_v(1, 3, 4, 0);
	set_v(2, 5, 6, 0);
	dw_gte_cmd(CMD_RTPT);
	CHECK_EQ(dw_gte.sx[0], 1);
	CHECK_EQ(dw_gte.sy[0], 2);
	CHECK_EQ(dw_gte.sx[1], 3);
	CHECK_EQ(dw_gte.sy[1], 4);
	CHECK_EQ(dw_gte.sx[2], 5);
	CHECK_EQ(dw_gte.sy[2], 6);
	CHECK_EQ(dw_gte.sz[1], 1000);
}

static void test_nclip_avsz(void)
{
	dw_gte_reset();
	dw_gte.sx[0] = 0;
	dw_gte.sy[0] = 0;
	dw_gte.sx[1] = 10;
	dw_gte.sy[1] = 0;
	dw_gte.sx[2] = 0;
	dw_gte.sy[2] = 10;
	dw_gte_cmd(GTE_OP_NCLIP);
	CHECK_EQ(dw_gte.mac[0], 100);

	dw_gte.zsf3 = 0x155;
	dw_gte.sz[1] = dw_gte.sz[2] = dw_gte.sz[3] = 1000;
	dw_gte_cmd(GTE_OP_AVSZ3);
	CHECK_EQ(dw_gte.mac[0], 0x155 * 3000);
	CHECK_EQ(dw_gte.otz, (0x155 * 3000) >> 12);

	dw_gte.zsf4 = 0x100;
	dw_gte.sz[0] = 1000;
	dw_gte_cmd(GTE_OP_AVSZ4);
	CHECK_EQ(dw_gte.otz, (0x100 * 4000) >> 12);
}

static void test_mvmva_sqr_op(void)
{
	dw_gte_reset();
	identity_rt();
	set_v(0, 1, 2, 3);
	dw_gte_cmd(GTE_SF | GTE_MX(0) | GTE_V(0) | GTE_CV(3) | GTE_OP_MVMVA);
	CHECK_EQ(dw_gte.mac[1], 1);
	CHECK_EQ(dw_gte.mac[2], 2);
	CHECK_EQ(dw_gte.mac[3], 3);

	/* With translation (cv=TR) */
	dw_gte.tr[0] = 10;
	dw_gte_cmd(GTE_SF | GTE_MX(0) | GTE_V(0) | GTE_CV(0) | GTE_OP_MVMVA);
	CHECK_EQ(dw_gte.mac[1], 11);

	dw_gte.ir[1] = 3;
	dw_gte.ir[2] = -4;
	dw_gte.ir[3] = 5;
	dw_gte_cmd(GTE_OP_SQR);
	CHECK_EQ(dw_gte.mac[1], 9);
	CHECK_EQ(dw_gte.mac[2], 16);
	CHECK_EQ(dw_gte.mac[3], 25);

	dw_gte_reset();
	dw_gte.rt[0][0] = 1;
	dw_gte.rt[1][1] = 2;
	dw_gte.rt[2][2] = 3;
	dw_gte.ir[1] = 4;
	dw_gte.ir[2] = 5;
	dw_gte.ir[3] = 6;
	dw_gte_cmd(GTE_OP_OP);
	CHECK_EQ(dw_gte.mac[1], -3);
	CHECK_EQ(dw_gte.mac[2], 6);
	CHECK_EQ(dw_gte.mac[3], -3);
}

static void test_nccs(void)
{
	dw_gte_reset();
	dw_gte.llm[0][0] = 0x1000;
	dw_gte.lcm[0][0] = dw_gte.lcm[1][1] = dw_gte.lcm[2][2] = 0x1000;
	set_v(0, 0x1000, 0, 0);
	dw_gte.rgbc[0] = dw_gte.rgbc[1] = dw_gte.rgbc[2] = 128;
	dw_gte.rgbc[3] = 0x30;
	dw_gte_cmd(GTE_SF | GTE_LM | GTE_OP_NCCS);
	CHECK_EQ(dw_gte.rgb[2][0], 128);
	CHECK_EQ(dw_gte.rgb[2][1], 0);
	CHECK_EQ(dw_gte.rgb[2][2], 0);
	CHECK_EQ(dw_gte.rgb[2][3], 0x30);
	CHECK_EQ(dw_gte.ir[1], 2048);
}

static void test_registers(void)
{
	dw_gte_reset();

	/* RT via ctc2 0..4, as SetRotMatrix does. */
	dw_gte_write_ctrl(0, 0x00021000u);
	dw_gte_write_ctrl(4, 0xFFFFF000u);
	CHECK_EQ(dw_gte.rt[0][0], 0x1000);
	CHECK_EQ(dw_gte.rt[0][1], 2);
	CHECK_EQ(dw_gte.rt[2][2], -0x1000);
	CHECK_EQ(dw_gte_read_ctrl(4), 0xFFFFF000u);

	/* H reads back sign-extended (hardware quirk). */
	dw_gte_write_ctrl(26, 0x8000);
	CHECK_EQ(dw_gte_read_ctrl(26), 0xFFFF8000u);

	/* SXYP write pushes the FIFO. */
	dw_gte_write_data(14, 0x00020001u);
	dw_gte_write_data(15, 0x00040003u);
	CHECK_EQ(dw_gte.sx[1], 1);
	CHECK_EQ(dw_gte.sx[2], 3);
	CHECK_EQ(dw_gte_read_data(15), 0x00040003u);

	/* Leading-zero count of the sign bit. */
	dw_gte_write_data(30, 0);
	CHECK_EQ(dw_gte_read_data(31), 32);
	dw_gte_write_data(30, 1);
	CHECK_EQ(dw_gte_read_data(31), 31);
	dw_gte_write_data(30, 0xFFFFFFFFu);
	CHECK_EQ(dw_gte_read_data(31), 32);
	dw_gte_write_data(30, 0xC0000000u);
	CHECK_EQ(dw_gte_read_data(31), 2);

	/* IRGB/ORGB */
	dw_gte_write_data(28, 0x7FFF);
	CHECK_EQ(dw_gte.ir[1], 0x1F << 7);
	CHECK_EQ(dw_gte_read_data(29), 0x7FFF);

	/* FLAG: only bits 12..30 are writable; bit 31 is derived. */
	dw_gte_write_ctrl(31, 0x00004000u);
	CHECK_EQ(dw_gte_read_ctrl(31), 0x80004000u);
	dw_gte_write_ctrl(31, 0x00000FFFu);
	CHECK_EQ(dw_gte_read_ctrl(31), 0);
}

int main(void)
{
	test_rtps_basic();
	test_rtps_saturation();
	test_rtps_ir3_quirk();
	test_mac_overflow();
	test_rtpt_fifo();
	test_nclip_avsz();
	test_mvmva_sqr_op();
	test_nccs();
	test_registers();

	if (failures) {
		printf("test_gte: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_gte: all tests passed\n");
	return 0;
}
