#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_ops.h"    /* rd */
#include "input_log.h"  /* PAD_* */
#include "recomp_rt.h"

/* ---- timer (Pan Docs "Timer obscure behaviour") ---- */
static int timer_bit(uint16_t div, uint8_t tac) {
    static const uint16_t BIT[4] = {1 << 9, 1 << 3, 1 << 5, 1 << 7};  /* 4096, 262144, 65536, 16384 Hz */
    return (tac & 4) && (div & BIT[tac & 3]);
}
static void timer_inc(Gb *g) {
    if (++g->tima == 0) g->tima_reload = 1;  /* TIMA reads 0 for a cycle, then TMA */
}

/* A byte as the DMA unit reads it: no side effects; $E000-$FFFF reach WRAM. */
static uint8_t dma_peek(Gb *g, uint16_t a) {
    if (a < 0x8000 || (a >= 0xA000 && a < 0xC000)) return gb_cart_read(&g->cart, a);
    if (a < 0xA000) return g->vram[a & 0x1FFF];
    return g->wram[a & 0x1FFF];
}

/* One M-cycle of everything but the CPU. */
static void tick(Gb *g) {
    if (g->tima_reload == 2) g->tima_reload = 0;
    else if (g->tima_reload == 1) g->tima = g->tma, g->ifl |= INT_TIMER, g->tima_reload = 2;
    int before = timer_bit(g->div, g->tac);
    uint16_t old = g->div;
    g->div += 4;
    if (before && !timer_bit(g->div, g->tac)) timer_inc(g);
    if (old & ~g->div & 0x1000) apu_frame_step(g);  /* DIV bit 4 fell */

    if (g->serial_left && !--g->serial_left) g->sb = 0xFF, g->sc &= 0x7F, g->ifl |= INT_SERIAL;

    if (g->dma_start && !--g->dma_start) g->dma_on = 1, g->dma_pos = 0, g->dma_src = g->dma_next;
    if (g->dma_on) {  /* 160 cycles that each move a byte; the bus is free after the last */
        if (g->dma_pos == 0xA0) g->dma_on = 0;
        else g->oam[g->dma_pos] = dma_peek(g, (uint16_t)(g->dma_src + g->dma_pos)), g->dma_pos++;
    }

    ppu_tick(g);
    apu_tick(g);
    gb_cart_tick(&g->cart);
}

/* ---- registers ---- */
static uint8_t io_read(Gb *g, uint16_t a) {
    switch (a) {
    case 0xFF00: {  /* buttons pressed read 0 in the selected group(s) */
        uint8_t low = 0x0F, p = g->pad;
        if (!(g->p1 & 0x10)) low &= (uint8_t)~((p >> 7 & 1) | (p >> 6 & 1) << 1 | (p >> 4 & 1) << 2 | (p >> 5 & 1) << 3);
        if (!(g->p1 & 0x20)) low &= (uint8_t)~(p & 0x0F);
        return (uint8_t)(0xC0 | g->p1 | low);
    }
    case 0xFF01: return g->sb;
    case 0xFF02: return g->sc | 0x7E;
    case 0xFF04: return (uint8_t)(g->div >> 8);
    case 0xFF05: return g->tima;
    case 0xFF06: return g->tma;
    case 0xFF07: return g->tac | 0xF8;
    case 0xFF0F: return g->ifl | 0xE0;
    case 0xFF40: return g->lcdc;
    case 0xFF41: return (uint8_t)(0x80 | g->stat | ((g->lcdc & 0x80) && g->ly == g->lyc ? 4 : 0) | ppu_mode(g));
    case 0xFF42: return g->scy;
    case 0xFF43: return g->scx;
    case 0xFF44: return g->ly;
    case 0xFF45: return g->lyc;
    case 0xFF46: return g->dma_reg;
    case 0xFF47: case 0xFF48: case 0xFF49: return g->io[a & 0x7F];
    case 0xFF4A: return g->wy;
    case 0xFF4B: return g->wx;
    }
    if (a >= 0xFF10 && a < 0xFF40) return apu_read(g, a);
    return 0xFF;
}

