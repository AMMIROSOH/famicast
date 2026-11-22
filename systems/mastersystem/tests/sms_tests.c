/* Master System self-checks: header and detection, mapper and RAM, ports,
 * the VDP's ports, tile and sprite formats, interrupts and the frame's
 * timing, Pause, and save states. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "detect.h"
#include "input_log.h"
#include "sms.h"

extern const SystemDetector sms_detector;
static uint8_t rom[512 * 1024];
static Sms s;

static void vdp_reg(unsigned r, uint8_t v) { z80_out(&s.cpu, 0xBF, v), z80_out(&s.cpu, 0xBF, (uint8_t)(0x80 | r)); }
static void vdp_addr(uint16_t a, unsigned code) { z80_out(&s.cpu, 0xBF, (uint8_t)a), z80_out(&s.cpu, 0xBF, (uint8_t)(code << 6 | a >> 8)); }

static void test_header(void) {
    memset(rom, 0, sizeof rom);
    assert(sms_header(rom, sizeof rom) < 0);
    assert(sms_detector.score(rom, sizeof rom) == 20);      /* headerless: weak, .sms decides */
    assert(sms_detector.score(rom, 0x2001) == 0);
    memcpy(rom + 0x7FF0, "TMR SEGA", 8);
    rom[0x7FFF] = 0x4C;                                     /* export SMS, 256 KB */
    assert(sms_header(rom, sizeof rom) == 0x7FF0);
    assert(sms_detector.score(rom, sizeof rom) == 100);
    rom[0x7FFF] = 0x6C;                                     /* Game Gear export */
    assert(sms_detector.score(rom, sizeof rom) == 0);
    memset(rom + 0x7FF0, 0, 16);
}

static void test_memory(void) {
    for (unsigned b = 0; b < 32; b++) memset(rom + b * 0x4000, (int)b, 0x4000);
    rom[0x0123] = 0xAA;
    sms_init(&s, rom, sizeof rom, 0);
    assert(z80_read(&s.cpu, 0x4000) == 1 && z80_read(&s.cpu, 0x8000) == 2);  /* power-on banks 0, 1, 2 */
    z80_write(&s.cpu, 0xFFFF, 0x1B);
    assert(z80_read(&s.cpu, 0x8000) == 0x1B);
    assert(z80_read(&s.cpu, 0xFFFF) == 0x1B && z80_read(&s.cpu, 0xDFFF) == 0x1B);  /* written through to RAM */
    z80_write(&s.cpu, 0xFFFD, 5);
    assert(z80_read(&s.cpu, 0x0123) == 0xAA && z80_read(&s.cpu, 0x0400) == 5);  /* the first 1 KB stays */
    z80_write(&s.cpu, 0xFFFE, 0x20 + 3);
    assert(z80_read(&s.cpu, 0x4000) == 3);                  /* bank numbers wrap on the ROM's size */
    z80_write(&s.cpu, 0xC123, 0x5A);
    assert(z80_read(&s.cpu, 0xE123) == 0x5A);               /* RAM mirrored at $E000 */
    /* cartridge RAM over slot 2, two banks */
    z80_write(&s.cpu, 0xFFFC, 0x08);
    z80_write(&s.cpu, 0x8000, 0x11);
    z80_write(&s.cpu, 0xFFFC, 0x0C);
    z80_write(&s.cpu, 0x8000, 0x22);
    assert(z80_read(&s.cpu, 0x8000) == 0x22);
    z80_write(&s.cpu, 0xFFFC, 0x08);
    assert(z80_read(&s.cpu, 0x8000) == 0x11 && s.cart.ram_used);
    z80_write(&s.cpu, 0xFFFC, 0x00);
    assert(z80_read(&s.cpu, 0x8000) == 0x1B);               /* ROM again */
}

static void test_codemasters(void) {
    for (unsigned b = 0; b < 16; b++) memset(rom + b * 0x4000, (int)b, 0x4000);
    rom[0x7FE6] = 0x34, rom[0x7FE7] = 0x12, rom[0x7FE8] = 0xCC, rom[0x7FE9] = 0xED;  /* $1234 + $EDCC */
    assert(sms_is_codemasters(rom, 0x40000));
    sms_init(&s, rom, 0x40000, 0);
    assert(z80_read(&s.cpu, 0x8000) == 0);                  /* power-on banks 0, 1, 0 */
    z80_write(&s.cpu, 0x8000, 7);
    z80_write(&s.cpu, 0x0000, 3);
    assert(z80_read(&s.cpu, 0x8000) == 7 && z80_read(&s.cpu, 0x0000) == 3);  /* no fixed 1 KB */
    z80_write(&s.cpu, 0xFFFF, 9);
    assert(z80_read(&s.cpu, 0x8000) == 7);                  /* Sega's registers do nothing */
    memset(rom + 0x7FE0, 0, 16);
    assert(!sms_is_codemasters(rom, 0x40000));
}

