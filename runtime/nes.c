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
 * and --diff (docs/06). The APU (M6) is caught up the same way; its events
 * are the frame IRQ and DMC sample fetches. The MMC3 IRQ (M7) asserts on a
 * PPU A12 rise: while it is armed, the PPU is caught up every cycle around
 * the sprite fetches (dots 255-330 of rendering lines). */
#include "nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DOTS_BEFORE_ACCESS = 2, LINE = 341, VBLANK_POS = 241 * LINE + 1, FRAME_SHORT = 262 * LINE - 1 };

/* How many dots may pile up before the bus must catch up: until vblank
 * start (241, 1) has been processed (assuming the odd-frame skipped dot, so
 * at most one dot early), or until the APU's next event, whichever is first. */
static void plan_next_event(Nes *n) {
    int pos = n->ppu.scanline * LINE + n->ppu.dot;
    int64_t d = VBLANK_POS - pos + 1;
    if (d <= 0) d += FRAME_SHORT;
    int64_t apu = n->cpu.bus_pending + 3 * apu_cycles_to_event(&n->apu);
    if (apu < d) d = apu;
    /* MMC3: the counter is clocked on A12 rises during rendering (around dot
     * 260 or 324, or at dot 5 of the pre-render line, depending on the
     * pattern tables). While the IRQ is enabled, check the counter at dots 0
     * and 255 of every rendering line; once the next clock can fire, catch
     * up every cycle until the line ends, so a batch never runs past it. */
    if (n->cart.mapper == 4 && n->cart.irq_enabled && (n->ppu.mask & 0x18)) {
        int line = n->ppu.scanline, dot = n->ppu.dot;
        int rendering_line = line < 240 || line == 261;
        int64_t m;
        if (rendering_line && cart_irq_armed(&n->cart)) m = 3;
        else if (rendering_line && dot < 255) m = 255 - dot;
        else {  /* dot 0 of the next rendering line */
            int next = line < 239 ? line + 1 : (line == 261 ? 0 : 261);
            m = ((next - line + 262) % 262) * LINE - dot;
        }
        m += n->cpu.bus_pending;
        if (m < d) d = m;
    }
    if (n->oam_dma_pending) d = n->cpu.bus_pending;  /* next access: slow path, which runs the DMA */
    n->cpu.bus_budget = (int32_t)d;
}

/* APU order within CPU cycle N: the bus access sees the APU as of N-1, then
 * the APU clocks cycle N, then the CPU samples IRQ. So catch-ups run the APU
 * to N-1, and every slow-path access finishes with sync_apu() (through N). */
static void sync_apu(Nes *n) {
    apu_run(&n->apu, n->cpu.cycles - 1);
    n->cpu.irq = apu_irq(&n->apu) || n->cart.irq_line;  /* APU IRQ lags one cycle, like $4015 */
    apu_run(&n->apu, n->cpu.cycles);
}

/* The NMI line only rises at vblank start (or on a $2000/$2002 access, which
 * catches up right away), and a batch never runs past vblank start by more
 * than the current cycle, so one edge check per batch sees every rise. */
static void catch_up(Nes *n) {
    ppu_run(&n->ppu, n->cpu.bus_pending);
    n->cpu.yield |= n->ppu.frame_done;  /* the frame loop needs control back */
    if (n->ppu.nmi_out && !n->nmi_prev) n->cpu.nmi = 1;  /* NMI is edge-triggered */
    n->nmi_prev = n->ppu.nmi_out;
    n->cpu.bus_pending = 0;
    apu_run(&n->apu, n->cpu.cycles - 1);
    n->cpu.irq = apu_irq(&n->apu) || n->cart.irq_line;
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
    if (a == 0x4015) {
        /* sees the APU as of the previous cycle (blargg apu 07.irq_flag_timing:
         * the flag set "at 29830" reads as set from 29831) */
        apu_run(&n->apu, n->cpu.cycles - 1);
        return apu_read_status(&n->apu, n->cpu.open_bus);
    }
    if (a < 0x4020) return n->cpu.open_bus;  /* write-only APU registers */
    return cart_cpu_read(&n->cart, a, n->cpu.open_bus);
}

static int is_ppu(uint16_t a) { return (a & 0xE000) == 0x2000; }
static void map_memory(Nes *n);

/* One bus read in the current cycle (n->cpu.cycles already counts it), with
 * every side effect a CPU read has. The CPU and the DMA unit both use it. */
