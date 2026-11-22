/* 6502 (2A03) instruction templates: the opcode table (ops.c) and the C of
 * each instruction (c6502.c), for the interpreter's switch (cpu6502_gen)
 * and for recompilers. */
#ifndef CPU6502_C6502_H
#define CPU6502_C6502_H

#include <stdint.h>

#include "template.h"  /* Lines, Buf, bprintf (core/recomp) */

/* ---- ops.c: the opcode table (16x16 grids, nesdev "CPU unofficial opcodes") ---- */
typedef struct {
    uint8_t code, size, cycles, official;
    char mn[4], mode[4];
} Op;

extern Op OPS[256];
void ops_init(void);
int is_branch(const char *mn);
int page_penalty(const Op *op);

/* ---- c6502.c: C statements executing one instruction (shared with the interpreter) ---- */
void body(const Op *op, Lines *out);
/* The interpreter's switch (cpu6502.c), one case per opcode. */
void emit_cpu_switch(Buf *out);

#endif
