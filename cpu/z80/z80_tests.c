/* Z80 test runner on a flat 64 KB bus (driven by tests/genesis/test_z80.py).
 *
 *   z80_tests sst FILE...   run SingleStepTests z80 files converted by
 *                           tests/fetch_test_roms.py: registers (with AF',
 *                           BC', DE', HL', IX, IY, I, R, WZ, IFF1/2, IM, EI,
 *                           P and Q), RAM, port accesses, and every T-state
 *                           of the bus (address, data, read/write/memory/IO
 *                           pins). Prints passed/total per file, the first
 *                           failures, and counts all of them.
 *   z80_tests self          what the corpus does not cover: HALT, EI's
 *                           delay, IM 0/1/2, NMI, R's bit 7
 *
 * Binary format (little endian): "Z80S\1", u32 count; per test: u8 length +
 * name, initial state, final state, u16 nports x (u16 port, u8 value, u8 1 =
 * write), u16 ncycles x (u16 address, i16 data (-1: none), u8 pins: bit 0
 * read, 1 write, 2 memory, 3 IO). State: u16 pc sp wz, u8 a f b c d e h l,
 * u16 ix iy af' bc' de' hl', u8 i r im p q iff1 iff2 ei, u16 nram, nram x
 * (u16 address, u8 value). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80_cpu.h"

static uint8_t mem[0x10000];

typedef struct { uint16_t addr; uint8_t value, write; } Port;
typedef struct { uint16_t addr; int16_t data; uint8_t pins; } Row;
enum { PIN_R = 1, PIN_W = 2, PIN_M = 4, PIN_I = 8 };
enum { MAX_EV = 40, MAX_ROWS = 96, MAX_PORTS = 16 };

/* what the CPU did on the bus, in order */
typedef enum { EV_M1, EV_READ, EV_WRITE, EV_IN, EV_OUT, EV_IDLE } Kind;
typedef struct { Kind kind; uint16_t addr; uint8_t value; unsigned n; } Event;
static Event ev[MAX_EV];
static int ev_n;
static Port want_ports[MAX_PORTS], got_ports[MAX_PORTS];
static int want_nports, got_nports;

static void add(Kind k, uint16_t a, uint8_t v, unsigned n) {
    if (ev_n < MAX_EV) ev[ev_n] = (Event){k, a, v, n};
    ev_n++;
}
uint8_t z80_fetch(Z80 *z, uint16_t pc) {
    uint8_t v = mem[pc];
    add(EV_M1, (uint16_t)(z->i << 8 | z->r), v, pc);  /* addr: what the refresh puts out; n: the fetch's address */
    return v;
}
uint8_t z80_read(Z80 *z, uint16_t a) { (void)z; add(EV_READ, a, mem[a], 0); return mem[a]; }
void z80_write(Z80 *z, uint16_t a, uint8_t v) { (void)z; add(EV_WRITE, a, v, 0); mem[a] = v; }
uint8_t z80_in(Z80 *z, uint16_t p) {
    (void)z;
    uint8_t v = 0xFF;
    if (got_nports < want_nports && !want_ports[got_nports].write && want_ports[got_nports].addr == p) v = want_ports[got_nports].value;
    if (got_nports < MAX_PORTS) got_ports[got_nports] = (Port){p, v, 0};
    got_nports++;
    add(EV_IN, p, v, 0);
    return v;
}
void z80_out(Z80 *z, uint16_t p, uint8_t v) {
    (void)z;
    if (got_nports < MAX_PORTS) got_ports[got_nports] = (Port){p, v, 1};
    got_nports++;
    add(EV_OUT, p, v, 0);
}
void z80_idle(Z80 *z, uint16_t a, unsigned n) { (void)z; add(EV_IDLE, a, 0, n); }
uint8_t z80_irq_ack(Z80 *z) { (void)z; return 0xFF; }

