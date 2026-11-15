/* CPU bus. Memory map: nesdev wiki "CPU memory map". Every CPU cycle makes
 * exactly one bus access (runtime/cpu.c), so the PPU is advanced here: 3 dots
 * per CPU cycle, 2 before the access and 1 after. The 2/1 split was measured:
 * of 0-3 dots before, only 2 passes blargg ppu_vbl_nmi 05-nmi_timing (docs/06). */
#include "nes.h"

#include <string.h>

enum { DOTS_BEFORE_ACCESS = 2 };

static void dots(Nes *n, int k) {
    for (int i = 0; i < k; i++) {
        ppu_step(&n->ppu);
        if (n->ppu.nmi_out && !n->nmi_prev) n->cpu.nmi = 1;  /* NMI is edge-triggered */
        n->nmi_prev = n->ppu.nmi_out;
    }
}
static void tick(Nes *n) { dots(n, 3); }  /* a whole CPU cycle (DMA) */

static void after_ppu_access(Nes *n) {
    if (n->ppu.nmi_cancel) { n->cpu.nmi = 0; n->ppu.nmi_cancel = 0; }
}

static uint8_t read_no_tick(Nes *n, uint16_t a) {
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a < 0x4000) return ppu_reg_read(&n->ppu, a);
    if (a == 0x4016 || a == 0x4017) {
        int i = a & 1;
        uint8_t bit;
        if (n->strobe) bit = n->pad[i] & 1;
        else { bit = n->pad_shift[i] & 1; n->pad_shift[i] = (uint8_t)(n->pad_shift[i] >> 1 | 0x80); }
        return (uint8_t)((n->open_bus & 0xE0) | bit);
    }
    if (a < 0x4020) return n->open_bus;  /* APU: not implemented yet (M6) */
    return cart_cpu_read(&n->cart, a, n->open_bus);
}

uint8_t cpu_read(Cpu *c, uint16_t a) {
    Nes *n = c->bus;
    dots(n, DOTS_BEFORE_ACCESS);
    n->open_bus = read_no_tick(n, a);
    after_ppu_access(n);
    dots(n, 3 - DOTS_BEFORE_ACCESS);
    return n->open_bus;
}

/* $4014: 256 read/write pairs, plus 1 idle cycle (+1 when starting on an odd
 * cycle): 513 or 514 cycles, CPU halted meanwhile (nesdev "PPU OAM"). */
static void oam_dma(Nes *n, uint8_t page) {
    int idle = 1 + (int)(n->cpu.cycles & 1);
    for (int i = 0; i < idle; i++) { n->cpu.cycles++; tick(n); }
    for (int i = 0; i < 256; i++) {
        n->cpu.cycles++;
        tick(n);
        uint8_t v = n->open_bus = read_no_tick(n, (uint16_t)(page << 8 | i));
        n->cpu.cycles++;
        tick(n);
        ppu_oam_dma_write(&n->ppu, v);
    }
}

void cpu_write(Cpu *c, uint16_t a, uint8_t v) {
    Nes *n = c->bus;
    dots(n, DOTS_BEFORE_ACCESS);
    n->open_bus = v;
    if (a < 0x2000) n->ram[a & 0x7FF] = v;
    else if (a < 0x4000) ppu_reg_write(&n->ppu, a, v);
    else if (a == 0x4014) oam_dma(n, v);
    else if (a == 0x4016) {
        n->strobe = v & 1;
        if (n->strobe) { n->pad_shift[0] = n->pad[0]; n->pad_shift[1] = n->pad[1]; }
    }
    else if (a >= 0x4020) cart_cpu_write(&n->cart, a, v);
    /* other $4000-$401F: APU, M6 */
    after_ppu_access(n);
    dots(n, 3 - DOTS_BEFORE_ACCESS);
}

int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom, size, err, errlen) != 0) return -1;
    ppu_reset(&n->ppu, &n->cart);
    n->cpu.bus = n;
    cpu_reset(&n->cpu);
    if (recomp_program && recomp_program->prg_hash == recomp_prg_hash(n->cart.prg, n->cart.prg_size))
        n->recomp = recomp_program->lookup;
    return 0;
}


void nes_reset(Nes *n) {
    n->ppu.ctrl = n->ppu.mask = 0;
    n->ppu.w = 0;
    cpu_reset(&n->cpu);
}

/* Dispatcher: recompiled block when there is one, else the interpreter.
 * Interrupt entry and a jammed CPU always go through cpu_step. */
void nes_step(Nes *n) {
    Cpu *c = &n->cpu;
    if (n->recomp && !c->poll_prev && !c->jammed) {
        RecompFn f = n->recomp(c->pc);
        if (f) {
            f(c);
            n->steps_native++;
            return;
        }
        if (n->misses) n->misses[c->pc]++;
    }
    if (c->poll_prev) n->steps_interrupt++;
    else n->steps_interp++;
    cpu_step(c);
}

void nes_run_frame(Nes *n) {
    n->ppu.frame_done = 0;
    while (!n->ppu.frame_done && !n->cpu.jammed) nes_step(n);
}

uint8_t nes_peek(Nes *n, uint16_t a) {
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a >= 0x6000) return cart_cpu_read(&n->cart, a, 0);
    return 0;
}
