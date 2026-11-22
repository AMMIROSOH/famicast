/* The Z80 instruction templates (z80.c), for the interpreter's opcode
 * switch (z80_gen) and, later, recompiled blocks. */
#ifndef Z80_H
#define Z80_H

#include <stddef.h>

#include "template.h"  /* Lines, Buf (core/recomp) */

/* The opcode tables. The prefixed ones are the base table with HL, H, L and
 * (HL) replaced by IX or IY, IXH, IXL and (IX+d), made by the same code. */
enum { Z80_T_BASE, Z80_T_CB, Z80_T_ED, Z80_T_DD, Z80_T_FD, Z80_T_DDCB, Z80_T_FDCB };

/* The template lines of opcode `op` of table `table` and its name, e.g.
 * "LD A,(IX+d)". Returns 0 when the opcode has no line of its own in that
 * table: after DD or FD, an opcode that does not use HL, H, L or (HL) runs
 * as in the base table (the prefix costs 4 T-states and an R count); the
 * prefixes themselves ($CB, $DD, $ED, $FD) have no lines either: their
 * cases in the switch fetch the next byte and dispatch on it.
 * DDCB/FDCB lines expect `ea`, the address (IX+d), computed by the switch
 * after it read d and the opcode (it also added the 2 T-states between). */
int z80_body(int table, int op, Lines *out, char *name, size_t len);
/* The interpreter's switch: the base table, with the other tables nested at
 * the prefix cases. `op` is the variable it dispatches on and `again:` the
 * label before the base switch (z80_cpu.c). */
void z80_cpu_switch(Buf *out);

#endif
