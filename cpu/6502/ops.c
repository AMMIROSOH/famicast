/* 2A03 (NES 6502) opcode table: all 256 opcodes.
 *
 * Laid out as 16x16 grids (row = high nibble, column = low nibble), the same
 * shape as the nesdev wiki "CPU unofficial opcodes" matrix, so it can be
 * checked by eye. Checked by tests/nes/test_ops.py against radare2's tables
 * (tests/nes/data/r2_6502.json); cycle counts against nestest.log and the
 * SingleStepTests suite (nes/docs/verification-log.md, M2).
 *
 * Unofficial names follow nesdev/nestest: SLO RLA SRE RRA SAX LAX DCP ISB ANC
 * ALR ARR XAA AXS AHX SHY SHX TAS LAS KIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c6502.h"

static const char MNEMONICS[] =
    "BRK ORA KIL SLO NOP ORA ASL SLO PHP ORA ASL ANC NOP ORA ASL SLO "
    "BPL ORA KIL SLO NOP ORA ASL SLO CLC ORA NOP SLO NOP ORA ASL SLO "
    "JSR AND KIL RLA BIT AND ROL RLA PLP AND ROL ANC BIT AND ROL RLA "
    "BMI AND KIL RLA NOP AND ROL RLA SEC AND NOP RLA NOP AND ROL RLA "
    "RTI EOR KIL SRE NOP EOR LSR SRE PHA EOR LSR ALR JMP EOR LSR SRE "
    "BVC EOR KIL SRE NOP EOR LSR SRE CLI EOR NOP SRE NOP EOR LSR SRE "
    "RTS ADC KIL RRA NOP ADC ROR RRA PLA ADC ROR ARR JMP ADC ROR RRA "
    "BVS ADC KIL RRA NOP ADC ROR RRA SEI ADC NOP RRA NOP ADC ROR RRA "
    "NOP STA NOP SAX STY STA STX SAX DEY NOP TXA XAA STY STA STX SAX "
    "BCC STA KIL AHX STY STA STX SAX TYA STA TXS TAS SHY STA SHX AHX "
    "LDY LDA LDX LAX LDY LDA LDX LAX TAY LDA TAX LAX LDY LDA LDX LAX "
    "BCS LDA KIL LAX LDY LDA LDX LAX CLV LDA TSX LAS LDY LDA LDX LAX "
    "CPY CMP NOP DCP CPY CMP DEC DCP INY CMP DEX AXS CPY CMP DEC DCP "
    "BNE CMP KIL DCP NOP CMP DEC DCP CLD CMP NOP DCP NOP CMP DEC DCP "
    "CPX SBC NOP ISB CPX SBC INC ISB INX SBC NOP SBC CPX SBC INC ISB "
    "BEQ SBC KIL ISB NOP SBC INC ISB SED SBC NOP ISB NOP SBC INC ISB ";

static const char MODES[] =
    "IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
    "ABS IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
    "IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
    "IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM IND ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
    "IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY "
    "IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY "
    "IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
    "IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS "
    "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX ";

/* Base cycles; KIL = 0 (CPU jams). Page-cross and branch penalties are added
 * at run time (page_penalty below). */
static const char CYCLES[] =
    "7 6 0 8 3 3 5 5 3 2 2 2 4 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 "
    "6 6 0 8 3 3 5 5 4 2 2 2 4 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 "
    "6 6 0 8 3 3 5 5 3 2 2 2 3 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 "
    "6 6 0 8 3 3 5 5 4 2 2 2 5 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 "
    "2 6 2 6 3 3 3 3 2 2 2 2 4 4 4 4 "
    "2 6 0 6 4 4 4 4 2 5 2 5 5 5 5 5 "
    "2 6 2 6 3 3 3 3 2 2 2 2 4 4 4 4 "
    "2 5 0 5 4 4 4 4 2 4 2 4 4 4 4 4 "
    "2 6 2 8 3 3 5 5 2 2 2 2 4 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 "
    "2 6 2 8 3 3 5 5 2 2 2 2 4 4 6 6 "
    "2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7 ";

Op OPS[256];

static int in_list(const char *s, const char *list) {  /* list: space-separated */
    size_t n = strlen(s);
    for (const char *p = list; (p = strstr(p, s)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return 1;
    return 0;
}

int is_branch(const char *mn) { return in_list(mn, "BPL BMI BVC BVS BCC BCS BNE BEQ"); }

/* Read instructions pay +1 cycle when ABX/ABY/IZY indexing crosses a page.
 * Stores and read-modify-write always take the longer time (in CYCLES). */
int page_penalty(const Op *op) {
    return in_list(op->mode, "ABX ABY IZY") && in_list(op->mn, "ADC AND CMP EOR LDA LDX LDY ORA SBC LAX LAS NOP");
}

static int mode_size(const char *mode) {
    if (in_list(mode, "IMP ACC")) return 1;
    if (in_list(mode, "ABS ABX ABY IND")) return 3;
    return 2;
}

void ops_init(void) {
    const char *m = MNEMONICS, *d = MODES, *c = CYCLES;
    for (int i = 0; i < 256; i++) {
        Op *op = &OPS[i];
        int n1, n2, n3, cycles;
        if (sscanf(m, "%3s%n", op->mn, &n1) != 1 || sscanf(d, "%3s%n", op->mode, &n2) != 1 ||
            sscanf(c, "%d%n", &cycles, &n3) != 1) {
            fprintf(stderr, "opcode table broken at $%02X\n", i);
            exit(1);
        }
        m += n1, d += n2, c += n3;
        op->code = (uint8_t)i;
        op->cycles = (uint8_t)cycles;
        op->size = (uint8_t)mode_size(op->mode);
        op->official = !(in_list(op->mn, "SLO RLA SRE RRA SAX LAX DCP ISB ANC ALR ARR XAA AXS AHX SHY SHX TAS LAS KIL") ||
                         (!strcmp(op->mn, "NOP") && i != 0xEA) || (!strcmp(op->mn, "SBC") && i == 0xEB));
    }
}
