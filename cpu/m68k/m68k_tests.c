/* SingleStepTests m68000 on a flat 16 MB bus (tests/genesis/test_m68k.py).
 *
 *   m68k_tests sst|sst-end [-v] FILE...
 *
 * Runs one instruction per test from the initial state and compares the
 * final one: registers, SR, PC (the next prefetch address), the two queue
 * words, RAM; sst also every bus transaction (kind, address, function
 * code, strobes, data, clocks, the idle spans between). Prints
 * "FILE: passed/total" and, with -v, the first failures.
 *
 * File format (tests/fetch_test_roms.py fetch_sst_m68k), little endian:
 * u32 count; per test: initial state, final state, u32 length, u32 n,
 * n transactions. A state: 19 u32 (d0-d7, a0-a6, usp, ssp, sr, pc), 2 u16
 * (prefetch), u32 m, m x (u32 address, u8 byte). A transaction: u8 kind
 * (0 idle, 1 write, 2 read, 3 TAS, 4 read and 5 write address error),
 * u16 clocks, u8 fc, u32 address, u16 data, u8 uds, u8 lds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k_cpu.h"

static uint8_t mem[1 << 24];
typedef struct { uint8_t kind; uint16_t clocks; uint8_t fc; uint32_t addr; uint16_t data; uint8_t uds, lds; } Tx;
static Tx got[256];
static int ngot;
static uint32_t touched[512];
static int ntouched;

static void log_tx(int kind, unsigned clocks, unsigned fc, uint32_t a, uint16_t d, unsigned strobe) {
    if (kind == 0 && ngot && got[ngot - 1].kind == 0) {  /* idle spans merge */
        got[ngot - 1].clocks = (uint16_t)(got[ngot - 1].clocks + clocks);
        return;
    }
    if (ngot < 256) got[ngot++] = (Tx){(uint8_t)kind, (uint16_t)clocks, (uint8_t)fc, a, d, (uint8_t)(strobe >> 1 & 1), (uint8_t)(strobe & 1)};
}
uint16_t m68k_read(M68k *c, uint32_t a, unsigned fc, unsigned strobe) {
    (void)c;
    uint16_t v = (uint16_t)(mem[a & ~1u] << 8 | mem[a | 1]);
    log_tx(2, 4, fc, a & ~1u, v, strobe);
    return v;
}
void m68k_write(M68k *c, uint32_t a, uint16_t v, unsigned fc, unsigned strobe) {
    (void)c;
    if (strobe & 2) mem[a & ~1u] = (uint8_t)(v >> 8);
    if (strobe & 1) mem[a | 1] = (uint8_t)v;
    if (ntouched < 510) touched[ntouched++] = a & ~1u, touched[ntouched++] = a | 1;
    log_tx(1, 4, fc, a & ~1u, v, strobe);
}
void m68k_idle(M68k *c, unsigned clocks) { (void)c; log_tx(0, clocks, 0, 0, 0, 0); }
void m68k_aborted(M68k *c, uint32_t a, unsigned fc, unsigned strobe, int write) {
    (void)c;
    log_tx(write ? 5 : 4, 4, fc, a & ~1u, 0, strobe);
}
int m68k_iack(M68k *c, int level) { (void)c, (void)level; return -1; }
void m68k_reset_out(M68k *c) { (void)c; }

static const uint8_t *p_;
static uint32_t u32(void) { uint32_t v; memcpy(&v, p_, 4); p_ += 4; return v; }
static uint16_t u16(void) { uint16_t v; memcpy(&v, p_, 2); p_ += 2; return v; }
typedef struct { uint32_t r[19]; uint16_t pf[2]; uint32_t n; const uint8_t *ram; } State;
static State state(void) {
    State s;
    for (int i = 0; i < 19; i++) s.r[i] = u32();
    s.pf[0] = u16(), s.pf[1] = u16();
    s.n = u32();
    s.ram = p_;
    p_ += s.n * 5;
    return s;
}
static const char *const NAME[19] = {"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3",
                                     "a4", "a5", "a6", "usp", "ssp", "sr", "pc"};
static const char KIND[] = "nwrtRW";

static int verbose, shown;
static int fail(unsigned i, const char *fmt, uint32_t g, uint32_t w) {
    if (verbose && shown < 8) {
        printf("  test %u: ", i);
        printf(fmt, g, w);
        printf("\n");
        shown++;
    }
    return 1;
}
static void show_tx(const char *label, const Tx *t, int n) {
    printf("    %s:", label);
    for (int i = 0; i < n; i++) {
        if (!t[i].kind) printf(" n%u", t[i].clocks);
        else printf(" %c%u:%06X%s=%04X/%u", KIND[t[i].kind], t[i].clocks, (unsigned)t[i].addr,
                    t[i].uds && t[i].lds ? "w" : t[i].uds ? "u" : "l", t[i].data, t[i].fc);
    }
    printf("\n");
}

