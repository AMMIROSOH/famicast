/* Recursive-descent decoding, basic blocks and PRG banking.
 *
 * Instructions are keyed by address, so two decodes may overlap (e.g. the SMB
 * `BIT abs` trick, where the operand bytes are also an instruction reached by
 * a jump). Code that cannot be followed statically (JMP (ind), RTS used as a
 * jump, jumps into RAM) is left to the runtime's interpreter (nes/docs/architecture.md). */
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

Instr decode_at(const uint8_t *mem, uint16_t pc) {
    Instr i = {pc, 0, 0, &OPS[mem[pc]]};
    i.size = i.op->size;
    if (i.size == 2) i.operand = mem[(uint16_t)(pc + 1)];
    else if (i.size == 3) i.operand = (uint16_t)(mem[(uint16_t)(pc + 1)] | mem[(uint16_t)(pc + 2)] << 8);
    return i;
}

int ins_target(const Instr *i) {
    if (is_branch(i->op->mn)) return (uint16_t)(i->pc + 2 + (int8_t)i->operand);
    if ((!strcmp(i->op->mn, "JMP") || !strcmp(i->op->mn, "JSR")) && !strcmp(i->op->mode, "ABS")) return i->operand;
    return -1;
}

static int flow_end(const char *mn) {
    return !strcmp(mn, "JMP") || !strcmp(mn, "RTS") || !strcmp(mn, "RTI") || !strcmp(mn, "BRK") || !strcmp(mn, "KIL");
}

int falls_through(const Instr *i) { return !flow_end(i->op->mn); }

int is_transfer(const Instr *i) {
    return is_branch(i->op->mn) || flow_end(i->op->mn) || !strcmp(i->op->mn, "JSR");
}

void explore(Program *prog, const uint8_t *mem, uint32_t lo, uint32_t hi, const AddrSet *no_return) {
    memset(&prog->instrs, 0, sizeof prog->instrs);
    memset(&prog->indirect, 0, sizeof prog->indirect);
    memset(&prog->external, 0, sizeof prog->external);
    memset(&prog->straddle, 0, sizeof prog->straddle);
    uint16_t *work = malloc(sizeof *work * 0x30000);  /* entries + 2 pushes per decoded pc */
    size_t n = 0;
    for (uint32_t a = 0; a < 0x10000; a++)
        if (prog->entries.b[a]) work[n++] = (uint16_t)a;
    while (n) {
        uint16_t pc = work[--n];
        if (prog->instrs.b[pc]) continue;
        if (pc < lo || pc >= hi) { prog->external.b[pc] = 1; continue; }
        Instr ins = decode_at(mem, pc);
        prog->instrs.b[pc] = 1;
        if ((uint32_t)pc + ins.size > hi) {  /* operand in the next window, maybe another bank */
            prog->straddle.b[pc] = 1;
            continue;
        }
        if (!strcmp(ins.op->mn, "JMP") && !strcmp(ins.op->mode, "IND")) prog->indirect.b[pc] = 1;
        int t = ins_target(&ins);
        if (t >= 0) {
            work[n++] = (uint16_t)t;
            if (!strcmp(ins.op->mn, "JSR")) prog->entries.b[t] = 1;
        }
        if (falls_through(&ins) && !(!strcmp(ins.op->mn, "JSR") && no_return && no_return->b[t]))
            work[n++] = (uint16_t)(pc + ins.size);
    }
    free(work);
}

void block_starts(const Program *prog, const uint8_t *mem, AddrSet *starts) {
    static uint8_t preds[0x10000];
    memset(preds, 0, sizeof preds);
    memcpy(starts, &prog->entries, sizeof *starts);
    for (uint32_t pc = 0; pc < 0x10000; pc++) {
        if (!prog->instrs.b[pc]) continue;
        Instr ins = decode_at(mem, (uint16_t)pc);
        if (prog->straddle.b[pc]) continue;  /* last of its block; operand unknown here */
        int t = ins_target(&ins);
        uint16_t nxt = (uint16_t)(pc + ins.size);
        if (t >= 0 && prog->instrs.b[t]) starts->b[t] = 1;
        if (is_transfer(&ins) && prog->instrs.b[nxt]) starts->b[nxt] = 1;
        /* two decode chains meeting (overlapping instructions) */
        if ((!is_transfer(&ins) || is_branch(ins.op->mn) || !strcmp(ins.op->mn, "JSR")) &&
            falls_through(&ins) && prog->instrs.b[nxt] && preds[nxt] < 2)
            preds[nxt]++;
    }
    for (uint32_t pc = 0; pc < 0x10000; pc++) {
        if (preds[pc] > 1) starts->b[pc] = 1;
        starts->b[pc] &= prog->instrs.b[pc];
    }
}

