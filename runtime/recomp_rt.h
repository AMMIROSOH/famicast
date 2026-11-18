/* Interface between the runtime and recompiled code (recomp/emit.c). */
#ifndef NES_RECOMP_RT_H
#define NES_RECOMP_RT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cpu.h"

/* Runs from c->pc (any instruction start in its block) until the block ends
 * or an interrupt is due; leaves c->pc at the next instruction. */
typedef void (*RecompFn)(Cpu *c);

/* Code recompiled from a file of a Famicom Disk System disk: it runs from
 * RAM, where other files may be loaded later, so a block runs only while RAM
 * holds the bytes it was made from (checked on entry, and after stores that
 * could change them). */
typedef struct {
    RecompFn fn;
    const uint8_t *bytes;
    uint16_t start, len;
} RecompRamBlock;

typedef struct {
    uint16_t pc;     /* an instruction of the block; sorted by pc */
    uint32_t block;  /* index in ram_blocks */
} RecompRamEntry;

typedef struct {
    uint32_t prg_hash;      /* FNV-1a of the PRG data the code was made from (FDS: the BIOS) */
    const char *rom_name;
    /* NULL when there is no recompiled code for pc with this 8 KB PRG bank
     * mapped in pc's window (recomp/analyze.c, banking) */
    RecompFn (*lookup)(uint16_t pc, uint32_t bank);
    const uint8_t *rom;     /* the iNES file or .fds image it was made from, embedded */
    size_t rom_size;
    const uint8_t *bios;    /* FDS: the BIOS (8 KB), embedded */
    const RecompRamBlock *ram_blocks;
    const RecompRamEntry *ram_entries;
    size_t ram_entries_n;
} RecompProgram;

/* Does RAM at a hold the n bytes k? */
static inline int recomp_ram_is(const Cpu *c, uint16_t a, const uint8_t *k, unsigned n) {
    for (unsigned i = 0; i < n;) {
        const uint8_t *page = c->read_map[(uint16_t)(a + i) >> 11];
        unsigned off = (uint16_t)(a + i) & 0x7FF, len = 0x800 - off < n - i ? 0x800 - off : n - i;
        if (!page || memcmp(page + off, k + i, len)) return 0;
        i += len;
    }
    return 1;
}

/* A block ends by continuing in the next block when it is known statically,
 * with a guaranteed tail call (no stack growth), unless an interrupt is due
 * or the dispatcher wants control (c->yield). Compilers without musttail
 * return to the dispatcher instead. */
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 15)
#define RECOMP_CHAIN(f) __attribute__((musttail)) return f(c)
#else
#define RECOMP_CHAIN(f) return
#endif

/* Defined by the generated code; NULL in builds without it (recomp_none.c). */
extern const RecompProgram *const recomp_program;

/* FNV-1a over the PRG data; must match fnv1a() in recomp/emit.c. */
static inline uint32_t recomp_prg_hash(const uint8_t *prg, uint32_t size) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++) h = (h ^ prg[i]) * 16777619u;
    return h;
}

#endif
