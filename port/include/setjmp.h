/*
 * Port replacement for PsyQ <setjmp.h>.
 *
 * The script interpreter keeps its jmp_buf (SCRIPT_JMP_BUF) in the
 * generated BSS, which reserves the PS1 size of 12 words. A C library
 * jmp_buf for SH-4 is larger, so this uses GCC's __builtin_setjmp,
 * whose buffer is 5 words, and carries the longjmp value separately.
 * The game only ever longjmps with non-zero values.
 */
#ifndef DW_PORT_SETJMP_H
#define DW_PORT_SETJMP_H

typedef int jmp_buf[12];

extern volatile int dw_jmp_value;

void dw_longjmp(jmp_buf env, int value) __attribute__((noreturn));

#define setjmp(env)		(__builtin_setjmp((void **)(env)) ? dw_jmp_value : 0)
#define longjmp(env, value)	dw_longjmp((env), (value))

#endif /* DW_PORT_SETJMP_H */