static uint8_t read_cycle(Nes *n, uint16_t a) {
    if (!is_ppu(a)) {
        add_dots(n, 3);
        n->cpu.open_bus = read_no_tick(n, a);
        sync_apu(n);
        plan_next_event(n);
        return n->cpu.open_bus;
    }
    n->cpu.bus_pending += DOTS_BEFORE_ACCESS;
    catch_up(n);
    n->cpu.open_bus = read_no_tick(n, a);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += 3 - DOTS_BEFORE_ACCESS;
    catch_up(n);
    sync_apu(n);
    plan_next_event(n);
    return n->cpu.open_bus;
}

/* DMA (nesdev "DMA"; the same model as Mesen2). OAM DMA ($4014) and DMC
 * sample fetches halt the CPU on its next read cycle. That read is performed
 * (the halt cycle) and repeated on every later cycle the DMA unit does not
 * use, then once more for real when the CPU resumes; repeated $4016/$4017
 * reads do not clock the controller again. The DMA unit reads on "get"
 * cycles (even cycle numbers here) and OAM DMA writes on "put" cycles:
 * OAM DMA = halt + optional alignment + 256 get/put pairs = 513 or 514
 * cycles; a DMC fetch alone = halt + dummy + optional alignment + get = 3
 * or 4. During OAM DMA, OAM cycles stand in for the DMC's halt and dummy
 * cycles, so its fetch usually costs 2 more. Interrupts arriving meanwhile
 * are seen by the resumed instruction's poll (cpu_interrupts_v2
 * 4-irq_and_dma). Checked by sprdma_and_dmc_dma and dmc_dma_during_read4. */
static void run_dma(Nes *n, uint16_t a) {
    int dmc = n->apu.dmc.dma_request, dmc_halt = 0, dmc_dummy = dmc;
    int oam = n->oam_dma_pending, count = 0;  /* count: OAM bytes read + written */
    int quiet = a == 0x4016 || a == 0x4017;
    uint8_t value = 0;
    n->oam_dma_pending = 0;
    read_cycle(n, a);  /* halt cycle */
    while (dmc || oam || n->apu.dmc.dma_request) {  /* a request in the last cycle joins in */
        n->cpu.cycles++;
        int get = !(n->cpu.cycles & 1);
        if (!dmc && n->apu.dmc.dma_request) dmc = dmc_halt = dmc_dummy = 1;  /* request during OAM DMA */
        int ready = dmc && !dmc_halt && !dmc_dummy;
        if (dmc_halt) dmc_halt = 0;    /* this cycle serves as the DMC's halt... */
        else if (dmc_dummy) dmc_dummy = 0;  /* ...or its dummy cycle */
        if (get && ready) {
            uint8_t v = read_cycle(n, n->apu.dmc.addr);
            apu_dmc_fill(&n->apu, v);
            n->cpu.irq = apu_irq(&n->apu) || n->cart.irq_line;
            plan_next_event(n);
            dmc = 0;
        } else if (get && oam) {
            value = read_cycle(n, (uint16_t)(n->oam_dma_page << 8 | count / 2));
            count++;
        } else if (!get && oam && (count & 1)) {
            n->cpu.bus_pending += 3;
            catch_up(n);  /* OAM is read by sprite evaluation: write at the exact dot */
            ppu_oam_dma_write(&n->ppu, value);
            sync_apu(n);
            plan_next_event(n);
            if (++count == 512) oam = 0;
        } else if (quiet) {  /* alignment / dummy cycle, controller not clocked again */
            add_dots(n, 3);
            sync_apu(n);
            plan_next_event(n);
        } else {
            read_cycle(n, a);  /* alignment / dummy cycle: the halted read again */
        }
    }
    n->cpu.cycles++;  /* the CPU's own read, done by the caller */
}

uint8_t cpu_read(Cpu *c, uint16_t a) {
    Nes *n = c->bus;
    if (n->apu.dmc.dma_request || n->oam_dma_pending) run_dma(n, a);
    return read_cycle(n, a);
}