static int run(unsigned index, int bus) {
    static M68k m;
    State in = state(), want = state();
    uint32_t length = u32(), ntx = u32();
    const uint8_t *tx = p_;
    p_ += ntx * 12;
    (void)length;
    for (uint32_t i = 0; i < in.n; i++) {
        uint32_t a;
        memcpy(&a, in.ram + i * 5, 4);
        mem[a & 0xFFFFFF] = in.ram[i * 5 + 4];
    }
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 8; i++) m.d[i] = in.r[i];
    for (int i = 0; i < 7; i++) m.a[i] = in.r[8 + i];
    m.sr = (uint16_t)in.r[17];
    m.usp = in.r[15], m.ssp = in.r[16];
    m.a[7] = m.sr & M68K_S ? m.ssp : m.usp;
    m.pc = in.r[18];
    m.ir = in.pf[0], m.irc = in.pf[1];
    ngot = ntouched = 0;
    m68k_step(&m);

    uint32_t g[19];
    for (int i = 0; i < 8; i++) g[i] = m.d[i];
    for (int i = 0; i < 7; i++) g[8 + i] = m.a[i];
    g[15] = m.sr & M68K_S ? m.usp : m.a[7];
    g[16] = m.sr & M68K_S ? m.a[7] : m.ssp;
    g[17] = m.sr, g[18] = m.pc;
    int bad = 0;
    char f[64];
    for (int i = 0; i < 19 && !bad; i++)
        if (g[i] != want.r[i]) snprintf(f, sizeof f, "%s = %%08X, want %%08X", NAME[i]), bad = fail(index, f, g[i], want.r[i]);
    if (!bad && (m.ir != want.pf[0] || m.irc != want.pf[1]))
        bad = fail(index, "queue %08X, want %08X", (uint32_t)m.ir << 16 | m.irc, (uint32_t)want.pf[0] << 16 | want.pf[1]);
    for (uint32_t i = 0; i < want.n && !bad; i++) {
        uint32_t a;
        memcpy(&a, want.ram + i * 5, 4);
        if (mem[a & 0xFFFFFF] != want.ram[i * 5 + 4]) {
            snprintf(f, sizeof f, "[%06X] = %%02X, want %%02X", (unsigned)a);
            bad = fail(index, f, mem[a & 0xFFFFFF], want.ram[i * 5 + 4]);
        }
    }
    if (!bad && bus) {
        Tx w[256];
        int nw = 0;
        for (uint32_t i = 0; i < ntx && nw < 256; i++) {
            const uint8_t *t = tx + i * 12;
            Tx x = {t[0], 0, t[3], 0, 0, t[10], t[11]};
            memcpy(&x.clocks, t + 1, 2), memcpy(&x.addr, t + 4, 4), memcpy(&x.data, t + 8, 2);
            x.addr &= ~1u;
            if (x.kind == 0 && nw && w[nw - 1].kind == 0) w[nw - 1].clocks = (uint16_t)(w[nw - 1].clocks + x.clocks);
            else w[nw++] = x;
        }
        int same = nw == ngot;
        for (int i = 0; same && i < nw; i++) {
            const Tx *a = &got[i], *b = &w[i];
            uint16_t dm = (uint16_t)((b->uds ? 0xFF00 : 0) | (b->lds ? 0xFF : 0));
            same = a->kind == b->kind && a->clocks == b->clocks &&
                   (!a->kind || (a->addr == b->addr && a->fc == b->fc && a->uds == b->uds && a->lds == b->lds &&
                                 (a->kind >= 4 || ((a->data ^ b->data) & dm) == 0)));
        }
        if (!same) {
            bad = fail(index, "bus %u transactions, want %u", (uint32_t)ngot, (uint32_t)nw);
            if (verbose && shown <= 8) show_tx("got ", got, ngot), show_tx("want", w, nw);
        }
    }
    for (uint32_t i = 0; i < in.n; i++) {
        uint32_t a;
        memcpy(&a, in.ram + i * 5, 4);
        mem[a & 0xFFFFFF] = 0;
    }
    for (int i = 0; i < ntouched; i++) mem[touched[i] & 0xFFFFFF] = 0;
    return bad;
}

int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "sst") && strcmp(argv[1], "sst-end"))) {
        fprintf(stderr, "usage: m68k_tests sst|sst-end [-v] FILE...\n");
        return 2;
    }
    int bus = !strcmp(argv[1], "sst"), failed = 0;
    for (int f = 2; f < argc; f++) {
        if (!strcmp(argv[f], "-v")) { verbose = 1; continue; }
        FILE *fp = fopen(argv[f], "rb");
        if (!fp) { perror(argv[f]); return 2; }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *data = malloc((size_t)size);
        if (!data || fread(data, 1, (size_t)size, fp) != (size_t)size) { perror(argv[f]); return 2; }
        fclose(fp);
        p_ = data;
        uint32_t count = u32(), bad = 0;
        shown = 0;
        for (uint32_t i = 0; i < count; i++) bad += (uint32_t)run(i, bus);
        printf("%s: %u/%u\n", argv[f], count - bad, count);
        failed |= bad != 0;
        free(data);
    }
    return failed;
}
