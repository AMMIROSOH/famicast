/* CPS-1 self-checks: matching a zip to the game table and the load layouts
 * (with a test-only game), the detector, and the machine: VBlank, inputs,
 * the CPS-B ID and multiplier, unmapped reads; the palette, the three
 * scroll layers, sprites and priority. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cps1.h"
#include "detect.h"
#include "input_log.h"

extern const SystemDetector cps1_detector;

/* ---- a stored-only zip built in memory ---- */

static uint8_t zbuf[1 << 21];
static size_t zlen;
static struct { const char *name; const uint8_t *data; uint32_t size, header; } zent[16];
static int zcount;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

static void zip_add(const char *name, const uint8_t *data, uint32_t size) {
    zent[zcount].name = name, zent[zcount].data = data, zent[zcount].size = size, zent[zcount].header = (uint32_t)zlen;
    zcount++;
}

static size_t zip_build(void) {  /* local headers and data, the directory, the end record */
    zlen = 0;
    for (int i = 0; i < zcount; i++) {
        uint8_t *h = zbuf + zlen;
        size_t n = strlen(zent[i].name);
        memset(h, 0, 30);
        put32(h, 0x04034B50u), put32(h + 14, zip_crc32(zent[i].data, zent[i].size));
        put32(h + 18, zent[i].size), put32(h + 22, zent[i].size), put16(h + 26, (uint32_t)n);
        zent[i].header = (uint32_t)zlen;
        memcpy(h + 30, zent[i].name, n), memcpy(h + 30 + n, zent[i].data, zent[i].size);
        zlen += 30 + n + zent[i].size;
    }
    size_t dir = zlen;
    for (int i = 0; i < zcount; i++) {
        uint8_t *h = zbuf + zlen;
        size_t n = strlen(zent[i].name);
        memset(h, 0, 46);
        put32(h, 0x02014B50u), put32(h + 16, zip_crc32(zent[i].data, zent[i].size));
        put32(h + 20, zent[i].size), put32(h + 24, zent[i].size), put16(h + 28, (uint32_t)n);
        put32(h + 42, zent[i].header);
        memcpy(h + 46, zent[i].name, n);
        zlen += 46 + n;
    }
    uint8_t *e = zbuf + zlen;
    memset(e, 0, 22);
    put32(e, 0x06054B50u), put16(e + 8, (uint32_t)zcount), put16(e + 10, (uint32_t)zcount);
    put32(e + 12, (uint32_t)(zlen - dir)), put32(e + 16, (uint32_t)dir);
    zlen += 22;
    return zlen;
}

/* ---- a test game: one file of each layout ---- */

static uint8_t even[4] = {1, 2, 5, 6}, odd[4] = {3, 4, 7, 8}, swapped[4] = {0x22, 0x11, 0x44, 0x33};
static uint8_t gfx[2][4] = {{0xA0, 0xA1, 0xA2, 0xA3}, {0xB0, 0xB1, 0xB2, 0xB3}}, plain[3] = {9, 8, 7};
static Cps1Game test_game = {"test", "Test", CPS1_PLAIN, {16, 16, 3, 0}, {0}, 6, {{0}}};

static void make_test_game(void) {
    Cps1File f[6] = {
        {zip_crc32(even, 4), 4, CPS1_R_MAIN, CPS1_LOAD_EVEN, 0},
        {zip_crc32(odd, 4), 4, CPS1_R_MAIN, CPS1_LOAD_ODD, 1},
        {zip_crc32(swapped, 4), 4, CPS1_R_MAIN, CPS1_LOAD_SWAP16, 8},
        {zip_crc32(gfx[0], 4), 4, CPS1_R_GFX, CPS1_LOAD_GFX4, 0},
        {zip_crc32(gfx[1], 4), 4, CPS1_R_GFX, CPS1_LOAD_GFX4, 6},
        {zip_crc32(plain, 3), 3, CPS1_R_SOUND, CPS1_LOAD_PLAIN, 0},
    };
    memcpy(test_game.files, f, sizeof f);
}

static void zip_test_game(int skip) {
    static const char *names[6] = {"even", "odd", "swapped", "gfx0", "gfx1", "plain"};
    const uint8_t *data[6] = {even, odd, swapped, gfx[0], gfx[1], plain};
    static const uint8_t pal[279] = {0};
    zcount = 0;
    zip_add("buf1", pal, sizeof pal);  /* a PAL dump the table does not list */
    for (int i = 0; i < 6; i++)
        if (i != skip) zip_add(names[i], data[i], test_game.files[i].size);
    zip_build();
}