void cpu_write(Cpu *c, uint16_t a, uint8_t v) {
    Nes *n = c->bus;
    n->cpu.open_bus = v;
    int mapper_reg = a >= 0x8000 && n->cart.mapper != 0;
    if (!is_ppu(a) && a != 0x4014 && !mapper_reg) {
        add_dots(n, 3);
        if (a < 0x2000) n->ram[a & 0x7FF] = v;
        else if (a == 0x4016) {
            n->strobe = v & 1;
            if (n->strobe) { n->pad_shift[0] = n->pad[0]; n->pad_shift[1] = n->pad[1]; }
        }
        else if (a >= 0x4020) cart_cpu_write(&n->cart, a, v, n->cpu.cycles);
        else if (a <= 0x4017) {  /* APU registers ($4014/$4016 handled above) */
            sync_apu(n);
            apu_write(&n->apu, a, v);
        }
        sync_apu(n);
        plan_next_event(n);
        return;
    }
    n->cpu.bus_pending += DOTS_BEFORE_ACCESS;
    catch_up(n);
    if (a == 0x4014) n->oam_dma_page = v, n->oam_dma_pending = 1;  /* runs at the next read (run_dma) */
    else if (mapper_reg) {  /* bank switches can be raster effects: PPU caught up first */
        if (cart_cpu_write(&n->cart, a, v, n->cpu.cycles)) map_memory(n);
    }
    else ppu_reg_write(&n->ppu, a, v);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += 3 - DOTS_BEFORE_ACCESS;
    catch_up(n);
    sync_apu(n);
    plan_next_event(n);
}

/* Fast-path pages for the CPU (cpu.h): 2 KB RAM mirrored over $0000-$1FFF,
 * PRG RAM at $6000-$7FFF, PRG ROM at $8000-$FFFF as the mapper has banked it
 * (rebuilt after every mapper write). */
static void map_memory(Nes *n) {
    Cpu *c = &n->cpu;
    for (int i = 0; i < 4; i++) c->read_map[i] = c->write_map[i] = n->ram;
    for (int i = 12; i < 16; i++) c->read_map[i] = c->write_map[i] = n->cart.prg_ram + (i - 12) * 0x800;
    for (int i = 16; i < 32; i++)  /* 8 KB windows from the mapper, 4 pages each */
        c->read_map[i] = n->cart.prg + n->cart.prg_off[(i - 16) >> 2] + ((i & 3) * 0x800);
    c->bus_step = 3;
}

int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom, size, err, errlen) != 0) return -1;
    ppu_reset(&n->ppu, &n->cart);
    n->cpu.bus = n;
    apu_reset(&n->apu, &n->cart, 1);
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
    apu_reset(&n->apu, &n->cart, 0);
    cpu_reset(&n->cpu);
    plan_next_event(n);
}

/* Dispatcher: recompiled block when there is one, else the interpreter.
 * Interrupt entry and a jammed CPU always go through cpu_step. */
void nes_step(Nes *n) {
    Cpu *c = &n->cpu;
    uint64_t before = c->cycles;
    if (n->recomp && !c->poll_prev && !c->jammed) {
        RecompFn f = n->recomp(c->pc, nes_prg_bank(n, c->pc));
        if (f) {
            f(c);
            n->steps_native++;
            n->cycles_native += c->cycles - before;
            return;
        }
    }
    int kind = c->poll_prev ? FALLBACK_INTERRUPT : c->pc < 0x8000 ? FALLBACK_RAM : FALLBACK_ROM;
    if (c->poll_prev) {
        n->steps_interrupt++;
    } else {
        n->steps_interp++;
        /* without recompiled code this traces every instruction (nesrecomp --misses) */
        if (n->misses && !c->jammed) n->misses[nes_miss_slot(n, c->pc)]++;
    }
    cpu_step(c);
    n->cycles_fallback[kind] += c->cycles - before;
}

/* ---- save states ----
 * The whole machine is one struct, the same in interpreter and recompiled
 * builds (recompiled code keeps no state of its own), so a state from one
 * loads in the other. File: header, the Nes struct, then CHR RAM if any.
 * shortcut: the struct is stored raw, so a state only loads in builds with
 * the same layout (checked by size and version); write fields one by one
 * when states must survive program updates. */
enum { STATE_VERSION = 1 };

typedef struct {
    char magic[4];  /* "NESS" */
    uint32_t version, size, prg_hash;
} StateHeader;

