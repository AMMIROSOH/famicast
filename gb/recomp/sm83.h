/* gbrecomp: the SM83 instruction templates (sm83.c). The recompiler itself
 * (decoding, blocks, emitting) comes later; for now gbrecomp writes the
 * interpreter's opcode switch from these templates. */
#ifndef GB_SM83_H
#define GB_SM83_H

#include <stddef.h>

#include "template.h"  /* Lines, Buf (core/recomp) */

/* Bytes of an instruction by its first opcode byte (CB xx: 2). */
int sm83_size(int op);
/* The template lines of opcode `op` (cb: of the CB table) and its name, e.g.
 * "LD A,(HL+)". The CB prefix itself ($CB, cb = 0) has no lines: its case
 * in the switch fetches the second byte and dispatches on it. */
void sm83_body(int op, int cb, Lines *out, char *name, size_t len);
/* The interpreter's switch: both tables, the CB one nested at case 0xCB. */
void sm83_cpu_switch(Buf *out);

#endif