static const uint8_t *p_;
static unsigned u8(void) { return *p_++; }
static unsigned u16(void) { unsigned v = p_[0] | p_[1] << 8; p_ += 2; return v; }

typedef struct {
    Z80 cpu;
    unsigned nram;
    const uint8_t *ram;  /* nram x (u16 addr, u8 value) */
} State;

static State state(void) {
    State s = {0};
    Z80 *c = &s.cpu;
    c->pc = (uint16_t)u16(), c->sp = (uint16_t)u16(), c->wz = (uint16_t)u16();
    uint8_t *r[8] = {&c->a, &c->f, &c->b, &c->c, &c->d, &c->e, &c->h, &c->l};
    for (int i = 0; i < 8; i++) *r[i] = (uint8_t)u8();
    unsigned ix = u16(), iy = u16();
    c->ixh = (uint8_t)(ix >> 8), c->ixl = (uint8_t)ix, c->iyh = (uint8_t)(iy >> 8), c->iyl = (uint8_t)iy;
    c->af_ = (uint16_t)u16(), c->bc_ = (uint16_t)u16(), c->de_ = (uint16_t)u16(), c->hl_ = (uint16_t)u16();
    c->i = (uint8_t)u8(), c->r = (uint8_t)u8(), c->im = (uint8_t)u8(), c->p = (uint8_t)u8(), c->q = (uint8_t)u8();
    c->iff1 = (uint8_t)u8(), c->iff2 = (uint8_t)u8(), c->ei = (uint8_t)u8();
    s.nram = u16();
    s.ram = p_;
    p_ += s.nram * 3;
    return s;
}

enum { NREGS = 26 };
static const char *const NAMES[NREGS] = {"pc", "sp", "wz", "a", "f", "b", "c", "d", "e", "h", "l", "ix", "iy", "af'", "bc'", "de'",
                                         "hl'", "i", "r", "im", "p", "q", "iff1", "iff2", "ei", "halted"};
static void regs(const Z80 *c, unsigned out[NREGS]) {
    unsigned v[NREGS] = {c->pc, c->sp, c->wz, c->a, c->f, c->b, c->c, c->d, c->e, c->h, c->l,
                         (unsigned)(c->ixh << 8 | c->ixl), (unsigned)(c->iyh << 8 | c->iyl), c->af_, c->bc_, c->de_, c->hl_,
                         c->i, c->r, c->im, c->p, c->q, c->iff1, c->iff2, c->ei, 0};
    memcpy(out, v, sizeof v);
}

/* The bus rows (one per T-state) of the events: what the CPU's pins show. */
static int rows_of(Row *rows) {
    int n = 0;
    for (int i = 0; i < ev_n && i < MAX_EV; i++) {
        const Event *e = &ev[i];
        Row r[5];
        int k = 0;
        switch (e->kind) {
        case EV_M1:  /* e->addr is the refresh address, e->n the PC */
            r[k++] = (Row){(uint16_t)e->n, -1, 0};
            r[k++] = (Row){(uint16_t)e->n, -1, PIN_R | PIN_M};
            r[k++] = (Row){e->addr, e->value, 0};
            r[k++] = (Row){e->addr, -1, 0};
            break;
        case EV_READ:
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, -1, PIN_R | PIN_M};
            r[k++] = (Row){e->addr, e->value, 0};
            break;
        case EV_WRITE:
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, e->value, PIN_W | PIN_M};
            r[k++] = (Row){e->addr, -1, 0};
            break;
        case EV_IN:
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, -1, PIN_R | PIN_I};
            r[k++] = (Row){e->addr, e->value, 0};
            break;
        case EV_OUT:
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, -1, 0};
            r[k++] = (Row){e->addr, e->value, PIN_W | PIN_I};
            r[k++] = (Row){e->addr, -1, 0};
            break;
        case EV_IDLE:
            for (unsigned t = 0; t < e->n && n < MAX_ROWS; t++) rows[n++] = (Row){e->addr, -1, 0};
            continue;
        }
        for (int j = 0; j < k && n < MAX_ROWS; j++) rows[n++] = r[j];
    }
    return n;
}

