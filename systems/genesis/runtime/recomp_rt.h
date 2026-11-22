/* Interface between the Genesis runtime and recompiled 68000 code
 * (systems/genesis/recomp). A block runs from the instruction in the CPU's queue
 * (c->pc - 4, any instruction of the block) and returns after the one
 * that ends it, or as soon as the interpreter's loop would do something
 * else before the next instruction: the line is over, an interrupt level
 * is presented, the CPU stopped or is tracing (MD_STOP). After each
 * instruction it lets the Z80 catch up, as the loop does after each step.
 * So recompiled and interpreted runs take the same steps, cycle for cycle
 * (genesis_headless --diff checks it). */
#ifndef GENESIS_RECOMP_RT_H
#define GENESIS_RECOMP_RT_H

#include <stddef.h>
#include <stdint.h>

#include "md.h"

typedef void (*MdRecompFn)(M68k *c);

typedef struct {
    uint32_t rom_hash;   /* md_rom_hash of the ROM it was made from */
    const char *rom_name;
    MdRecompFn (*lookup)(uint32_t at);  /* the block holding the instruction at `at`; NULL: none */
    const uint8_t *rom;  /* the ROM, embedded; NULL if not */
    size_t rom_size;
} MdRecompProgram;

/* Defined by the generated code; NULL in builds without it (recomp_none.c). */
extern const MdRecompProgram *const md_recomp_program;

void md_z80_catch_up(Md *m);  /* md.c: the Z80 up to the 68000's clock */

/* After an instruction: must the block hand back to the loop? */
#define MD_STOP(m, c) (md_now(m) >= (m)->line_end || (c)->ipl || (c)->ipl_seen || (c)->stopped || (c)->trace)

/* Continuing in the next block: a guaranteed tail call where the compiler
 * has one, else back to the loop, which looks it up. */
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 15)
#define RECOMP_CHAIN(f) __attribute__((musttail)) return f(c)
#else
#define RECOMP_CHAIN(f) return
#endif

#endif
