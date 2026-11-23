/* Makes a .sms of legal Z80 instructions for the recompiler's lockstep test:
 * the base, CB, ED, DD and FD tables, memory reads and writes included. An
 * instruction that writes through HL, BC, DE, IX, IY or (nn) is preceded by
 * a load that points it into work RAM, so a random value never writes the
 * mapper registers. Left out: unconditional jumps, calls and returns (the body is a
 * straight run, looped by a JP at the end), anything that moves SP, HALT,
 * DI/EI, the repeating block instructions and DD CB / FD CB. Conditional
 * branches (JR cc, DJNZ, JP cc) jump forward over a few NOPs. No expected
 * output: the recompiled and interpreted runs have to agree
 * (sms_headless --diff). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "template.h"
#include "z80.h"

#define ROM_SIZE 0x4000          /* one page in slot 0 */
#define LOOP 0x0100
#define LOOP_END 0x3FE0

static uint32_t seed = 0x1234567u;
static uint32_t rnd(void) {
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return seed;
}

static const char *const NOT[] = {"PUSH", "POP", "LD SP", "INC SP", "DEC SP", "EX (SP)", "HALT", "DI", "EI", NULL};

static int usable(int table, int op, Lines *l, char *name, size_t len) {
    if (!z80_body(table, op, l, name, len)) return 0;  /* prefixes, and DD/FD opcodes run as base ones */
    for (int k = 0; NOT[k]; k++)
        if (!strncmp(name, NOT[k], strlen(NOT[k]))) return 0;
    for (int k = 0; k < l->n; k++)
        if (strstr(l->s[k], "@J") || strstr(l->s[k], "@B")) return 0;  /* jumps, calls, returns, LDIR and friends */
    return 1;
}

static unsigned ram(void) { return 0xC000 + (rnd() & 0x1E00) + (rnd() & 0xFF); }  /* $C000-$DFFF, IX/IY+d too */

static unsigned at;
static uint8_t rom[ROM_SIZE];
static void put(unsigned v) { rom[at++] = (uint8_t)v; }
static void put16(unsigned v) { put(v), put(v >> 8); }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: sms_random_rom OUT.sms\n"); return 2; }
    memset(rom, 0xFF, sizeof rom);
    at = 0, put(0xC3), put16(LOOP);       /* reset: JP $0100 */
    at = 0x38, put(0xC3), put16(LOOP);    /* RST $38 */
    at = 0x66, put(0xC3), put16(LOOP);    /* NMI */
    at = LOOP;
    put(0xF3);                            /* DI */
    put(0xED), put(0x56);                 /* IM 1 */
    put(0x31), put16(0xDFF0);             /* LD SP,$DFF0 */
    static const uint8_t PREFIX[] = {0, 0xCB, 0xED, 0xDD, 0xFD};
    unsigned count = 0;
    while (at + 16 < LOOP_END) {
        if (rnd() % 12 == 0) {  /* a conditional branch forward over 1-3 NOPs: taken or not, the two paths differ */
            static const uint8_t BR[] = {0x20, 0x28, 0x30, 0x38, 0x10, 0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA};
            uint8_t op = BR[rnd() % sizeof BR];
            unsigned skip = 1 + rnd() % 3;
            put(op);
            if (op < 0xC0) put(skip);                  /* JR cc,e / DJNZ e */
            else put16(at + 2 + skip);                 /* JP cc,nn: just past the NOPs */
            while (skip--) put(0x00);
            count++;
            continue;
        }
        int table = (int)(rnd() % 8);
        table = table < 4 ? Z80_T_BASE : table - 3;  /* half base, the rest CB, ED, DD, FD (1-4) */
        Lines l;
        char name[64];
        int op;
        do op = (int)(rnd() & 0xFF); while (!usable(table, op, &l, name, sizeof name));
        /* point the registers it addresses memory through into RAM */
        if (strstr(name, "(HL)") || !strncmp(name, "LDI", 3) || !strncmp(name, "LDD", 3) || !strncmp(name, "INI", 3) ||
            !strncmp(name, "IND", 3) || !strncmp(name, "OUTI", 4) || !strncmp(name, "OUTD", 4) || !strncmp(name, "CPI", 3) ||
            !strncmp(name, "CPD", 3) || !strcmp(name, "RLD") || !strcmp(name, "RRD"))
            put(0x21), put16(ram());      /* LD HL,ram */
        if (strstr(name, "(DE)") || !strncmp(name, "LDI", 3) || !strncmp(name, "LDD", 3)) put(0x11), put16(ram());
        if (strstr(name, "(BC)")) put(0x01), put16(ram());
        if (strstr(name, "(IX")) put(0xDD), put(0x21), put16(ram() - 0x80);
        if (strstr(name, "(IY")) put(0xFD), put(0x21), put16(ram() - 0x80);
        if (PREFIX[table]) put(PREFIX[table]);
        put((unsigned)op);
        /* operand bytes (@F, @R): random, but (nn) is a RAM address */
        unsigned n = 0;
        for (int k = 0; k < l.n; k++)
            for (const char *p = l.s[k]; *p; p++)
                if (p[0] == '@' && (p[1] == 'F' || p[1] == 'R')) n++;
        if (strstr(name, "(nn)") && n >= 2) put16(ram() & 0xDFFE), n -= 2;
        while (n--) put(rnd());
        count++;
    }
    put(0xC3), put16(LOOP);               /* loop forever */
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(rom, 1, ROM_SIZE, f) != ROM_SIZE || fclose(f)) { perror(argv[1]); return 1; }
    fprintf(stderr, "sms_random_rom: %u instructions\n", count);
    return 0;
}
