/* CPU bus. Memory map: nesdev wiki "CPU memory map". Every CPU cycle makes
 * exactly one bus access (runtime/cpu.c) and is worth 3 PPU dots: 2 before the
 * access and 1 after. The 2/1 split was measured: of 0-3 dots before, only 2
 * passes blargg ppu_vbl_nmi 05-nmi_timing (docs/06).
 *
 * The PPU is caught up lazily (M5b): dots are only counted (cpu.bus_pending) until
 * the CPU touches the PPU ($2000-$3FFF, OAM DMA) or the next point where the
 * PPU acts on the CPU by itself is reached (vblank start: NMI, frame end).
 * Between those points nothing can observe the PPU, so running its dots later
 * in one batch gives the same result; checked by the test ROMs, frame hashes
 * and --diff (docs/06). Mapper IRQs (M7) will add their own catch-up point. */
#include "nes.h"

#include <string.h>

enum { DOTS_BEFORE_ACCESS = 2, LINE = 341, VBLANK_POS = 241 * LINE + 1, FRAME_SHORT = 262 * LINE - 1 };

/* PPU steps needed until vblank start (241, 1) has been processed. Assumes
 * the odd-frame skipped dot, so it can only be one dot early, never late. */
static void plan_next_event(Nes *n) {
    int pos = n->ppu.scanline * LINE + n->ppu.dot;
    int d = VBLANK_POS - pos + 1;
    if (d <= 0) d += FRAME_SHORT;
    n->cpu.bus_budget = d;
}

/* The NMI line only rises at vblank start (or on a $2000/$2002 access, which
 * catches up right away), and a batch never runs past vblank start by more
 * than the current cycle, so one edge check per batch sees every rise. */
static void catch_up(Nes *n) {
    ppu_run(&n->ppu, n->cpu.bus_pending);
    if (n->ppu.nmi_out && !n->nmi_prev) n->cpu.nmi = 1;  /* NMI is edge-triggered */
    n->nmi_prev = n->ppu.nmi_out;
    n->cpu.bus_pending = 0;
    plan_next_event(n);
}

static inline void add_dots(Nes *n, int k) {
    n->cpu.bus_pending += k;
    if (n->cpu.bus_pending >= n->cpu.bus_budget) catch_up(n);
}

void nes_sync(Nes *n) { catch_up(n); }

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
        return (uint8_t)((n->cpu.open_bus & 0xE0) | bit);
    }
    if (a < 0x4020) return n->cpu.open_bus;  /* APU: not implemented yet (M6) */
    return cart_cpu_read(&n->cart, a, n->cpu.open_bus);
}

static int is_ppu(uint16_t a) { return (a & 0xE000) == 0x2000; }

uint8_t cpu_read(Cpu *c, uint16_t a) {
    Nes *n = c->bus;
    if (!is_ppu(a)) {
        add_dots(n, 3);
        return n->cpu.open_bus = read_no_tick(n, a);
    }
    n->cpu.bus_pending += DOTS_BEFORE_ACCESS;
    catch_up(n);
    n->cpu.open_bus = read_no_tick(n, a);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += 3 - DOTS_BEFORE_ACCESS;
    catch_up(n);
    return n->cpu.open_bus;
}

/* $4014: 256 read/write pairs, plus 1 idle cycle (+1 when starting on an odd
 * cycle): 513 or 514 cycles, CPU halted meanwhile (nesdev "PPU OAM"). */
static void oam_dma(Nes *n, uint8_t page) {
    int idle = 1 + (int)(n->cpu.cycles & 1);
    for (int i = 0; i < idle; i++) { n->cpu.cycles++; add_dots(n, 3); }
    for (int i = 0; i < 256; i++) {
        n->cpu.cycles++;
        add_dots(n, 3);
        uint8_t v = n->cpu.open_bus = read_no_tick(n, (uint16_t)(page << 8 | i));
        n->cpu.cycles++;
        n->cpu.bus_pending += 3;
        catch_up(n);  /* OAM is read by sprite evaluation: write at the exact dot */
        ppu_oam_dma_write(&n->ppu, v);
    }
}

void cpu_write(Cpu *c, uint16_t a, uint8_t v) {
    Nes *n = c->bus;
    n->cpu.open_bus = v;
    if (!is_ppu(a) && a != 0x4014) {
        add_dots(n, 3);
        if (a < 0x2000) n->ram[a & 0x7FF] = v;
        else if (a == 0x4016) {
            n->strobe = v & 1;
            if (n->strobe) { n->pad_shift[0] = n->pad[0]; n->pad_shift[1] = n->pad[1]; }
        }
        else if (a >= 0x4020) cart_cpu_write(&n->cart, a, v);
        /* other $4000-$401F: APU, M6 */
        return;
    }
    n->cpu.bus_pending += DOTS_BEFORE_ACCESS;
    catch_up(n);
    if (a == 0x4014) oam_dma(n, v);
    else ppu_reg_write(&n->ppu, a, v);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += 3 - DOTS_BEFORE_ACCESS;
    catch_up(n);
}

/* Fast-path pages for the CPU (cpu.h): 2 KB RAM mirrored over $0000-$1FFF,
 * PRG RAM at $6000-$7FFF, PRG ROM at $8000-$FFFF (NROM: 16 KB mirrored).
 * shortcut: NROM layout only; bank-switching mappers (M7) update these. */
static void map_memory(Nes *n) {
    Cpu *c = &n->cpu;
    for (int i = 0; i < 4; i++) c->read_map[i] = c->write_map[i] = n->ram;
    for (int i = 12; i < 16; i++) c->read_map[i] = c->write_map[i] = n->cart.prg_ram + (i - 12) * 0x800;
    for (int i = 16; i < 32; i++) c->read_map[i] = n->cart.prg + ((i - 16) * 0x800) % n->cart.prg_size;
    c->bus_step = 3;
}

int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom, size, err, errlen) != 0) return -1;
    ppu_reset(&n->ppu, &n->cart);
    n->cpu.bus = n;
    cpu_reset(&n->cpu);
    plan_next_event(n);
    map_memory(n);
    if (recomp_program && recomp_program->prg_hash == recomp_prg_hash(n->cart.prg, n->cart.prg_size))
        n->recomp = recomp_program->lookup;
    return 0;
}


void nes_reset(Nes *n) {
    catch_up(n);
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