static void io_write(Gb *g, uint16_t a, uint8_t v) {
    switch (a) {
    case 0xFF00: g->p1 = v & 0x30; return;
    case 0xFF01: g->sb = v; return;
    case 0xFF02:
        g->sc = v & 0x81;
        if ((v & 0x81) == 0x81) {  /* internal clock: send SB */
            if (g->serial_n < (int)sizeof g->serial_out - 1) g->serial_out[g->serial_n++] = (char)g->sb;
            g->serial_left = 8 * 128;
        }
        return;
    case 0xFF04: {  /* reset the counter; a falling edge on the way counts */
        int before = timer_bit(g->div, g->tac);
        if (g->div & 0x1000) apu_frame_step(g);
        g->div = 0;
        if (before) timer_inc(g);
        return;
    }
    case 0xFF05:
        if (g->tima_reload == 1) g->tima_reload = 0;  /* cancels the reload and the interrupt */
        if (g->tima_reload != 2) g->tima = v;           /* in the reload cycle TMA wins */
        return;
    case 0xFF06:
        g->tma = v;
        if (g->tima_reload == 2) g->tima = v;
        return;
    case 0xFF07: {  /* switching the selected bit or disabling can make an edge */
        int before = timer_bit(g->div, g->tac);
        g->tac = v & 7;
        if (before && !timer_bit(g->div, g->tac)) timer_inc(g);
        return;
    }
    case 0xFF0F: g->ifl = v & 0x1F; return;
    case 0xFF40: ppu_lcdc_write(g, v); return;
    case 0xFF41: ppu_stat_write(g, v); return;
    case 0xFF42: g->scy = v; return;
    case 0xFF43: g->scx = v; return;
    case 0xFF44: return;  /* LY is read-only */
    case 0xFF45: g->lyc = v; ppu_stat_update(g); return;
    case 0xFF46: g->dma_reg = v, g->dma_next = (uint16_t)(v << 8), g->dma_start = 2; return;  /* mooneye oam_dma_start: the next cycle still free */
    case 0xFF4A: g->wy = v; return;
    case 0xFF4B: g->wx = v; return;
    }
    if (a >= 0xFF10 && a < 0xFF40) apu_write(g, a, v);
    else if (a >= 0xFF47 && a <= 0xFF49) g->io[a & 0x7F] = v;
}

static uint8_t bus_read(Gb *g, uint16_t a) {
    if (a < 0x8000) return gb_cart_read(&g->cart, a);
    if (a < 0xA000) return g->vram[a & 0x1FFF];
    if (a < 0xC000) return gb_cart_read(&g->cart, a);
    if (a < 0xFE00) return g->wram[a & 0x1FFF];   /* $E000-$FDFF: echo of WRAM */
    if (a < 0xFEA0) return g->oam[a - 0xFE00];
    if (a < 0xFF00) return 0x00;                  /* unusable area (DMG reads 0) */
    if (a == 0xFFFF) return g->ie;
    if (a >= 0xFF80) return g->hram[a - 0xFF80];
    return io_read(g, a);
}

static void bus_write(Gb *g, uint16_t a, uint8_t v) {
    if (a < 0x8000) gb_cart_write(&g->cart, a, v);
    else if (a < 0xA000) g->vram[a & 0x1FFF] = v;
    else if (a < 0xC000) gb_cart_write(&g->cart, a, v);
    else if (a < 0xFE00) g->wram[a & 0x1FFF] = v;
    else if (a < 0xFEA0) g->oam[a - 0xFE00] = v;
    else if (a < 0xFF00) return;
    else if (a == 0xFFFF) g->ie = v;
    else if (a >= 0xFF80) g->hram[a - 0xFF80] = v;
    else io_write(g, a, v);
}

/* ---- the CPU's bus: each call is one M-cycle; the rest of the machine runs first ---- */
/* During OAM DMA the unit owns one bus: the video bus ($8000-$9FFF) or
 * the external one (cartridge, WRAM). A CPU access on that bus gets the
 * byte the DMA is moving (writes are lost); OAM reads $FF; the other bus,
 * I/O and HRAM work as usual. */
