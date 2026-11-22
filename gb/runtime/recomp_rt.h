/* Interface between the Game Boy runtime and recompiled code (gb/recomp).
 * A block runs from c->pc (any instruction of it) and returns after the
 * instruction that ends it, or as soon as the interpreter would do
 * something else before the next instruction: an interrupt is due, or the
 * frame is over (gb_run_frame). So recompiled and interpreted runs take
 * the same steps, cycle for cycle (gb_headless --diff checks it). */
#ifndef GB_RECOMP_RT_H
#define GB_RECOMP_RT_H

#include <stddef.h>
#include <stdint.h>

#include "sm83_cpu.h"

typedef void (*GbRecompFn)(GbCpu *c);

typedef struct {
    uint32_t rom_hash;   /* FNV-1a of the ROM it was made from (gb_rom_hash) */
    const char *rom_name;
    /* the block holding pc with ROM bank `bank` mapped at pc; NULL: none */
    GbRecompFn (*lookup)(uint16_t pc, uint32_t bank);
    const uint8_t *rom;  /* the ROM, embedded; NULL if not */
    size_t rom_size;
} GbRecompProgram;

/* Defined by the generated code; NULL in builds without it (recomp_none.c). */
extern const GbRecompProgram *const gb_recomp_program;

/* After an instruction: must the block hand back to gb_step? */
#define RECOMP_STOP(g) ((g)->cpu.cycles >= (g)->frame_end || ((g)->cpu.ime && ((g)->ie & (g)->ifl & 0x1F)))

/* Continuing in the next block: a guaranteed tail call where the compiler
 * has one, else back to gb_step, which looks it up. */
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 15)
#define RECOMP_CHAIN(f) __attribute__((musttail)) return f(c)
#else
#define RECOMP_CHAIN(f) return
#endif

#endif
