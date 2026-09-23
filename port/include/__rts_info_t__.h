/*
 * Port replacement for the Metrowerks linker-symbol header.
 *
 * The game only reads _end, _stack_addr and _stack_size, to size the PsyQ
 * heap in initializeHeap(). The port uses the toolchain's malloc, so these
 * are plain variables in the port runtime, renamed to stay clear of the
 * GNU linker's own _end.
 */
#ifndef DW_PORT_RTS_INFO_T_H
#define DW_PORT_RTS_INFO_T_H

#define _end		dw_rts_end
#define _stack_addr	dw_rts_stack_addr
#define _stack_size	dw_rts_stack_size

extern unsigned long _end;
extern unsigned long _stack_addr;
extern unsigned long _stack_size;

#endif /* DW_PORT_RTS_INFO_T_H */