static void test_layouts(void) {
    Zip z;
    Cps1Rom r;
    char err[128];
    zip_test_game(-1);
    assert(zip_open(&z, zbuf, zlen) == 0);
    assert(cps1_match_in(&z, &test_game, 1) == &test_game);
    assert(cps1_load(&test_game, &z, &r, err, sizeof err) == 0);
    static const uint8_t main_[16] = {1, 3, 2, 4, 5, 7, 6, 8, 0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0};
    assert(r.size[CPS1_R_MAIN] == 16 && memcmp(r.region[CPS1_R_MAIN], main_, 16) == 0);
    static const uint8_t gfx_[16] = {0xA0, 0xA1, 0, 0, 0, 0, 0xB0, 0xB1, 0xA2, 0xA3, 0, 0, 0, 0, 0xB2, 0xB3};
    assert(memcmp(r.region[CPS1_R_GFX], gfx_, 16) == 0);
    assert(memcmp(r.region[CPS1_R_SOUND], plain, 3) == 0);
    assert(r.size[CPS1_R_OKI] == 0);
    cps1_unload(&r);
    zip_close(&z);
}

static void test_missing(void) {
    Zip z;
    Cps1Rom r;
    char err[128] = "";
    zip_test_game(2);
    assert(zip_open(&z, zbuf, zlen) == 0);
    assert(cps1_match_in(&z, &test_game, 1) == NULL);
    assert(cps1_load(&test_game, &z, &r, err, sizeof err) == -1);
    char crc[16];
    snprintf(crc, sizeof crc, "%08x", (unsigned)test_game.files[2].crc);
    assert(strstr(err, crc));
    zip_close(&z);
}

static void test_detector(void) {
    static uint8_t junk[1 << 20];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 2654435761u >> 13);
    zcount = 0;
    zip_add("game.bin", junk, sizeof junk);
    zip_build();
    assert(cps1_detector.score(zbuf, zlen) == 0);
    assert(cps1_detector.score(junk, sizeof junk) == 0);  /* not a zip at all */
}

/* ---- the machine, on a hand-assembled program ---- */

static Cps1 m;
static uint8_t prog[1 << 20];
static Cps1Game prog_game = {"prog", "Program", CPS1_PLAIN, {1 << 20, 0, 0, 0},
                             {0x20, 0x0004, 0x00, 0x02, 0x04, 0x06, 0x2E, {0x26, 0x28, 0x2A, 0x2C}, 0x30, {0}}, 0, {{0}}};
static Cps1Rom prog_rom;

static void w16(uint32_t a, uint16_t v) { prog[a] = (uint8_t)(v >> 8), prog[a + 1] = (uint8_t)v; }

static void boot_program(void) {
    memset(prog, 0, sizeof prog);
    w16(0, 0x00FF), w16(2, 0x8000);              /* SSP $FF8000 */
    w16(4, 0x0000), w16(6, 0x0400);              /* PC $400 */
    w16(26 * 4, 0x0000), w16(26 * 4 + 2, 0x0500);  /* level 2 autovector: $500 */
    static const uint16_t main_[] = {0x46FC, 0x2000,                       /* move #$2000,sr */
                                     0x33F9, 0x0080, 0x0000, 0x00FF, 0x0002,  /* move.w $800000,$FF0002 */
                                     0x60F4};                              /* bra.s the move */
    for (unsigned i = 0; i < sizeof main_ / 2; i++) w16(0x400 + i * 2, main_[i]);
    static const uint16_t irq[] = {0x5279, 0x00FF, 0x0000, 0x4E73};      /* addq.w #1,$FF0000; rte */
    for (unsigned i = 0; i < sizeof irq / 2; i++) w16(0x500 + i * 2, irq[i]);
    prog_rom.game = &prog_game;
    prog_rom.region[CPS1_R_MAIN] = prog;
    prog_rom.size[CPS1_R_MAIN] = sizeof prog;
    cps1_init(&m, &prog_rom);
}

