/*
 * Core runtime for the non-matching port build (DW_PORT): the pieces of
 * the PS1 environment that the game code relies on directly.
 *
 * - dw_scratchpad: the 1 KB data cache at 0x1F800000 (getScratchAddr).
 * - dw_psx_arena:  a copy of the overlay load area, 0x80010000..0x80090000.
 *   Overlay files are still read into it (the loader's *_START targets),
 *   and fixed buffer addresses map into it (include/dw/psx_addr.h).
 * - Overlay data snapshots: overlay code and data are linked statically.
 *   Loading an overlay on the PS1 resets its data, so dw_overlay_reset()
 *   restores the startup contents.
 * - setjmp/longjmp support for port/include/setjmp.h.
 * - The entry point, which wraps the game's main() (built as dw_game_main).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DW_ARENA_BASE	0x80010000u
#define DW_ARENA_SIZE	0x80000u

unsigned long dw_scratchpad[256] __attribute__((aligned(32)));
unsigned char dw_psx_arena[DW_ARENA_SIZE] __attribute__((aligned(32)));

/* ---- overlay load addresses (config/overlay.ld) -------------------- */

#define DW_STR2(x) #x
#define DW_STR(x) DW_STR2(x)
#define DW_SYM(name) DW_STR(__USER_LABEL_PREFIX__) #name

#define OVERLAY_START(name, addr)                                          \
	__asm__(".globl " DW_SYM(name##_START) "\n\t"                      \
		".set " DW_SYM(name##_START) ", " DW_SYM(dw_psx_arena)     \
		" + (" #addr " - 0x80010000)");

OVERLAY_START(btl, 0x80052AE0)
OVERLAY_START(dget, 0x80080800)
OVERLAY_START(doo2, 0x80070000)
OVERLAY_START(dooa, 0x80080000)
OVERLAY_START(eab, 0x80060000)
OVERLAY_START(endi, 0x80060000)
OVERLAY_START(evl, 0x80060000)
OVERLAY_START(fish, 0x80070000)
OVERLAY_START(kar, 0x80053800)
OVERLAY_START(mov, 0x80010000)
OVERLAY_START(murd, 0x8007C000)
OVERLAY_START(std, 0x80052AE0)
OVERLAY_START(trn2, 0x80088800)
OVERLAY_START(trn, 0x80088800)
OVERLAY_START(vs, 0x80052AE0)

/* ---- overlay data snapshots ---------------------------------------- */

/*
 * The port Makefile renames each overlay object's .data and .bss to
 * dw_ovl_<name>_data / dw_ovl_<name>_bss, so the linker groups them and
 * defines __start_/__stop_ bounds.
 */
#define OVERLAY_SECTIONS(name)                                             \
	extern char __start_dw_ovl_##name##_data[] __attribute__((weak));  \
	extern char __stop_dw_ovl_##name##_data[] __attribute__((weak));   \
	extern char __start_dw_ovl_##name##_bss[] __attribute__((weak));   \
	extern char __stop_dw_ovl_##name##_bss[] __attribute__((weak));

OVERLAY_SECTIONS(btl)
OVERLAY_SECTIONS(dget)
OVERLAY_SECTIONS(doo2)
OVERLAY_SECTIONS(dooa)
OVERLAY_SECTIONS(eab)
OVERLAY_SECTIONS(endi)
OVERLAY_SECTIONS(evl)
OVERLAY_SECTIONS(fish)
OVERLAY_SECTIONS(kar)
OVERLAY_SECTIONS(mov)
OVERLAY_SECTIONS(murd)
OVERLAY_SECTIONS(std)
OVERLAY_SECTIONS(trn2)
OVERLAY_SECTIONS(trn)
OVERLAY_SECTIONS(vs)

typedef struct {
	const char *name;
	char *start[2];
	char *stop[2];
	char *saved[2];
} OverlayData;

#define OVL(name)                                                          \
	{ #name,                                                           \
	  { __start_dw_ovl_##name##_data, __start_dw_ovl_##name##_bss },   \
	  { __stop_dw_ovl_##name##_data, __stop_dw_ovl_##name##_bss },     \
	  { NULL, NULL } }
#define NO_OVL(name) { #name, { NULL, NULL }, { NULL, NULL }, { NULL, NULL } }

/* Indexed by the game's Overlay enum (include/dw/utils.h), minus one. */
static OverlayData overlays[] = {
	OVL(btl), OVL(std), OVL(fish), OVL(evl), OVL(kar), OVL(vs), OVL(mov),
	OVL(doo2), OVL(dooa), OVL(trn),
	NO_OVL(shop),	/* SHOP_REL.BIN has not been decompiled */
	OVL(dget), OVL(trn2), OVL(murd), OVL(endi), OVL(eab),
};

#define NUM_OVERLAYS ((int)(sizeof(overlays) / sizeof(overlays[0])))

static void snapshot_overlays(void)
{
	int i, s;

	for (i = 0; i < NUM_OVERLAYS; i++) {
		for (s = 0; s < 2; s++) {
			OverlayData *o = &overlays[i];
			size_t size;

			if (o->start[s] == NULL || o->stop[s] <= o->start[s]) {
				continue;
			}
			size = (size_t)(o->stop[s] - o->start[s]);
			o->saved[s] = malloc(size);
			if (o->saved[s] == NULL) {
				printf("[port] out of memory saving overlay %s\n", o->name);
				abort();
			}
			memcpy(o->saved[s], o->start[s], size);
		}
	}
}

void dw_overlay_reset(int lib)
{
	OverlayData *o;
	int s;

	if (lib < 1 || lib > NUM_OVERLAYS) {
		return;
	}
	o = &overlays[lib - 1];
	if (o->start[0] == NULL && o->start[1] == NULL) {
		printf("[port] overlay %s is not available in this build\n", o->name);
		return;
	}
	for (s = 0; s < 2; s++) {
		if (o->saved[s] != NULL) {
			memcpy(o->start[s], o->saved[s], (size_t)(o->stop[s] - o->start[s]));
		}
	}
}

/* ---- linker symbols read by initializeHeap() ----------------------- */

/*
 * initializeHeap() passes these to the PsyQ InitHeap3(). The port's heap is
 * the C library's, so they describe an empty PS1 heap: the heap starts at
 * (_end & ~0xF) + 0x10 = 0x801FFF00 and ends at the stack, also 0x801FFF00.
 */
unsigned long dw_rts_end = 0x801FFEF0;
unsigned long dw_rts_stack_addr = 0x801FFF00;
unsigned long dw_rts_stack_size = 0;

/* ---- setjmp/longjmp (port/include/setjmp.h) ------------------------ */

volatile int dw_jmp_value;

void dw_longjmp(int *env, int value)
{
	dw_jmp_value = value != 0 ? value : 1;
	__builtin_longjmp((void **)env, 1);
}

/* ---- unimplemented SDK functions (generated stubs) ----------------- */

void dw_stub_called(const char *name)
{
	printf("[psyq stub] %s\n", name);
}

/* ---- entry point ---------------------------------------------------- */

int dw_game_main(void);

int main(void)
{
	/* Unbuffered, so the log survives a hang or a crash. */
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("[port] Digimon World port runtime starting\n");
	snapshot_overlays();
	return dw_game_main();
}
