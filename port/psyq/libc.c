/*
 * PsyQ libc behaviour that differs from the toolchain C library.
 */

/*
 * The game's random() computes (limit * rand()) >> 15, which relies on
 * PsyQ's RAND_MAX of 0x7FFF. newlib and glibc return 31-bit values, so
 * the port provides the PsyQ generator (the ANSI C example LCG).
 * VERIFY: the initial seed against the libc data in SLUS_010.32
 * (psyq/libc .data at 0x85A34). The game never calls srand().
 */
static unsigned int rand_next = 0x24040001;

int rand(void)
{
	rand_next = rand_next * 0x41C64E6D + 0x3039;
	return (int)((rand_next >> 16) & 0x7FFF);
}

void srand(unsigned int seed)
{
	rand_next = seed;
}