static int video_bus(uint16_t a) { return a >= 0x8000 && a < 0xA000; }
static int dma_conflict(const Gb *g, uint16_t a) {
    return g->dma_on && a < 0xFF00 && (a >= 0xFE00 || video_bus(a) == video_bus(g->dma_src));
}
/* The PPU owns VRAM in mode 3 and OAM in modes 2 and 3: the CPU reads $FF
 * and its writes are lost (Pan Docs "Accessing VRAM and OAM"). */
static int ppu_owns(const Gb *g, uint16_t a) {
    int mode = ppu_mode(g);
    return (video_bus(a) && mode == 3) || (a >= 0xFE00 && a < 0xFEA0 && mode >= 2);
}
uint8_t gb_read(GbCpu *c, uint16_t a) {
    Gb *g = c->bus;
    tick(g);
    if (dma_conflict(g, a)) return a >= 0xFE00 ? 0xFF : dma_peek(g, (uint16_t)(g->dma_src + g->dma_pos));
    if (ppu_owns(g, a)) return 0xFF;
    return bus_read(g, a);
}
void gb_write(GbCpu *c, uint16_t a, uint8_t v) {
    Gb *g = c->bus;
    tick(g);
    if (!dma_conflict(g, a) && !ppu_owns(g, a)) bus_write(g, a, v);
}
void gb_idle(GbCpu *c) { tick(c->bus); }

/* Interrupt entry asks after pushing PC's high byte: that push can change
 * IE (SP = $0000), and then no interrupt is taken (PC = 0). */
uint16_t gb_irq_ack(GbCpu *c) {
    Gb *g = c->bus;
    uint8_t p = g->ie & g->ifl & 0x1F;
    if (!p) return 0x0000;
    int bit = 0;
    while (!(p & 1 << bit)) bit++;
    g->ifl &= (uint8_t)~(1 << bit);
    return (uint16_t)(0x40 + 8 * bit);
}

/* A step: interrupt entry, a halted cycle, or code: a recompiled block when
 * there is one for this PC and bank, else one instruction interpreted. */
void gb_step(Gb *g) {
    GbCpu *c = &g->cpu;
    uint8_t pending = g->ie & g->ifl & 0x1F;
    if (c->halted && !pending) {
        /* halted, the CPU keeps reading the opcode at PC; an interrupt that
         * turns up in that cycle wakes it: with IME on the entry follows at
         * once, with IME off the next instruction is fetched in the next
         * cycle (mooneye halt_ime0_nointr_timing, halt_ime1_timing) */
        rd(c, c->pc);
        if (!(g->ie & g->ifl & 0x1F)) return;
        c->halted = 0;
        if (c->ime) gb_cpu_interrupt(c);
        return;
    }
    if (c->halted) c->halted = 0;  /* any enabled interrupt wakes HALT, IME or not */
    if (c->ime && pending) {
        gb_cpu_interrupt(c);
        return;
    }
    uint64_t before = c->cycles;
    GbRecompFn f = NULL;
    if (g->recomp_on && c->pc < 0x8000 && !c->halt_bug && !c->stopped && !c->jammed)
        f = gb_recomp_program->lookup(c->pc, gb_cart_bank(&g->cart, c->pc));
    if (f) {
        if (c->ei) c->ei = 0, c->ime = 1;  /* as gb_cpu_step: EI ends blocks, so only here */
        f(c);
        g->cycles_native += c->cycles - before;
    } else {
        if (g->misses && c->pc < 0x8000 && !c->stopped && !c->jammed)
            g->misses[gb_cart_bank(&g->cart, c->pc) * 0x8000 + c->pc] = 1;
        gb_cpu_step(c);
        g->cycles_interp += c->cycles - before;
    }
    /* HALT with IME off and an interrupt already pending does not halt; the
     * next opcode byte is read twice (the "HALT bug") */
    if (c->halted && !c->ime && (g->ie & g->ifl & 0x1F)) c->halted = 0, c->halt_bug = 1;
}

