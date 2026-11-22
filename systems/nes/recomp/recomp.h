/* nesrecomp: 6502 -> C static recompiler (docs/architecture.md). Build-time tool, plain
 * C11, no dependencies beyond runtime/cart.c (iNES loading and banking). */
#ifndef NES_RECOMP_H
#define NES_RECOMP_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "c6502.h"    /* Op, OPS, body (cpu/6502); Lines, Buf (core/recomp) */

/* ---- analyze.c: decoding, exploration, basic blocks ---- */
typedef struct {
    uint16_t pc, operand;
    uint8_t size;
    const Op *op;
} Instr;

Instr decode_at(const uint8_t *mem, uint16_t pc);
int ins_target(const Instr *i);        /* static branch/JMP abs/JSR target, else -1 */
int falls_through(const Instr *i);
int is_transfer(const Instr *i);       /* ends a basic block */

/* Bit sets over the 64 KB address space. */
typedef struct { uint8_t b[0x10000]; } AddrSet;

typedef struct {
    AddrSet instrs, entries, indirect, external;
    AddrSet straddle;  /* operand bytes in the next window: read at run time, flow not followed */
} Program;

/* Follow statically known control flow from prog->entries within [lo, hi).
 * no_return: subroutines that never return to the caller. */
void explore(Program *prog, const uint8_t *mem, uint32_t lo, uint32_t hi, const AddrSet *no_return);
/* Marks in `starts` where each basic block begins; every instruction then
 * belongs to the block of the closest start at or before it on its chain. */
void block_starts(const Program *prog, const uint8_t *mem, AddrSet *starts);
/* Missed ROM addresses that no other miss reaches statically; loops entered
 * from elsewhere (RAM, another bank) get their lowest address. */
void chain_starts(const uint8_t *mem, const AddrSet *misses, AddrSet *starts);

/* ---- banks.c (in analyze.c): PRG banking, see systems/nes/docs/architecture.md "Banked mappers" ---- */
typedef struct {
    const uint8_t *prg;
    uint32_t prg_size;
    int mapper;
} Rom;

int assumed_layout(const Rom *r, int layout[4]);  /* -1 = window unknown; returns 0 or -1 */
int unit_windows(const Rom *r);
/* Lowest CPU address of a PRG bank register (a store there may switch the
 * code being run); 0 if PRG never switches. Matches cart_is_reg (cart.h):
 * 79 at $4100, MMC5 at $5100, the rest at $8000. */
int prg_bank_reg(const Rom *r);
static inline int window_of(uint32_t a) { return (int)((a - 0x8000) >> 13); }
void view(const Rom *r, const int layout[4], uint8_t *mem /* 64 KB */);

/* ---- emit.c ---- */
uint32_t fnv1a(const uint8_t *d, size_t n);

typedef struct {
    Program *prog;
    uint8_t *mem;      /* 64 KB view */
    int banks[4];      /* window -> 8 KB bank, -1 if not part of the region */
    /* ram: an FDS disk file (number `file`), loaded at [lo, hi); its blocks
     * check that RAM holds their bytes (recomp_rt.h) */
    int ram, file;
    uint32_t lo, hi;
} Region;

/* dispatch_at: an address recompiled code never chains to, so the runtime's
 * dispatcher sees it (the FDS BIOS disk check at $E445, nes.c); 0: none.
 * bios: the FDS BIOS to embed, or NULL. */
void emit_program(Buf *out, Region *regions, int nregions, const char *rom_name, uint32_t prg_hash,
                  int nbanks, int bank_reg, const uint8_t *rom, size_t rom_size, uint16_t dispatch_at,
                  const uint8_t *bios);

#endif
