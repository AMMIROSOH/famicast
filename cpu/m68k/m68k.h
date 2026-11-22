/* The 68000 instruction templates (m68k.c), for the interpreter's handlers
 * (m68k_gen) and, later, recompiled blocks. */
#ifndef M68K_H
#define M68K_H

#include <stddef.h>
#include <stdint.h>

#include "template.h"  /* Lines, Buf (core/recomp) */

/* The template lines of opcode `op` (they read the opcode as `op`, the CPU
 * as `c`); 0 for an illegal opcode (its lines raise the exception). */
int m68k_body(uint16_t op, Lines *out);
/* The interpreter's handlers: one function per distinct body, and the
 * 64K table from opcode to handler. */
void m68k_cpu_handlers(Buf *out);

#endif
