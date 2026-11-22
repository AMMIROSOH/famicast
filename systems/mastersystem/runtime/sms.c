/* The Master System machine: the Z80's memory and I/O, the pads, and a
 * frame's timeline. No BIOS: the cartridge starts at $0000, as games do on a
 * console whose BIOS found them (the mapper's power-on banks 0, 1, 2). */
#include <stdio.h>
#include <string.h>

#include "input_log.h"
#include "sms.h"

/* ---- memory ---- */
static int cart_ram_on(const Sms *s, uint16_t a) { return a >= 0x8000 && a < 0xC000 && (s->cart.page[0] & 0x08); }
static uint8_t *cart_ram(Sms *s, uint16_t a) {
    return &s->cart.ram[(s->cart.page[0] & 0x04 ? 0x4000 : 0) + (a & 0x3FFF)];
}

static uint8_t read_mem(Sms *s, uint16_t a) {
    if (a >= 0xC000) return s->ram[a & 0x1FFF];
    if (cart_ram_on(s, a)) return *cart_ram(s, a);
    uint32_t off = sms_cart_addr(&s->cart, a);
    return off < s->cart.size ? s->cart.data[off] : 0xFF;
}

static void write_mem(Sms *s, uint16_t a, uint8_t v) {
    if (a >= 0xC000) {
        s->ram[a & 0x1FFF] = v;  /* the mapper registers are written through to RAM too */
        if (a >= 0xFFFC && !s->cart.codemasters) s->cart.page[a & 3] = v;
    } else if (cart_ram_on(s, a)) {
        *cart_ram(s, a) = v;
        s->cart.ram_used = 1;
    } else if (s->cart.codemasters && (a & 0x3FFF) == 0) {
        s->cart.page[1 + (a >> 14)] = v;
    }
}

uint8_t z80_fetch(Z80 *z, uint16_t pc) { return read_mem(z->bus, pc); }
uint8_t z80_read(Z80 *z, uint16_t a) { return read_mem(z->bus, a); }
void z80_write(Z80 *z, uint16_t a, uint8_t v) { write_mem(z->bus, a, v); }
void z80_idle(Z80 *z, uint16_t a, unsigned n) { (void)z, (void)a, (void)n; }
uint8_t z80_irq_ack(Z80 *z) { (void)z; return 0xFF; }  /* games use IM 1: RST $38 */

/* ---- I/O: decoded by A7, A6 and A0 (SMS Power, Port map) ---- */
/* $DC: pad 1 up, down, left, right, button 1, button 2, pad 2 up, down;
 * $DD: pad 2 left, right, 1, 2, Reset, -, then the TH lines (0 = pressed). */
static unsigned pad_bits(uint16_t p) {
    return (p & PAD_UP ? 1 : 0) | (p & PAD_DOWN ? 2 : 0) | (p & PAD_LEFT ? 4 : 0) |
           (p & PAD_RIGHT ? 8 : 0) | (p & PAD_B ? 16 : 0) | (p & PAD_A ? 32 : 0);
}

static uint8_t port_dd(const Sms *s) {
    unsigned p2 = pad_bits(s->pad[1]);
    uint8_t v = (uint8_t)~(p2 >> 2) & 0x0F;
    v |= 0x30;  /* Reset not pressed; bit 5 reads 1 */
    /* TH: an output set by $3F reads back its level (export consoles), an input reads 1 */
    v |= s->io_ctrl & 0x02 ? 0x40 : s->io_ctrl & 0x20 ? 0x40 : 0;
    v |= s->io_ctrl & 0x08 ? 0x80 : s->io_ctrl & 0x80 ? 0x80 : 0;
    return v;
}

uint8_t z80_in(Z80 *z, uint16_t port) {
    Sms *s = z->bus;
    switch (port & 0xC1) {
    case 0x40: return sms_vdp_vcount(s);
    case 0x41: return sms_vdp_hcount(s);
    case 0x80: return sms_vdp_read_data(&s->vdp);
    case 0x81: return sms_vdp_read_status(&s->vdp);
    case 0xC0: return (uint8_t)~(pad_bits(s->pad[0]) | (pad_bits(s->pad[1]) & 3) << 6);
    case 0xC1: return port_dd(s);
    default: return 0xFF;
    }
}

