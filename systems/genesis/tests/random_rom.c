/* random_rom OUT.md [SEED]: a 64 KB ROM of random 68000 instructions, for
 * the recompiler's lockstep test in CI (tests/genesis/test_recomp.py; no
 * game ROMs there). Whatever the code does, the recompiled machine must do
 * the same as the interpreter (genesis_headless --diff), so it needs no
 * expected results.
 *
 * The loop at $400: random legal opcodes whose templates do not change
 * the PC (so a block runs many of them), with random extension words,
 * some behind a Bcc.S over them (random condition); a JMP back to the
 * start. Every few instructions a checkpoint sets SP again (a random
 * instruction may move it) and stores the next checkpoint's address at
 * $FF8000. Every exception vector leads to $300: interrupts masked, SP
 * set, on at that address (made even and inside the loop), so a fault
 * skips the rest of its segment, not the whole loop. STOP is left out
 * (nothing would wake it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"

static uint8_t rom[0x10000];
static uint32_t at;
static uint32_t seed = 1;

static uint32_t rnd(void) {  /* xorshift32 */
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return seed;
}
static void w16(uint32_t a, unsigned v) { rom[a] = (uint8_t)(v >> 8), rom[a + 1] = (uint8_t)v; }
static void put(unsigned v) { w16(at, v), at += 2; }
static void put32(uint32_t v) { put(v >> 16), put(v & 0xFFFF); }

static int usable(uint16_t op) {
    Lines l;
    if (op == 0x4E72 || !m68k_body(op, &l)) return 0;
    for (int k = 0; k < l.n; k++)
        if (strstr(l.s[k], "@J") || strstr(l.s[k], "@B")) return 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: random_rom OUT.md [SEED]\n"); return 2; }
    if (argc > 2) seed = (uint32_t)strtoul(argv[2], NULL, 0) | 1;
    for (uint32_t v = 2; v < 64; v++) w16(v * 4, 0), w16(v * 4 + 2, 0x300);  /* every exception: $300 */
    w16(0, 0x00FF), w16(2, 0xFE00), w16(4, 0), w16(6, 0x400);                /* SSP $FFFE00, PC $400 */
    memcpy(rom + 0x100, "SEGA GENESIS    ", 16);
    memcpy(rom + 0x1F0, "U", 1);
    at = 0x300;
    put(0x46FC), put(0x2700);                 /* MOVE.W #$2700,SR */
    put(0x2E7C), put32(0x00FFFE00);           /* MOVEA.L #$FFFE00,A7 */
    put(0x2039), put32(0x00FF8000);           /* MOVE.L $FF8000,D0 */
    put(0x0280), put32(0x0000FFFE);           /* ANDI.L #$FFFE,D0 */
    put(0x0C40), put(0x0400);                 /* CMPI.W #$400,D0 */
    put(0x6404);                              /* BCC.S over the next */
    put(0x303C), put(0x0400);                 /* MOVE.W #$400,D0 */
    put(0x2040);                              /* MOVEA.L D0,A0 */
    put(0x4ED0);                              /* JMP (A0) */
    at = 0x400;
    uint32_t patch = 0;  /* where the last checkpoint's address operand is */
    for (int n = 0; at < 0xF000; n++) {
        if (n % 4 == 0) {  /* a checkpoint: MOVEA.L #$FFFE00,A7; MOVE.L #next,$FF8000 */
            if (patch) w16(patch, at >> 16), w16(patch + 2, at & 0xFFFF);
            put(0x2E7C), put32(0x00FFFE00);
            put(0x23FC), patch = at, put32(0x400), put32(0x00FF8000);
        }
        uint16_t op;
        do op = (uint16_t)rnd(); while (!usable(op));
        if (rnd() % 3 == 0)  /* Bcc.S over the instruction below */
            put(0x6000 | (2 + rnd() % 14) << 8 | ((unsigned)m68k_ext_words(op) * 2 + 2));
        put(op);
        for (int k = 0; k < m68k_ext_words(op); k++) put(rnd() & 0xFFFF);
    }
    put(0x4EF9), put32(0x400);                /* JMP $400 */
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(rom, 1, sizeof rom, f) != sizeof rom || fclose(f)) { perror(argv[1]); return 2; }
    return 0;
}
