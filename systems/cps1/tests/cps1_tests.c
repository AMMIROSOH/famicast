/* CPS-1 self-checks: matching a zip to the game table and the load layouts
 * (with a test-only game), and the detector. */
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

int main(void) {
    make_test_game();
    test_layouts();
    test_missing();
    test_detector();
    puts("cps1_tests: ok");
    return 0;
}