void gb_run_frame(Gb *g) {
    g->frame_end += GB_FRAME_CYCLES;
    while (g->cpu.cycles < g->frame_end) gb_step(g);
}

void gb_set_pad(Gb *g, uint8_t pad) {
    if (pad & ~g->pad) g->ifl |= INT_JOYPAD, g->cpu.stopped = 0;  /* a new press */
    g->pad = pad;
}

int gb_load(Gb *g, const uint8_t *rom, size_t size, char *err, size_t errlen) {
    memset(g, 0, sizeof *g);
    if (gb_cart_load(&g->cart, rom, size, err, errlen) != 0) return -1;
    GbCpu *c = &g->cpu;
    c->bus = g;
    /* what the DMG boot ROM leaves (Pan Docs "Power Up Sequence"): F = $B0
     * with a correct header checksum, else H and C too */
    c->a = 0x01, c->f = 0xB0, c->b = 0x00, c->c = 0x13, c->d = 0x00, c->e = 0xD8, c->h = 0x01, c->l = 0x4D;
    c->sp = 0xFFFE, c->pc = 0x0100;
    g->div = 0xABC8;  /* DIV $AB; the low bits from mooneye boot_div-dmgABCmgb (the only value that passes) */
    g->tac = 0x00;
    g->ifl = INT_VBLANK;
    g->lcdc = 0x91;
    g->mode = 2;
    g->dma_reg = 0xFF;
    apu_boot(g);
    g->recomp_on = gb_recomp_program && gb_recomp_program->rom_hash == gb_rom_hash(g);
    g->io[0x47] = 0xFC, g->io[0x48] = 0xFF, g->io[0x49] = 0xFF;
    return 0;
}

void gb_free(Gb *g) { gb_cart_free(&g->cart); }

/* ---- save states: a header, then the Gb struct as it is in memory (the
 * same build reads it back; a field-wise format when states must outlive
 * program updates) ---- */
enum { GB_STATE_VERSION = 1 };
typedef struct { char magic[4]; uint32_t version, size, rom_hash; } GbStateHeader;

uint32_t gb_rom_hash(const Gb *g) {  /* FNV-1a */
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < g->cart.rom_size; i++) h = (h ^ g->cart.rom[i]) * 16777619u;
    return h;
}

int gb_state_save(const Gb *g, const char *path) {
    GbStateHeader h = {{'G', 'B', 'S', 'S'}, GB_STATE_VERSION, (uint32_t)sizeof *g, gb_rom_hash(g)};
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(g, sizeof *g, 1, f) == 1;
    return fclose(f) == 0 && ok ? 0 : -1;
}

int gb_state_load(Gb *g, const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) return snprintf(err, errlen, "cannot open"), -1;
    GbStateHeader h;
    Gb *s = malloc(sizeof *s);
    int ok = s && fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "GBSS", 4);
    if (!ok) snprintf(err, errlen, "not a save state");
    else if (h.version != GB_STATE_VERSION || h.size != sizeof *g)
        ok = 0, snprintf(err, errlen, "saved by a different version of the program");
    else if (h.rom_hash != gb_rom_hash(g))
        ok = 0, snprintf(err, errlen, "saved with a different game");
    else if (fread(s, sizeof *s, 1, f) != 1)
        ok = 0, snprintf(err, errlen, "file is truncated");
    fclose(f);
    if (ok) {  /* everything but what belongs to this process: the ROM copy, the CPU's link to its bus,
                * this program's recompiled code and the misses being recorded */
        s->cart.rom = g->cart.rom;
        s->recomp_on = g->recomp_on, s->misses = g->misses;
        *g = *s;
        g->cpu.bus = g;
    }
    free(s);
    return ok ? 0 : -1;
}

void gb_misses_write(const Gb *g, FILE *f) {
    for (size_t i = 0; g->misses && i < gb_miss_slots(g); i++)
        if (g->misses[i]) fprintf(f, "MISS $%04X bank %u\n", (unsigned)(i & 0x7FFF), (unsigned)(i / 0x8000));
}