static void test_ports(void) {
    sms_init(&s, rom, sizeof rom, 0);
    assert(z80_in(&s.cpu, 0xDC) == 0xFF && z80_in(&s.cpu, 0xDD) == 0xFF);  /* nothing pressed */
    s.pad[0] = PAD_UP | PAD_B;
    s.pad[1] = PAD_DOWN | PAD_A;
    assert(z80_in(&s.cpu, 0xDC) == (uint8_t)~(0x01 | 0x10 | 0x80));
    assert(z80_in(&s.cpu, 0xDD) == (uint8_t)~0x08);         /* pad 2 button 2 */
    assert(z80_in(&s.cpu, 0xC0) == z80_in(&s.cpu, 0xDC));   /* decoded by A7, A6, A0 */
    s.pad[0] = s.pad[1] = 0;
    z80_out(&s.cpu, 0x3F, 0x55);                            /* TH outputs, both low */
    assert((z80_in(&s.cpu, 0xDD) & 0xC0) == 0x00);
    z80_out(&s.cpu, 0x3F, 0xF5);                            /* TH outputs, both high */
    assert((z80_in(&s.cpu, 0xDD) & 0xC0) == 0xC0);
    assert(s.vdp.reg[1] == 0);                              /* $3F is not the VDP */
    s.line = 100;
    assert(z80_in(&s.cpu, 0x7E) == 100);
    s.line = 0xDB;
    assert(z80_in(&s.cpu, 0x7E) == 0xD5);                   /* NTSC: the counter jumps back */
}

static void test_vdp_ports(void) {
    sms_init(&s, rom, sizeof rom, 0);
    vdp_addr(0x1234, 1);
    z80_out(&s.cpu, 0xBE, 0xA5);
    z80_out(&s.cpu, 0xBE, 0x5A);
    assert(s.vdp.vram[0x1234] == 0xA5 && s.vdp.vram[0x1235] == 0x5A);
    vdp_addr(0x1234, 0);                                    /* read: through the buffer */
    assert(z80_in(&s.cpu, 0xBE) == 0xA5 && z80_in(&s.cpu, 0xBE) == 0x5A);
    vdp_reg(10, 0x33);
    assert(s.vdp.reg[10] == 0x33);
    vdp_addr(3, 3);
    z80_out(&s.cpu, 0xBE, 0xFF);
    assert(s.vdp.cram[3] == 0x3F);
    s.vdp.cram[1] = 0x39;                                   /* --BBGGRR: blue 3, green 2, red 1 */
    assert(sms_color(&s, 1) == 0xFF55AAFFu);
}

/* A 16-colour tile and a sprite, drawn on line 0. */
static void test_render(void) {
    sms_init(&s, rom, sizeof rom, 0);
    vdp_reg(1, 0x40);                                       /* display on */
    vdp_reg(2, 0xFF);                                       /* names at $3800 */
    vdp_reg(5, 0xFF);                                       /* sprites at $3F00 */
    s.vdp.vram[0x3800] = 1;                                 /* name (0,0) = tile 1 */
    s.vdp.vram[0x3802] = 1;
    s.vdp.vram[0x3803] = 0x02;                              /* name (1,0) = tile 1, flipped */
    uint8_t *t = &s.vdp.vram[32];                           /* tile 1, row 0: pixel 0 colour 15, pixel 1 colour 5 */
    t[0] = 0xC0, t[1] = 0x80, t[2] = 0xC0, t[3] = 0x80;
    s.vdp.vram[0x3F00] = 0xD0;                              /* no sprites */
    sms_vdp_line(&s, 0);
    assert(s.vdp.frame[0] == 15 && s.vdp.frame[1] == 5 && s.vdp.frame[2] == 0);
    assert(s.vdp.frame[15] == 15 && s.vdp.frame[14] == 5);  /* horizontally flipped */
    vdp_reg(8, 3);                                          /* scrolled 3 pixels right */
    sms_vdp_line(&s, 0);
    assert(s.vdp.frame[3] == 15 && s.vdp.frame[4] == 5);
    vdp_reg(8, 0);
    /* sprite 0 at (40, line 0): Y = 255 (one less), X at $3F80, tile at $3F81 */
    s.vdp.vram[0x3F00] = 0xFF, s.vdp.vram[0x3F01] = 0xD0;
    s.vdp.vram[0x3F80] = 40, s.vdp.vram[0x3F81] = 1;
    sms_vdp_line(&s, 0);
    assert(s.vdp.frame[40] == 16 + 15 && s.vdp.frame[41] == 16 + 5);
    vdp_reg(0, 0x20);                                       /* left column hidden: backdrop */
    vdp_reg(7, 0x04);
    sms_vdp_line(&s, 0);
    assert(s.vdp.frame[0] == 16 + 4 && s.vdp.frame[7] == 16 + 4);
    /* nine sprites on a line: the ninth is dropped and flagged */
    vdp_reg(0, 0);
    for (unsigned i = 0; i < 9; i++) s.vdp.vram[0x3F00 + i] = 0xFF, s.vdp.vram[0x3F80 + i * 2] = (uint8_t)(i * 16), s.vdp.vram[0x3F81 + i * 2] = 1;
    s.vdp.vram[0x3F09] = 0xD0;
    s.vdp.status = 0;
    sms_vdp_line(&s, 0);
    assert(s.vdp.status & 0x40);
    assert(s.vdp.frame[128] != 16 + 15);                    /* the ninth, at 128, not drawn */
}