static uint16_t ram16(uint32_t a) { return (uint16_t)(m.ram[a & 0xFFFF] << 8 | m.ram[(a + 1) & 0xFFFF]); }

static void test_vblank(void) {
    boot_program();
    for (int i = 0; i < 3; i++) cps1_frame(&m);
    assert(ram16(0xFF0000) == 3);
}

static void test_inputs(void) {
    boot_program();
    cps1_frame(&m);
    assert(ram16(0xFF0002) == 0xFFFF);           /* nothing pressed: all high */
    cps1_set_input(&m, 0, PAD_RIGHT | PAD_B);
    cps1_set_input(&m, 1, PAD_UP);
    cps1_frame(&m);
    assert(ram16(0xFF0002) == (uint16_t)~(0x01 | 0x10 | 0x0800));  /* P1 right, button 1; P2 up */
    cps1_set_input(&m, 0, PAD_START | PAD_SELECT);
    assert((m68k_read(&m.cpu, 0x800018, 5, 3) >> 8) == (uint8_t)~(0x10 | 0x01));  /* start 1, coin 1 */
}

static void test_cpsb(void) {
    boot_program();
    assert(m68k_read(&m.cpu, 0x800140 + 0x20, 5, 3) == 0x0004);  /* the ID port */
    m68k_write(&m.cpu, 0x800140 + 0x00, 300, 5, 3);
    m68k_write(&m.cpu, 0x800140 + 0x02, 400, 5, 3);
    uint32_t p = (uint32_t)m68k_read(&m.cpu, 0x800140 + 0x06, 5, 3) << 16 | m68k_read(&m.cpu, 0x800140 + 0x04, 5, 3);
    assert(p == 120000);
}

static void test_unmapped(void) {
    boot_program();
    assert(m68k_read(&m.cpu, 0x600000, 5, 3) == 0xFFFF);
    m68k_write(&m.cpu, 0x900010, 0xABCD, 5, 3);  /* graphics RAM is RAM */
    assert(m68k_read(&m.cpu, 0x900010, 5, 3) == 0xABCD);
}

/* ---- video ---- */

static uint8_t gfxrom[1 << 20];

static void boot_video(void) {
    boot_program();
    memset(gfxrom, 0xFF, sizeof gfxrom);  /* every pen 15: transparent */
    prog_rom.region[CPS1_R_GFX] = gfxrom;
    prog_rom.size[CPS1_R_GFX] = sizeof gfxrom;
    prog_game.b.layer_enable[0] = 0x02, prog_game.b.layer_enable[1] = 0x04, prog_game.b.layer_enable[2] = 0x08;
}

static void cpsa(unsigned off, uint16_t v) { m68k_write(&m.cpu, 0x800100 + off, v, 5, 3); }
static void cpsb(unsigned off, uint16_t v) { m68k_write(&m.cpu, 0x800140 + off, v, 5, 3); }
static void gram(uint32_t off, uint16_t v) { m68k_write(&m.cpu, 0x900000 + off, v, 5, 3); }

/* pen at (x, y) of a tile at addr with rows of rowbytes; 8 pixels per 4 bytes */
static void pixel(uint32_t addr, unsigned rowbytes, unsigned x, unsigned y, unsigned pen) {
    uint8_t *b = gfxrom + addr + y * rowbytes + (x / 8) * 4;
    for (unsigned p = 0; p < 4; p++) {
        b[p] = (uint8_t)(b[p] & ~(0x80 >> x % 8));
        b[p] |= (uint8_t)(((pen >> p) & 1) << (7 - x % 8));
    }
}

/* bottom to top: scroll3, scroll2, sprites, scroll1; all three enabled */
#define ORDER (3 << 6 | 2 << 8 | 0 << 10 | 1 << 12 | 0x0E)

static void test_palette(void) {
    boot_video();
    cpsb(0x30, 0x3F);                    /* upload all six pages */
    gram(0x20000 + 0x13 * 2, 0xFF08);    /* brightest: red 15, blue 8 */
    gram(0x20000 + 0x14 * 2, 0x0F00);    /* darkest: red 15 */
    cpsa(0x0A, 0x9200);                  /* palette base $920000: the upload */
    assert(cps1_color(&m, 0x13) == 0xFF0088);
    assert(cps1_color(&m, 0x14) == 0x550000);
}

