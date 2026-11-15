/* Interface between the runtime and recompiled code (recomp/emit.c). */
#ifndef NES_RECOMP_RT_H
#define NES_RECOMP_RT_H

#include <stddef.h>
#include <stdint.h>

#include "cpu.h"

/* Runs from c->pc (any instruction start in its block) until the block ends
 * or an interrupt is due; leaves c->pc at the next instruction. */
typedef void (*RecompFn)(Cpu *c);

typedef struct {
    uint32_t prg_hash;      /* FNV-1a of the PRG data the code was made from */
    const char *rom_name;
    /* NULL when there is no recompiled code for pc with this 8 KB PRG bank
     * mapped in pc's window (recomp/analyze.c, banking) */
    RecompFn (*lookup)(uint16_t pc, uint32_t bank);
    const uint8_t *rom;     /* the iNES file it was made from, embedded */
    size_t rom_size;
} RecompProgram;

/* Defined by the generated code; NULL in builds without it (recomp_none.c). */
extern const RecompProgram *const recomp_program;

/* FNV-1a over the PRG data; must match fnv1a() in recomp/emit.c. */
static inline uint32_t recomp_prg_hash(const uint8_t *prg, uint32_t size) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++) h = (h ^ prg[i]) * 16777619u;
    return h;
}

#endif
