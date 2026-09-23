/*
 * Port wrapper for PsyQ <libetc.h>: the 1 KB scratchpad at 0x1F800000
 * becomes an ordinary buffer.
 */
#ifndef DW_PORT_LIBETC_H
#define DW_PORT_LIBETC_H

#include_next <libetc.h>

extern u_long dw_scratchpad[256];

#undef getScratchAddr
#define getScratchAddr(offset) (&dw_scratchpad[(offset)])

#endif /* DW_PORT_LIBETC_H */