void chain_starts(const uint8_t *mem, const AddrSet *misses, AddrSet *starts) {
    static AddrSet follows, seen;
    static uint16_t succ[0x10000][2];
    memset(&follows, 0, sizeof follows);
    for (uint32_t pc = 0; pc < 0x10000; pc++) {
        succ[pc][0] = succ[pc][1] = 0;  /* 0 = none ($0000 is never a ROM miss) */
        if (!misses->b[pc]) continue;
        Instr ins = decode_at(mem, (uint16_t)pc);
        int t = ins_target(&ins);
        uint16_t nxt = (uint16_t)(pc + ins.size);
        if (falls_through(&ins) && misses->b[nxt]) succ[pc][0] = nxt, follows.b[nxt] = 1;
        if (t >= 0 && misses->b[t]) succ[pc][1] = (uint16_t)t, follows.b[t] = 1;
    }
    for (uint32_t pc = 0; pc < 0x10000; pc++) starts->b[pc] = misses->b[pc] && !follows.b[pc];
    /* A loop entered from code the decoder cannot see (RAM, another bank) has
     * no start of its own: take its lowest address until all is reachable. */
    uint16_t *work = malloc(sizeof *work * 0x30000);  /* starts + 2 pushes per pc */
    for (;;) {
        memset(&seen, 0, sizeof seen);
        size_t n = 0;
        for (uint32_t pc = 0; pc < 0x10000; pc++)
            if (starts->b[pc]) work[n++] = (uint16_t)pc;
        while (n) {
            uint16_t pc = work[--n];
            if (seen.b[pc]) continue;
            seen.b[pc] = 1;
            for (int k = 0; k < 2; k++)
                if (succ[pc][k]) work[n++] = succ[pc][k];
        }
        uint32_t rest = 0;
        while (rest < 0x10000 && !(misses->b[rest] && !seen.b[rest])) rest++;
        if (rest == 0x10000) break;
        starts->b[rest] = 1;
    }
    free(work);
}

/* ---- banking ----
 * Recompiled code is identified by (8 KB PRG bank, CPU window); window w
 * covers $8000 + w * $2000. The runtime looks code up by the bank mapped at
 * that moment, so the layout below is only where static exploration starts:
 * a wrong guess costs coverage (the interpreter runs that code), never
 * correctness. */