/* IM 1, EI, then a loop: count the frame interrupts and the T-states. */
static void test_timing(void) {
    memset(rom, 0, 0x8000);
    static const uint8_t main_code[] = {0xED, 0x56, 0xFB, 0x18, 0xFE};  /* IM 1; EI; JR $ */
    memcpy(rom, main_code, sizeof main_code);
    static const uint8_t isr[] = {0xDB, 0xBF, 0x21, 0x00, 0xC0, 0x34, 0xFB, 0xC9};  /* IN A,($BF); INC ($C000); EI; RET */
    memcpy(rom + 0x38, isr, sizeof isr);
    sms_init(&s, rom, 0x8000, 0);
    s.vdp.reg[1] = 0x20;                                    /* frame interrupt on */
    for (int i = 0; i < 10; i++) sms_frame(&s, 0, 0);
    assert(s.ram[0] == 10);                                 /* one a frame */
    assert(s.cpu.tstates >= 10u * 262 * 228 && s.cpu.tstates < 10u * 262 * 228 + 30);
    assert(s.n_samples == 735 || s.n_samples == 736);       /* 44100 / 59.92 */
    /* a line interrupt every 10 lines, lines 0-192 */
    sms_init(&s, rom, 0x8000, 0);
    s.vdp.reg[0] = 0x10, s.vdp.reg[10] = 9;
    sms_frame(&s, 0, 0);
    sms_frame(&s, 0, 0);
    assert(s.ram[0] >= 19 * 2 - 2 && s.ram[0] <= 20 * 2);
    /* Pause: an NMI on the press only */
    sms_init(&s, rom, 0x8000, 0);
    rom[0x66] = 0x3C, rom[0x67] = 0xED, rom[0x68] = 0x45;   /* INC A; RETN */
    s.cpu.a = 0;
    sms_frame(&s, PAD_START, 0);
    sms_frame(&s, PAD_START, 0);
    sms_frame(&s, 0, PAD_START);
    assert(s.cpu.a == 1);
    /* PAL: 313 lines */
    sms_init(&s, rom, 0x8000, 1);
    sms_frame(&s, 0, 0);
    assert(s.cpu.tstates >= 313u * 228 && s.cpu.tstates < 313u * 228 + 30);
}

static void test_state(void) {
    sms_init(&s, rom, 0x8000, 0);
    sms_frame(&s, 0, 0);
    z80_write(&s.cpu, 0xC010, 0x77);
    sms_psg_write(&s.psg, 0x9A);
    assert(sms_save(&s, "sms_test.state") == 0);
    static Sms t;
    sms_init(&t, rom, 0x8000, 0);
    char err[128];
    assert(sms_load(&t, "sms_test.state", err, sizeof err) == 0);
    assert(t.cpu.pc == s.cpu.pc && z80_read(&t.cpu, 0xC010) == 0x77 && t.psg.vol[0] == 10);
    assert(t.cart.data == rom && t.cpu.bus == &t);
    remove("sms_test.state");
}

int main(void) {
    test_header();
    test_memory();
    test_codemasters();
    test_ports();
    test_vdp_ports();
    test_render();
    test_timing();
    test_state();
    puts("Master System runtime tests passed");
    return 0;
}