/* 0 if the test passes, else prints why (the first difference) when `show`. */
static int run_test(unsigned index, int show) {
    char name[256] = {0};
    unsigned len = u8();
    memcpy(name, p_, len);
    p_ += len;
    State in = state(), want = state();
    want_nports = (int)u16();
    for (int i = 0; i < want_nports; i++) {
        want_ports[i].addr = (uint16_t)u16(), want_ports[i].value = (uint8_t)u8(), want_ports[i].write = (uint8_t)u8();
    }
    unsigned n = u16();
    const uint8_t *acc = p_;
    p_ += n * 5;
    memset(mem, 0, sizeof mem);
    for (unsigned i = 0; i < in.nram; i++) mem[in.ram[i * 3] | in.ram[i * 3 + 1] << 8] = in.ram[i * 3 + 2];
    Z80 c = in.cpu;
    ev_n = 0;
    got_nports = 0;
    z80_step(&c);
#define FAIL(...) return (show ? printf("test %u (%s): ", index, name), printf(__VA_ARGS__), printf("\n") : 0), 1
    unsigned got[NREGS], exp[NREGS];
    regs(&c, got);
    regs(&want.cpu, exp);
    for (int i = 0; i < NREGS - 1; i++)
        if (got[i] != exp[i]) FAIL("%s = %X, want %X (f = %02X, want %02X)", NAMES[i], got[i], exp[i], c.f, want.cpu.f);
    for (unsigned i = 0; i < want.nram; i++) {
        unsigned a = want.ram[i * 3] | want.ram[i * 3 + 1] << 8;
        if (mem[a] != want.ram[i * 3 + 2]) FAIL("[%04X] = %02X, want %02X", a, mem[a], want.ram[i * 3 + 2]);
    }
    int ports_ok = got_nports == want_nports;
    for (int i = 0; ports_ok && i < got_nports && i < MAX_PORTS; i++)
        ports_ok = got_ports[i].addr == want_ports[i].addr && got_ports[i].value == want_ports[i].value && got_ports[i].write == want_ports[i].write;
    if (!ports_ok) FAIL("port accesses differ (%d, want %d)", got_nports, want_nports);
    if (c.tstates != n) FAIL("%u T-states, want %u", (unsigned)c.tstates, n);
    Row rows[MAX_ROWS];
    int nrows = rows_of(rows);
    if ((unsigned)nrows != n) FAIL("bus shows %d T-states, want %u", nrows, n);
    for (unsigned i = 0; i < n; i++) {
        Row w = {(uint16_t)(acc[i * 5] | acc[i * 5 + 1] << 8), (int16_t)(acc[i * 5 + 2] | acc[i * 5 + 3] << 8), acc[i * 5 + 4]};
        if (rows[i].addr != w.addr || rows[i].data != w.data || rows[i].pins != w.pins)
            FAIL("T-state %u: addr %04X data %d pins %X, want %04X %d %X", i + 1, rows[i].addr, rows[i].data, rows[i].pins, w.addr, w.data, w.pins);
    }
    return 0;
}

static int self_failed;
#define CHECK(cond) do { if (!(cond)) { printf("self: %s (line %d)\n", #cond, __LINE__); self_failed = 1; } } while (0)

static Z80 fresh(void) {
    Z80 c = {0};
    memset(mem, 0, sizeof mem);
    c.sp = 0x8000, c.pc = 0x1234;
    return c;
}

