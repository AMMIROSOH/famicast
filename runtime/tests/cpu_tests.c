/* CPU test runner on a flat 64 KB bus (driven by tests/test_cpu.py).
 *
 *   cpu_tests nestest ROM N   print the state before each of N instructions,
 *                             starting at $C000 (nestest automation mode)
 *   cpu_tests nestest-recomp ROM MAXCYC
 *                             same, but through the linked recompiled code;
 *                             prints the state after every block until MAXCYC
 *   cpu_tests sst FILE...     run SingleStepTests files converted by
 *                             tests/fetch_test_roms.py; one summary line each
 *
 * SST binary format (little endian): u32 count; per test: initial state,
 * final state, u16 ncycles, ncycles x (u16 addr, u8 value, u8 is_write).
 * State: u16 pc, u8 s, a, x, y, p, u16 nram, nram x (u16 addr, u8 value).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "recomp_rt.h"

static uint8_t mem[0x10000];
typedef struct { uint16_t addr; uint8_t value, is_write; } Access;
static Access log_[64];
static int log_n;

uint8_t cpu_read(Cpu *c, uint16_t a) {
    (void)c;
    if (log_n < 64) log_[log_n++] = (Access){a, mem[a], 0};
    return mem[a];
}

void cpu_write(Cpu *c, uint16_t a, uint8_t v) {
    (void)c;
    if (log_n < 64) log_[log_n++] = (Access){a, v, 1};
    mem[a] = v;
}

static uint8_t *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(*size);
    if (fread(buf, 1, *size, f) != (size_t)*size) { perror(path); exit(2); }
    fclose(f);
    return buf;
}

static int nestest(const char *rom, long steps, int recomp) {
    long size;
    uint8_t *d = read_file(rom, &size);
    if (size < 16 + 0x4000 || memcmp(d, "NES\x1a", 4) != 0) { fprintf(stderr, "bad ROM\n"); return 2; }
    memcpy(mem + 0x8000, d + 16, 0x4000);
    memcpy(mem + 0xC000, d + 16, 0x4000);
    Cpu c = {0};
    cpu_reset(&c);
    c.pc = 0xC000;  /* automation mode entry */
    if (recomp) {  /* steps = last cycle count to run to */
        if (!recomp_program || recomp_program->prg_hash != recomp_prg_hash(d + 16, 0x4000)) {
            fprintf(stderr, "no recompiled code for this ROM\n");
            return 2;
        }
        while (c.cycles <= (uint64_t)steps && !c.jammed) {
            printf("%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%" PRIu64 "\n",
                   c.pc, c.a, c.x, c.y, c.p, c.s, c.cycles);
            RecompFn f = c.poll_prev ? NULL : recomp_program->lookup(c.pc);
            if (f) f(&c);
            else cpu_step(&c);
        }
        return 0;
    }
    for (long i = 0; i < steps; i++) {
        printf("%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%" PRIu64 "\n",
               c.pc, c.a, c.x, c.y, c.p, c.s, c.cycles);
        cpu_step(&c);
    }
    return 0;
}

typedef struct { uint16_t pc; uint8_t s, a, x, y, p; uint16_t nram; const uint8_t *ram; } State;

static const uint8_t *read_state(const uint8_t *q, State *st) {
    st->pc = q[0] | q[1] << 8;
    st->s = q[2]; st->a = q[3]; st->x = q[4]; st->y = q[5]; st->p = q[6];
    st->nram = q[7] | q[8] << 8;
    st->ram = q + 9;
    return q + 9 + 3 * st->nram;
}

static int sst_file(const char *path) {
    long size;
    uint8_t *d = read_file(path, &size);
    const uint8_t *q = d + 4;
    uint32_t n = d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24;
    int fails = 0;
    for (uint32_t t = 0; t < n; t++) {
        State in, out;
        q = read_state(q, &in);
        q = read_state(q, &out);
        int ncyc = q[0] | q[1] << 8;
        const uint8_t *cyc = q + 2;
        q = cyc + 4 * ncyc;

        for (int i = 0; i < in.nram; i++) mem[in.ram[3 * i] | in.ram[3 * i + 1] << 8] = in.ram[3 * i + 2];
        Cpu c = {.pc = in.pc, .s = in.s, .a = in.a, .x = in.x, .y = in.y, .p = in.p};
        log_n = 0;
        cpu_step(&c);

        char why[160] = "";
        if (c.pc != out.pc || c.s != out.s || c.a != out.a || c.x != out.x || c.y != out.y || c.p != out.p)
            snprintf(why, sizeof why, "regs pc=%04X s=%02X a=%02X x=%02X y=%02X p=%02X, want %04X %02X %02X %02X %02X %02X",
                     c.pc, c.s, c.a, c.x, c.y, c.p, out.pc, out.s, out.a, out.x, out.y, out.p);
        for (int i = 0; !why[0] && i < out.nram; i++) {
            uint16_t a = out.ram[3 * i] | out.ram[3 * i + 1] << 8;
            if (mem[a] != out.ram[3 * i + 2])
                snprintf(why, sizeof why, "ram[%04X]=%02X want %02X", a, mem[a], out.ram[3 * i + 2]);
        }
        if (!why[0] && (log_n != ncyc || (int)c.cycles != ncyc))
            snprintf(why, sizeof why, "%d bus cycles, want %d", log_n, ncyc);
        for (int i = 0; !why[0] && i < ncyc; i++) {
            Access want = {cyc[4 * i] | cyc[4 * i + 1] << 8, cyc[4 * i + 2], cyc[4 * i + 3]};
            if (memcmp(&log_[i], &want, sizeof want) != 0)
                snprintf(why, sizeof why, "cycle %d: %s %04X=%02X, want %s %04X=%02X", i + 1,
                         log_[i].is_write ? "write" : "read", log_[i].addr, log_[i].value,
                         want.is_write ? "write" : "read", want.addr, want.value);
        }
        if (why[0] && fails++ < 3) printf("  FAIL test %u: %s\n", t, why);

        for (int i = 0; i < in.nram; i++) mem[in.ram[3 * i] | in.ram[3 * i + 1] << 8] = 0;
        for (int i = 0; i < out.nram; i++) mem[out.ram[3 * i] | out.ram[3 * i + 1] << 8] = 0;
    }
    printf("SST %s pass %u fail %d\n", path, n - fails, fails);
    free(d);
    return fails != 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "nestest")) return nestest(argv[2], atol(argv[3]), 0);
    if (argc == 4 && !strcmp(argv[1], "nestest-recomp")) return nestest(argv[2], atol(argv[3]), 1);
    if (argc >= 3 && !strcmp(argv[1], "sst")) {
        int bad = 0;
        for (int i = 2; i < argc; i++) bad |= sst_file(argv[i]);
        return bad;
    }
    fprintf(stderr, "usage: cpu_tests nestest ROM N | cpu_tests sst FILE...\n");
    return 2;
}
