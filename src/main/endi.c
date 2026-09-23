#include <libgpu.h>

#include <dw/endi.h>
#include <dw/graphics.h>
#include <dw/main.h>
#include <dw/types.h>

u_long *ENDI_FADE_CLUT_BUFFER = (u_long *)GENERAL_BUFFER;
u_long *ENDI_CLUT_BUFFER = (u_long *)(GENERAL_BUFFER + 0x304);
RGB8 ENDI_PARTICLE_COLOR = { 0x80, 0x80, 0x80 };