int nes_state_save(Nes *n, const char *path) {
    StateHeader h = {{'N', 'E', 'S', 'S'}, STATE_VERSION, (uint32_t)sizeof *n,
                     recomp_prg_hash(n->cart.prg, n->cart.prg_size)};
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(n, sizeof *n, 1, f) == 1 &&
             (!n->cart.chr_ram || fwrite(n->cart.chr, n->cart.chr_size, 1, f) == 1);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int nes_state_load(Nes *n, const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot open"); return -1; }
    StateHeader h;
    Nes *s = malloc(sizeof *s);
    uint8_t *chr = malloc(n->cart.chr_size);
    int ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "NESS", 4);
    if (!ok) snprintf(err, errlen, "not a save state");
    else if (h.version != STATE_VERSION || h.size != sizeof *n)
        ok = 0, snprintf(err, errlen, "saved by a different version of the program");
    else if (h.prg_hash != recomp_prg_hash(n->cart.prg, n->cart.prg_size))
        ok = 0, snprintf(err, errlen, "saved with a different game");
    else if (fread(s, sizeof *s, 1, f) != 1 || s->cart.chr_size != n->cart.chr_size ||
             (n->cart.chr_ram && fread(chr, n->cart.chr_size, 1, f) != 1))
        ok = 0, snprintf(err, errlen, "file is truncated");
    fclose(f);
    if (ok) {
        /* the state, except what belongs to this process: memory, linked
         * code, statistics */
        s->cart.prg = n->cart.prg;
        s->cart.chr = n->cart.chr;
        s->recomp = n->recomp;
        s->misses = n->misses;
        s->steps_native = n->steps_native, s->steps_interp = n->steps_interp;
        s->steps_interrupt = n->steps_interrupt, s->cycles_native = n->cycles_native;
        memcpy(s->cycles_fallback, n->cycles_fallback, sizeof s->cycles_fallback);
        *n = *s;
        if (n->cart.chr_ram) memcpy(n->cart.chr, chr, n->cart.chr_size);
        n->ppu.cart = n->apu.cart = &n->cart;
        n->cpu.bus = n;
        map_memory(n);
    }
    free(s);
    free(chr);
    return ok ? 0 : -1;
}

void nes_run_frame(Nes *n) {
    n->ppu.frame_done = n->cpu.yield = 0;
    while (!n->ppu.frame_done && !n->cpu.jammed) nes_step(n);
}

uint32_t nes_prg_bank(Nes *n, uint16_t a) { return n->cart.prg_off[(a >> 13) & 3] >> 13; }

size_t nes_miss_slots(Nes *n) { return 0x8000u + (size_t)n->cart.prg_size * 4; }

void nes_misses_write(Nes *n, FILE *f) {
    for (uint32_t a = 0; a < 0x8000; a++)
        if (n->misses[a]) fprintf(f, "MISS $%04X %u (RAM)\n", a, n->misses[a]);
    for (size_t slot = 0x8000; slot < nes_miss_slots(n); slot++) {
        if (!n->misses[slot]) continue;
        size_t key = (slot - 0x8000) >> 13;  /* bank * 4 + window */
        unsigned addr = 0x8000u + (unsigned)(key & 3) * 0x2000u + (unsigned)((slot - 0x8000) & 0x1FFF);
        fprintf(f, "MISS $%04X %u bank %u\n", addr, n->misses[slot], (unsigned)(key >> 2));
    }
}

long nes_misses_merge(Nes *n, const char *path) {
    size_t slots = nes_miss_slots(n);
    uint8_t *listed = calloc(slots, 1);
    FILE *f = fopen(path, "r");
    char line[96];
    while (f && fgets(line, sizeof line, f)) {
        unsigned a, count, bank;
        size_t slot = SIZE_MAX;
        if (sscanf(line, "MISS $%4x %u bank %u", &a, &count, &bank) == 3 && a >= 0x8000)
            slot = 0x8000u + (((size_t)bank * 4 + ((a >> 13) & 3)) << 13 | (a & 0x1FFF));
        else if (sscanf(line, "MISS $%4x %u (RAM", &a, &count) == 2 && a < 0x8000)
            slot = a;
        if (slot >= slots) continue;  /* another dump, or a damaged line */
        n->misses[slot] += count;
        listed[slot] = 1;
    }
    long fresh = 0;
    for (size_t slot = 0x8000; slot < slots; slot++) fresh += n->misses[slot] && !listed[slot];
    free(listed);
    if (!f) return -1;
    fclose(f);
    return fresh;
}

size_t nes_miss_slot(Nes *n, uint16_t a) {
    if (a < 0x8000) return a;
    return 0x8000u + (((size_t)nes_prg_bank(n, a) * 4 + ((a >> 13) & 3)) << 13 | (a & 0x1FFF));
}

uint8_t nes_peek(Nes *n, uint16_t a) {
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a >= 0x6000) return cart_cpu_read(&n->cart, a, 0);
    return 0;
}