static void test_scroll1_tile(void) {
    boot_video();
    cpsb(0x2E, ORDER);
    cpsa(0x02, 0x9080);                  /* scroll1 map at $908000 */
    unsigned col = 9, row = 2, i = (row & 0x1F) + ((col & 0x3F) << 5) + ((row & 0x20) << 6);
    gram(0x8000 + i * 4, 3), gram(0x8000 + i * 4 + 2, 4);  /* code 3, palette 4 */
    pixel(3 * 64, 8, 8 + 0, 0, 5);       /* an odd column: the right half (bytes 4-7) */
    cps1_render(&m);
    assert(m.frame[0 * CPS1_WIDTH + 8] == 0x200 + 4 * 16 + 5);
    assert(m.frame[0 * CPS1_WIDTH + 9] == 0xBFF);  /* the background */
}

static void test_scroll2_tile(void) {
    boot_video();
    cpsb(0x2E, ORDER);
    cpsa(0x04, 0x9040);                  /* scroll2 map at $904000 */
    cpsa(0x10, 16), cpsa(0x12, 0);       /* scrolled 16 right */
    unsigned col = 6, row = 2, i = (row & 0x0F) + ((col & 0x3F) << 4) + ((row & 0x30) << 6);
    gram(0x4000 + i * 4, 5), gram(0x4000 + i * 4 + 2, 2);
    pixel(5 * 128, 8, 3, 4, 7);          /* tile pixel (3, 4): map (99, 36) */
    cps1_render(&m);
    assert(m.frame[(36 - 16) * CPS1_WIDTH + (99 - 64 - 16)] == 0x400 + 2 * 16 + 7);
}

static void test_scroll3_tile(void) {
    boot_video();
    cpsb(0x2E, ORDER);
    cpsa(0x06, 0x90C0);                  /* scroll3 map at $90C000 */
    unsigned col = 3, row = 0, i = (row & 0x07) + ((col & 0x3F) << 3) + ((row & 0x38) << 6);
    gram(0xC000 + i * 4, 2), gram(0xC000 + i * 4 + 2, 0);
    pixel(2 * 512, 16, 8, 26, 9);        /* map (104, 26): screen (40, 10) */
    cps1_render(&m);
    assert(m.frame[10 * CPS1_WIDTH + 40] == 0x600 + 9);
}

static void sprite_scene(uint16_t prio) {
    boot_video();
    cpsb(0x2E, ORDER);
    cpsb(0x28, prio);                    /* the mask of group 1 */
    cpsa(0x00, 0x9100);                  /* sprites at $910000 */
    gram(0x10000, 64 + 100), gram(0x10002, 16 + 50), gram(0x10004, 0x20);
    gram(0x10006, 1 | 0x20 | 1 << 8);    /* palette 1, flipped in X, 2 blocks wide */
    gram(0x1000E, 0xFF00);               /* the end */
    pixel(0x20 * 128, 8, 0, 0, 3);
    cpsa(0x04, 0x9040);                  /* a scroll2 tile under the sprite's pixel, group 1, pen 7 */
    unsigned col = 12, row = 4, i = (row & 0x0F) + ((col & 0x3F) << 4) + ((row & 0x30) << 6);
    gram(0x4000 + i * 4, 6), gram(0x4000 + i * 4 + 2, 1 << 7);
    pixel(6 * 128, 8, 3, 2, 7);          /* map (195, 66): screen (131, 50) */
    cps1_render(&m);
}

static void test_sprite_flip(void) {
    sprite_scene(0);
    assert(m.frame[50 * CPS1_WIDTH + 131] == 0x13);   /* block $20, flipped, on the right */
    assert(m.frame[50 * CPS1_WIDTH + 100] == 0xBFF);  /* block $21 (empty) on the left */
}

static void test_priority(void) {
    sprite_scene(1 << 7);                /* pen 7 of group 1 goes over sprites */
    assert(m.frame[50 * CPS1_WIDTH + 131] == 0x400 + 0 * 16 + 7);
}

int main(void) {
    make_test_game();
    test_layouts();
    test_missing();
    test_detector();
    test_vblank();
    test_inputs();
    test_cpsb();
    test_unmapped();
    test_palette();
    test_scroll1_tile();
    test_scroll2_tile();
    test_scroll3_tile();
    test_sprite_flip();
    test_priority();
    puts("cps1_tests: ok");
    return 0;
}
