/* SM83 test runner on a flat 64 KB bus (driven by tests/test_gb_cpu.py).
 *
 *   gb_cpu_tests sst FILE...      run SingleStepTests sm83 files converted by
 *                                 tests/fetch_test_roms.py: registers, RAM
 *                                 and every M-cycle (read, write or internal,
 *                                 with address and value); a line per file
 *   gb_cpu_tests sst-end FILE...  the same without the cycles (HALT, STOP:
 *                                 what they do next is up to the system)
 *
 * Binary format (little endian): u32 count; per test: initial state, final
 * state, u16 ncycles, ncycles x (u16 addr, u8 value, u8 kind: 0 internal,
 * 1 read, 2 write). State: u16 pc, sp, u8 a f b c d e h l ime ei, u16 nram,
 * nram x (u16 addr, u8 value). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"

static uint8_t mem[0x10000];
typedef struct { uint16_t addr; uint8_t value, kind; } Access;
enum { MAX_LOG = 16 };
static Access log_[MAX_LOG];
static int log_n;

uint8_t gb_read(GbCpu *c, uint16_t a) {
    (void)c;
    if (log_n < MAX_LOG) log_[log_n] = (Access){a, mem[a], 1};
    log_n++;
    return mem[a];
}
void gb_write(GbCpu *c, uint16_t a, uint8_t v) {
    (void)c;
    if (log_n < MAX_LOG) log_[log_n] = (Access){a, v, 2};
    log_n++;
    mem[a] = v;
}
uint16_t gb_irq_ack(GbCpu *c) { (void)c; return 0; }  /* no interrupts on the test bus */
void gb_idle(GbCpu *c) {
    (void)c;
    if (log_n < MAX_LOG) log_[log_n] = (Access){0, 0, 0};
    log_n++;
}

static const uint8_t *p_;
static unsigned u8(void) { return *p_++; }
static unsigned u16(void) { unsigned v = p_[0] | p_[1] << 8; p_ += 2; return v; }

typedef struct {
    GbCpu cpu;
    unsigned nram;
    const uint8_t *ram;  /* nram x (u16 addr, u8 value) */
} State;

static State state(void) {
    State s = {0};
    s.cpu.pc = (uint16_t)u16(), s.cpu.sp = (uint16_t)u16();
    uint8_t *r[8] = {&s.cpu.a, &s.cpu.f, &s.cpu.b, &s.cpu.c, &s.cpu.d, &s.cpu.e, &s.cpu.h, &s.cpu.l};
    for (int i = 0; i < 8; i++) *r[i] = (uint8_t)u8();
    s.cpu.ime = (uint8_t)u8(), s.cpu.ei = (uint8_t)u8();
    s.nram = u16();
    s.ram = p_;
    p_ += s.nram * 3;
    return s;
}

static const char *const NAMES[] = {"pc", "sp", "a", "f", "b", "c", "d", "e", "h", "l", "ime", "ei"};
static void regs(const GbCpu *c, unsigned out[12]) {
    unsigned v[12] = {c->pc, c->sp, c->a, c->f, c->b, c->c, c->d, c->e, c->h, c->l, c->ime, c->ei};
    memcpy(out, v, sizeof v);
}

/* 0 if the test passes, else prints why (the first difference). */
static int run_test(unsigned index, int cycles) {
    State in = state(), want = state();
    unsigned n = u16();
    const uint8_t *acc = p_;
    p_ += n * 4;
    for (unsigned i = 0; i < in.nram; i++) mem[in.ram[i * 3] | in.ram[i * 3 + 1] << 8] = in.ram[i * 3 + 2];
    GbCpu c = in.cpu;
    log_n = 0;
    gb_cpu_step(&c);
    unsigned got[12], exp[12];
    regs(&c, got);
    regs(&want.cpu, exp);
    for (int i = 0; i < 12; i++)
        if (got[i] != exp[i]) return printf("test %u: %s = %X, want %X\n", index, NAMES[i], got[i], exp[i]), 1;
    for (unsigned i = 0; i < want.nram; i++) {
        unsigned a = want.ram[i * 3] | want.ram[i * 3 + 1] << 8;
        if (mem[a] != want.ram[i * 3 + 2])
            return printf("test %u: [%04X] = %02X, want %02X\n", index, a, mem[a], want.ram[i * 3 + 2]), 1;
    }
    if (!cycles) return 0;
    if ((unsigned)log_n != n) return printf("test %u: %d cycles, want %u\n", index, log_n, n), 1;
    if (c.cycles != n) return printf("test %u: cycles counted %u, want %u\n", index, (unsigned)c.cycles, n), 1;
    for (unsigned i = 0; i < n; i++) {
        unsigned a = acc[i * 4] | acc[i * 4 + 1] << 8, v = acc[i * 4 + 2], k = acc[i * 4 + 3];
        const Access *g = &log_[i];
        static const char *const KIND[] = {"internal", "read", "write"};
        if (g->kind != k || (k && (g->addr != a || g->value != v)))
            return printf("test %u: cycle %u: %s %04X %02X, want %s %04X %02X\n", index, i + 1, KIND[g->kind],
                          g->addr, g->value, KIND[k], a, v), 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "sst") && strcmp(argv[1], "sst-end"))) {
        fprintf(stderr, "usage: gb_cpu_tests sst|sst-end FILE...\n");
        return 2;
    }
    int cycles = !strcmp(argv[1], "sst"), failed = 0;
    for (int f = 2; f < argc; f++) {
        FILE *fp = fopen(argv[f], "rb");
        if (!fp) { perror(argv[f]); return 2; }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *data = malloc((size_t)size);
        if (!data || fread(data, 1, (size_t)size, fp) != (size_t)size) { perror(argv[f]); return 2; }
        fclose(fp);
        p_ = data;
        unsigned count = u16() | u16() << 16, bad = 0;
        for (unsigned i = 0; i < count; i++)
            if (run_test(i, cycles) && ++bad >= 3) break;  /* the first few are enough */
        printf("%s: %s\n", argv[f], bad ? "FAIL" : "ok");
        failed |= bad != 0;
        free(data);
    }
    return failed;
}