static int self_test(void) {
    Z80 c = fresh();
    c.iff1 = c.iff2 = 1, c.im = 1;  /* IM 1: 13 T-states, to $38, PC pushed */
    CHECK(z80_interrupt(&c) == 13 && c.pc == 0x38 && c.sp == 0x7FFE && mem[0x7FFF] == 0x12 && mem[0x7FFE] == 0x34);
    CHECK(!c.iff1 && !c.iff2 && z80_interrupt(&c) == 0);
    c = fresh();
    c.iff1 = c.iff2 = 1, c.im = 2, c.i = 0x40;  /* IM 2: the vector is I and the byte on the bus ($FF here): 19 */
    mem[0x40FF] = 0x00, mem[0x4100] = 0x90;
    CHECK(z80_interrupt(&c) == 19 && c.pc == 0x9000 && c.sp == 0x7FFE);
    c = fresh();
    c.iff1 = c.iff2 = 1, c.im = 0;  /* IM 0 with RST 38 on the bus */
    CHECK(z80_interrupt(&c) == 13 && c.pc == 0x38);
    c = fresh();  /* EI: no interrupt until the next instruction ends; DI: none at all */
    mem[0x1234] = 0xFB, mem[0x1235] = 0x00, mem[0x1236] = 0xF3;
    c.im = 1;
    z80_step(&c);
    CHECK(c.iff1 && c.iff2 && z80_interrupt(&c) == 0);
    z80_step(&c);
    CHECK(z80_interrupt(&c) == 13 && c.pc == 0x38);
    c = fresh();
    c.iff1 = c.iff2 = 1, c.im = 1;
    mem[0x1234] = 0xF3;
    z80_step(&c);
    CHECK(z80_interrupt(&c) == 0);
    c = fresh();  /* NMI: 11 T-states, to $66, IFF1 off and IFF2 kept; RETN gives IFF1 back */
    c.iff1 = c.iff2 = 1;
    CHECK(z80_nmi(&c) == 11 && c.pc == 0x66 && !c.iff1 && c.iff2 && mem[0x7FFE] == 0x34);
    mem[0x66] = 0xED, mem[0x67] = 0x45;
    z80_step(&c);
    CHECK(c.pc == 0x1234 && c.iff1);
    c = fresh();  /* HALT: the PC stays after it, NOPs run until an interrupt or NMI, which pushes that PC */
    mem[0x1234] = 0x76;
    z80_step(&c);
    CHECK(c.halted && c.pc == 0x1235);
    uint64_t t = c.tstates;
    z80_step(&c);
    CHECK(c.halted && c.pc == 0x1235 && c.tstates == t + 4);
    c.iff1 = c.iff2 = 1, c.im = 1;
    CHECK(z80_interrupt(&c) == 13 && !c.halted && mem[0x7FFF] == 0x12 && mem[0x7FFE] == 0x35);
    c = fresh();  /* R: 7 bits count, bit 7 stays */
    c.r = 0xFF;
    z80_step(&c);
    CHECK(c.r == 0x80);
    puts(self_failed ? "self: FAIL" : "self: ok");
    return self_failed;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "self")) return self_test();
    if (argc < 3 || strcmp(argv[1], "sst")) {
        fprintf(stderr, "usage: z80_tests sst FILE...\n");
        return 2;
    }
    int failed = 0;
    for (int f = 2; f < argc; f++) {
        FILE *fp = fopen(argv[f], "rb");
        if (!fp) { perror(argv[f]); return 2; }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *data = malloc((size_t)size);
        if (!data || fread(data, 1, (size_t)size, fp) != (size_t)size) { perror(argv[f]); return 2; }
        fclose(fp);
        if (size < 9 || memcmp(data, "Z80S\1", 5)) { fprintf(stderr, "%s: not a converted z80 file (run tests/fetch_test_roms.py --sst-z80)\n", argv[f]); return 2; }
        p_ = data + 5;
        unsigned count = u16(), bad = 0;
        count |= u16() << 16;
        for (unsigned i = 0; i < count; i++) bad += run_test(i, bad < 3);
        printf("%s: %s %u/%u\n", argv[f], bad ? "FAIL" : "ok", count - bad, count);
        failed |= bad != 0;
        free(data);
    }
    return failed;
}