int assumed_layout(const Rom *r, int layout[4]) {
    int n8 = (int)(r->prg_size / 0x2000);
    for (int w = 0; w < 4; w++) layout[w] = -1;
    switch (r->mapper) {
    case 0: case 3: case 87: case 185:  /* no PRG switching (16 KB mirrors: 0,1,0,1) */
    case 13: case 184:
    case 7: case 11: case 66: case 79:  /* 32 KB switching, bank 0 at power-on */
    case 34: case 41: case 77: case 86: case 113: case 140: case 228:
        for (int w = 0; w < 4; w++) layout[w] = w % n8;
        return 0;
    case 180: case 92:           /* 16 KB at $C000 switched, the first at $8000 */
        layout[0] = 0;
        layout[1] = 1;
        return 0;
    case 97:                     /* Irem TAM-S1: the last 16 KB at $8000 */
        layout[0] = n8 - 2;
        layout[1] = n8 - 1;
        return 0;
    case 188: case 232:          /* Karaoke Studio: bank 7 at $C000; Quattro: block 0's last */
        layout[0] = 0;
        layout[1] = 1;
        layout[2] = r->mapper == 188 ? 14 : 6;
        layout[3] = r->mapper == 188 ? 15 : 7;
        return 0;
    case 37: case 47:            /* MMC3 multicarts: outer block 0 at power-on */
        layout[2] = r->mapper == 37 ? 6 : 14;
        layout[3] = r->mapper == 37 ? 7 : 15;
        return 0;
    case 70: case 152: case 72: case 78: case 89: case 93: case 94:  /* 16 KB at $8000, the last fixed */
    case 67: case 68: case 73: case 155:
    case 76: case 88: case 95: case 119: case 154:  /* MMC3, Namco 108 boards */
    case 1: case 2: case 71:     /* MMC1 (mode 3), UxROM, Camerica: last 16 KB fixed */
    case 4: case 118: case 206:  /* MMC3, TxSROM, Namco 108: $E000 fixed, $C000 in mode 0 */
    case 32: case 33: case 48:   /* Irem G-101, Taito TC0190/TC0690: the same */
    case 21: case 22: case 23: case 25:  /* VRC2/VRC4: the same, swap mode off */
        layout[2] = n8 - 2;
        layout[3] = n8 - 1;
        return 0;
    case 153:                    /* Bandai with 512 KB: the first half at power-on */
        layout[2] = (n8 < 32 ? n8 : 32) - 2;
        layout[3] = (n8 < 32 ? n8 : 32) - 1;
        return 0;
    case 10:                     /* MMC4: 16 KB at $8000, the last fixed */
    case 16: case 157: case 159: /* Bandai FCG */
        layout[2] = n8 - 2;
        layout[3] = n8 - 1;
        return 0;
    case 9:                      /* MMC2: the last three 8 KB fixed */
        for (int w = 1; w < 4; w++) layout[w] = n8 - 4 + w;
        return 0;
    case 5: case 19: case 210:   /* MMC5 at power-on ($5117 = $FF), Namco 163/175/340: $E000 = last bank */
    case 18: case 69: case 75:   /* SS88006, FME-7, VRC1 */
    case 80: case 207:           /* Taito X1-005 */
    case 24: case 26:            /* VRC6: $8000 16 KB and $C000 8 KB switchable */
    case 85:                     /* VRC7 */
    case 64: case 158: case 65: case 82:  /* RAMBO-1, Irem H3001, Taito X1-017 */
        layout[3] = n8 - 1;
        return 0;
    case 20:                     /* FDS BIOS at $E000; $6000-$DFFF is RAM */
        layout[3] = 0;
        return 0;
    }
    return -1;
}

/* How many 8 KB windows switch together: code may run across windows inside
 * one unit (consecutive banks), never across units. */
int unit_windows(const Rom *r) {
    int m = r->mapper;
    switch (m) {
    case 7: case 11: case 66: case 79: case 34: case 41: case 77: case 86: case 113: case 140:
        return 4;                               /* 32 KB */
    case 1: case 2: case 10: case 71: case 16: case 153: case 157: case 159: case 67: case 68: case 70:
    case 72: case 73: case 78: case 89: case 92: case 93: case 94: case 97: case 152: case 155: case 180:
    case 188: case 228: case 232:
        return 2;                               /* 16 KB (MMC1 32 KB mode = two units) */
    }
    return 1;                                   /* MMC3, Namco 108, VRC: 8 KB windows independent */
}

int prg_bank_reg(const Rom *r) {
    switch (r->mapper) {
    case 0: case 3: case 20: case 87: case 185: case 13: case 184: return 0;
    case 79: case 113: return 0x4100;
    case 34: case 37: case 41: case 47: case 82: case 86: case 140: return 0x6000;  /* registers at $6000-$7FFF */
    case 5: return 0x5100;
    case 16: return 0x6000;  /* FCG-1/2 registers at $6000-$7FFF */
    case 80: case 207: return 0x6000;  /* X1-005 registers at $7EF0-$7EFF */
    default: return 0x8000;
    }
}

void view(const Rom *r, const int layout[4], uint8_t *mem) {
    memset(mem, 0, 0x10000);
    for (int w = 0; w < 4; w++)
        if (layout[w] >= 0 && (uint32_t)(layout[w] + 1) * 0x2000 <= r->prg_size)
            memcpy(mem + 0x8000 + w * 0x2000, r->prg + layout[w] * 0x2000, 0x2000);
}