void z80_out(Z80 *z, uint16_t port, uint8_t v) {
    Sms *s = z->bus;
    switch (port & 0xC1) {
    case 0x00: s->mem_ctrl = v; break;
    case 0x01: s->io_ctrl = v; break;
    case 0x40: case 0x41: sms_psg_run(&s->psg, z->tstates); sms_psg_write(&s->psg, v); break;
    case 0x80: sms_vdp_write_data(&s->vdp, v); s->writes++; break;
    case 0x81: sms_vdp_write_ctrl(&s->vdp, v); s->writes++; break;
    default: break;
    }
}

/* ---- the machine ---- */
unsigned sms_lines(const Sms *s) { return s->pal ? SMS_LINES_PAL : SMS_LINES_NTSC; }

void sms_reset(Sms *s) {
    memset(&s->cpu, 0, sizeof s->cpu);
    s->cpu.bus = s;
    z80_reset(&s->cpu);
    s->cpu.sp = 0xDFF0;  /* where the BIOS leaves it */
    memset(s->ram, 0, sizeof s->ram);
    memset(&s->vdp, 0, sizeof s->vdp);
    s->cart.page[0] = 0, s->cart.page[1] = 0, s->cart.page[2] = 1, s->cart.page[3] = s->cart.codemasters ? 0 : 2;
    s->mem_ctrl = 0xAB, s->io_ctrl = 0xFF;
    s->pause = 0;
    s->line = 0;
    s->line_start = 0;
    s->frames = 0;
    s->samples_made = 0;
    s->n_samples = 0;
    sms_psg_reset(&s->psg);
}

int sms_init(Sms *s, const uint8_t *rom, size_t size, int pal) {
    if (!rom || !size) return 1;
    memset(s, 0, sizeof *s);
    s->cart.data = rom;
    s->cart.size = size;
    s->cart.codemasters = (uint8_t)sms_is_codemasters(rom, size);
    s->pal = pal;
    sms_reset(s);
    return 0;
}

static uint64_t sample_time(const Sms *s, uint64_t n) {
    return n * (uint64_t)(s->pal ? SMS_CLOCK_PAL : SMS_CLOCK_NTSC) / SMS_SAMPLE_RATE;
}

void sms_frame(Sms *s, uint16_t pad0, uint16_t pad1) {
    s->pad[0] = pad0, s->pad[1] = pad1;
    int pause = ((pad0 | pad1) & PAD_START) != 0;
    if (pause && !s->pause) z80_nmi(&s->cpu);  /* Pause is the Z80's NMI, on the press */
    s->pause = (uint8_t)pause;
    s->n_samples = 0;
    unsigned lines = sms_lines(s);
    for (unsigned line = 0; line < lines; line++) {
        s->line = line;
        sms_vdp_line(s, line);
        uint64_t end = s->line_start + SMS_LINE_TSTATES;
        while (s->cpu.tstates < end)
            if (!sms_vdp_irq(&s->vdp) || !z80_interrupt(&s->cpu)) z80_step(&s->cpu);
        for (uint64_t t; (t = sample_time(s, s->samples_made + 1)) <= end; s->samples_made++) {
            sms_psg_run(&s->psg, t);
            if (s->n_samples < sizeof s->samples / sizeof s->samples[0])
                s->samples[s->n_samples++] = sms_psg_take(&s->psg);
        }
        s->line_start = end;
    }
    s->frames++;
}

/* ---- save states: the machine without the ROM pointer ---- */
static const char MAGIC[8] = {'S', 'M', 'S', 'S', 'T', 'A', 'T', '1'};

int sms_save(const Sms *s, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    Sms copy = *s;
    copy.cart.data = NULL;
    copy.cpu.bus = NULL;
    uint32_t size = sizeof copy;
    int bad = fwrite(MAGIC, 1, 8, f) != 8 || fwrite(&size, 4, 1, f) != 1 || fwrite(&copy, 1, sizeof copy, f) != sizeof copy;
    return fclose(f) || bad;
}

int sms_load(Sms *s, const char *path, char *err, size_t errlen) {
    static Sms loaded;
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot open state %s", path); return 1; }
    char magic[8];
    uint32_t size = 0;
    int bad = fread(magic, 1, 8, f) != 8 || memcmp(magic, MAGIC, 8) || fread(&size, 4, 1, f) != 1 ||
              size != sizeof loaded || fread(&loaded, 1, sizeof loaded, f) != sizeof loaded;
    fclose(f);
    if (bad) { snprintf(err, errlen, "%s: not a state of this version", path); return 1; }
    const uint8_t *rom = s->cart.data;
    size_t rom_size = s->cart.size;
    *s = loaded;
    s->cart.data = rom;
    s->cart.size = rom_size;
    s->cpu.bus = s;
    return 0;
}
