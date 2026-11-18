/* CPU bus. Memory map: nesdev wiki "CPU memory map". Every CPU cycle makes
 * exactly one bus access (runtime/cpu.c) and is worth 3 PPU dots: 2 before the
 * access and 1 after. The 2/1 split was measured: of 0-3 dots before, only 2
 * passes blargg ppu_vbl_nmi 05-nmi_timing (docs/06).
 *
 * The clock counts master clocks ("Cycle reference chart"): a CPU cycle is
 * 12 (NTSC), 16 (PAL) or 15 (Dendy), a PPU dot 4, 5 or 5, so PAL runs 3.2
 * dots a cycle; the fraction of a dot not yet run is kept in clk_rem. The
 * access stays 2 dots into the cycle on every region (shortcut: the PAL
 * CPU/PPU phase is not measured; tests on a PAL console would settle it).
 *
 * The PPU is caught up lazily (M5b): clocks are only counted (cpu.bus_pending) until
 * the CPU touches the PPU ($2000-$3FFF, OAM DMA) or the next point where the
 * PPU acts on the CPU by itself is reached (vblank start: NMI, frame end).
 * Between those points nothing can observe the PPU, so running its dots later
 * in one batch gives the same result; checked by the test ROMs, frame hashes
 * and --diff (docs/06). The APU (M6) is caught up the same way; its events
 * are the frame IRQ and DMC sample fetches. The MMC3 IRQ (M7) asserts on a
 * PPU A12 rise: while it is armed, the PPU is caught up every cycle around
 * the sprite fetches (dots 255-330 of rendering lines). The VRC4, Namco
 * 163, SS88006, FME-7 and Bandai FCG IRQs count CPU cycles: like an APU event, the next
 * one ends the batch (cart_run). The MMC5 counts scanlines: while its IRQ is on, every line
 * start ends a batch, and a $5204 read catches the PPU up first. */
#include "nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LINE = 341 };

/* How many clocks may pile up before the bus must catch up: until vblank
 * start (vbl_line, 1) has been processed (assuming the odd-frame skipped dot,
 * so at most one dot early), or until the APU's next event, whichever is
 * first. Dots from the PPU's position become clocks as d * clk_dot - clk_rem. */
static void plan_next_event(Nes *n) {
    const int pre = n->ppu.pre_line, lines = pre + 1, pre_clear = pre * LINE + 2;  /* just past (pre, 1) */
    const int dot = n->clk_dot, rem = n->clk_rem;
    int pos = n->ppu.scanline * LINE + n->ppu.dot;
    int64_t d = n->ppu.vbl_line * LINE + 1 - pos + 1;
    if (d <= 0) d += lines * LINE - 1;
    /* NMI line high: also stop once the pre-render line has cleared it at
     * (pre, 1), or a batch through to the next vblank would see high -> high
     * and miss the edge (a game waiting in a RAM loop after enabling NMI
     * inside vblank) */
    if (n->ppu.nmi_out && pos < pre_clear && pre_clear - pos < d) d = pre_clear - pos;
    d = d * dot - rem;
    int64_t apu = n->cpu.bus_pending + (int64_t)n->clk_cpu * apu_cycles_to_event(&n->apu);
    if (apu < d) d = apu;
    int64_t vrc = cart_cycles_to_irq(&n->cart, n->cpu.cycles);
    if (vrc >= 0 && n->cpu.bus_pending + n->clk_cpu * vrc < d) d = n->cpu.bus_pending + n->clk_cpu * vrc;
    if (n->cart.mapper == 5 && n->cart.mmc5_irq_enable && (n->ppu.mask & 0x18)) {
        int64_t m = (LINE - n->ppu.dot + 1) * dot - rem;  /* through dot 0 of the next line (cart_mmc5_scanline) */
        if (m < d) d = m;
    }
    /* MMC3: the counter is clocked on A12 rises during rendering (around dot
     * 260 or 324, or at dot 5 of the pre-render line, depending on the
     * pattern tables). While the IRQ is enabled, check the counter at dots 0
     * and 255 of every rendering line; once the next clock can fire, catch
     * up every cycle until the line ends, so a batch never runs past it. */
    if (cart_is_mmc3(&n->cart) && n->cart.irq_enabled && (n->ppu.mask & 0x18)) {
        int line = n->ppu.scanline;
        int rendering_line = line < 240 || line == pre;
        int64_t m;
        if (rendering_line && cart_irq_armed(&n->cart)) m = 3;
        else if (rendering_line && n->ppu.dot < 255) m = 255 - n->ppu.dot;
        else {  /* dot 0 of the next rendering line */
            int next = line < 239 ? line + 1 : (line == pre ? 0 : pre);
            m = ((next - line + lines) % lines) * LINE - n->ppu.dot;
        }
        m = m * dot + n->cpu.bus_pending;
        if (m < d) d = m;
    }
    if (n->oam_dma_pending) d = n->cpu.bus_pending;  /* next access: slow path, which runs the DMA */
    n->cpu.bus_budget = (int32_t)d;
}

/* The cartridge's IRQ line, its cycle counter (VRC4) run to now. */
static int cart_irq(Nes *n) {
    cart_run(&n->cart, n->cpu.cycles);
    return n->cart.irq_line;
}

/* APU order within CPU cycle N: the bus access sees the APU as of N-1, then
 * the APU clocks cycle N, then the CPU samples IRQ. So catch-ups run the APU
 * to N-1, and every slow-path access finishes with sync_apu() (through N). */
static void sync_apu(Nes *n) {
    apu_run(&n->apu, n->cpu.cycles - 1);
    n->cpu.irq = apu_irq(&n->apu) || cart_irq(n);  /* APU IRQ lags one cycle, like $4015 */
    apu_run(&n->apu, n->cpu.cycles);
}

/* The NMI line only rises at vblank start (or on a $2000/$2002 access, which
 * catches up right away), and a batch never runs past vblank start by more
 * than the current cycle, nor past vblank end while the line is high, so one
 * edge check per batch sees every rise. */
static void catch_up(Nes *n) {
    int32_t clocks = n->cpu.bus_pending + n->clk_rem;
    ppu_run(&n->ppu, clocks / n->clk_dot);
    n->clk_rem = clocks % n->clk_dot;
    n->cpu.yield |= n->ppu.frame_done;  /* the frame loop needs control back */
    if (n->ppu.nmi_out && !n->nmi_prev) n->cpu.nmi = 1;  /* NMI is edge-triggered */
    n->nmi_prev = n->ppu.nmi_out;
    n->cpu.bus_pending = 0;
    apu_run(&n->apu, n->cpu.cycles - 1);
    n->cpu.irq = apu_irq(&n->apu) || cart_irq(n);
    plan_next_event(n);
}

static inline void add_clocks(Nes *n, int k) {
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
    if (n->cart.mapper == 5 && (a == 0x5010 || a == 0x5015)) {  /* MMC5 sound: in the APU */
        apu_run(&n->apu, n->cpu.cycles - 1);
        return apu_mmc5_read(&n->apu, a, n->cpu.open_bus);
    }
    if (n->cart.mapper == 20 && a >= 0x4040 && a < 0x4100 && (n->cart.fds_io & 2)) {  /* FDS sound: in the APU */
        apu_run(&n->apu, n->cpu.cycles - 1);
        return apu_fds_read(&n->apu, a, n->cpu.open_bus);
    }
    if (a < 0x6000) cart_run(&n->cart, n->cpu.cycles);  /* Namco 163 counter reads */
    if (a >= 0x4800 && a < 0x5000 && n->cart.n163_sound) apu_run(&n->apu, n->cpu.cycles - 1);  /* N163 sound updates its RAM */
    return cart_cpu_read(&n->cart, a, n->cpu.open_bus);
}

static int is_ppu(uint16_t a) { return (a & 0xE000) == 0x2000; }
static void map_memory(Nes *n);

/* One bus read in the current cycle (n->cpu.cycles already counts it), with
 * every side effect a CPU read has. The CPU and the DMA unit both use it. */
static uint8_t read_cycle(Nes *n, uint16_t a) {
    if (!is_ppu(a) && !cart_read_syncs(&n->cart, a)) {
        add_clocks(n, n->clk_cpu);
        n->cpu.open_bus = read_no_tick(n, a);
        sync_apu(n);
        plan_next_event(n);
        return n->cpu.open_bus;
    }
    n->cpu.bus_pending += 2 * n->clk_dot;  /* 2 dots before the access */
    catch_up(n);
    n->cpu.open_bus = read_no_tick(n, a);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += n->clk_cpu - 2 * n->clk_dot;
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
            n->cpu.irq = apu_irq(&n->apu) || cart_irq(n);
            plan_next_event(n);
            dmc = 0;
        } else if (get && oam) {
            value = read_cycle(n, (uint16_t)(n->oam_dma_page << 8 | count / 2));
            count++;
        } else if (!get && oam && (count & 1)) {
            n->cpu.bus_pending += n->clk_cpu;
            catch_up(n);  /* OAM is read by sprite evaluation: write at the exact dot */
            ppu_oam_dma_write(&n->ppu, value);
            sync_apu(n);
            plan_next_event(n);
            if (++count == 512) oam = 0;
        } else if (quiet) {  /* alignment / dummy cycle, controller not clocked again */
            add_clocks(n, n->clk_cpu);
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
    int mapper_reg = cart_is_reg(&n->cart, a);
    if (!is_ppu(a) && a != 0x4014 && !mapper_reg) {
        add_clocks(n, n->clk_cpu);
        if (a < 0x2000) n->ram[a & 0x7FF] = v;
        else if (a == 0x4016) {
            n->strobe = v & 1;
            if (n->strobe) { n->pad_shift[0] = n->pad[0]; n->pad_shift[1] = n->pad[1]; }
        }
        else if (n->cart.mapper == 20 && a >= 0x4040 && a < 0x4100) {  /* FDS sound: in the APU */
            if (n->cart.fds_io & 2) {
                sync_apu(n);
                apu_fds_write(&n->apu, a, v);
            }
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
    n->cpu.bus_pending += 2 * n->clk_dot;  /* 2 dots before the access */
    catch_up(n);
    if (a == 0x4014) n->oam_dma_page = v, n->oam_dma_pending = 1;  /* runs at the next read (run_dma) */
    else if (n->cart.mapper == 5 && a >= 0x5000 && a <= 0x5015) {  /* MMC5 sound: in the APU */
        sync_apu(n);
        apu_mmc5_write(&n->apu, a, v);
    }
    else if (n->cart.mapper == 69 && a >= 0xC000) {  /* Sunsoft 5B sound: in the APU */
        sync_apu(n);
        apu_5b_write(&n->apu, a, v);
    }
    else if (n->cart.mapper == 85 && (a & 0xF000) == 0x9000 && cart_vrc7_line(&n->cart, a)) {  /* VRC7 sound */
        sync_apu(n);
        apu_vrc7_write(&n->apu, (a >> 5) & 1, v);
    }
    else if (n->cart.mapper == 85 && (a & 0xF000) == 0xE000 && !cart_vrc7_line(&n->cart, a)) {
        sync_apu(n);
        apu_vrc7_silence(&n->apu, (v >> 6) & 1);  /* and the mirroring, below */
        cart_cpu_write(&n->cart, a, v, n->cpu.cycles);
    }
    else if ((n->cart.mapper == 24 || n->cart.mapper == 26) && cart_vrc6_reg(&n->cart, a) >= 0x9000 &&
             cart_vrc6_reg(&n->cart, a) < 0xB003) {  /* VRC6 sound: in the APU */
        sync_apu(n);
        apu_vrc6_write(&n->apu, cart_vrc6_reg(&n->cart, a), v);
    }
    else if (mapper_reg) {  /* bank switches can be raster effects: PPU caught up first */
        if (cart_cpu_write(&n->cart, a, v, n->cpu.cycles)) map_memory(n);
    }
    else ppu_reg_write(&n->ppu, a, v);
    after_ppu_access(n);
    /* the rest of this cycle runs now: a $2000 write or $2002 read can change
     * the NMI line, and the edge must be seen on the following dot */
    n->cpu.bus_pending += n->clk_cpu - 2 * n->clk_dot;
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
    const uint8_t *m6 = n->cart.rom_at_6000 ? n->cart.prg : n->cart.prg_ram;  /* FME-7: ROM there */
    for (int i = 12; i < 16; i++) c->read_map[i] = c->write_map[i] = (uint8_t *)m6 + n->cart.ram_off + (i - 12) * 0x800;
    if (n->cart.rom_at_6000)  /* writes ignored (slow path) */
        for (int i = 12; i < 16; i++) c->write_map[i] = NULL;
    if (cart_6000_is_io(&n->cart))  /* registers, EEPROM lines */
        for (int i = 12; i < 16; i++) c->read_map[i] = c->write_map[i] = NULL;
    for (int i = 16; i < 32; i++) {  /* 8 KB windows from the mapper, 4 pages each */
        int w = (i - 16) >> 2;
        const uint8_t *mem = (n->cart.prg_ram_window >> w & 1) ? n->cart.prg_ram : n->cart.prg;
        c->read_map[i] = (uint8_t *)mem + n->cart.prg_off[w] + ((i & 3) * 0x800);
    }
    if (n->cart.mapper == 20)  /* FDS: RAM up to $DFFF */
        for (int i = 16; i < 28; i++) c->write_map[i] = (uint8_t *)c->read_map[i];
    c->bus_step = n->clk_cpu;
}

static void power_on(Nes *n) {
    ppu_reset(&n->ppu, &n->cart);
    n->clk_cpu = n->cart.region == REGION_PAL ? 16 : n->cart.region == REGION_DENDY ? 15 : 12;
    n->clk_dot = n->cart.region == REGION_NTSC ? 4 : 5;
    n->cpu.bus = n;
    apu_reset(&n->apu, &n->cart, 1);
    cpu_reset(&n->cpu);
    plan_next_event(n);
    map_memory(n);
}

int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom, size, err, errlen) != 0) return -1;
    power_on(n);
    if (recomp_program && recomp_program->prg_hash == recomp_prg_hash(n->cart.prg, n->cart.prg_size))
        n->recomp = recomp_program->lookup;
    return 0;
}

/* Power on again as another console: the cartridge as loaded (only its
 * reset vector has been read), everything else from zero. */
void nes_set_region(Nes *n, int region) {
    static Cart cart;  /* 64 KB of PRG RAM: not on the stack */
    cart = n->cart;
    cart.region = region;
    RecompFn (*recomp)(uint16_t, uint32_t) = n->recomp;
    uint32_t *misses = n->misses;
    uint64_t *ram_code = n->ram_code;
    memset(n, 0, sizeof *n);
    n->cart = cart;
    n->recomp = recomp;
    n->misses = misses;
    n->ram_code = ram_code;
    power_on(n);
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
/* FDS: the BIOS's disk check ($E445) compares the disk's header with the
 * 10-byte ID at ($00); automatic swaps put the matching side in first. */
static void fds_disk_check(Nes *n) {
    uint8_t id[10];
    uint16_t p = (uint16_t)(n->ram[0] | n->ram[1] << 8);
    for (int i = 0; i < 10; i++) {
        uint16_t a = (uint16_t)(p + i);
        const uint8_t *page = n->cpu.read_map[a >> 11];
        id[i] = page ? page[a & 0x7FF] : 0;
    }
    cart_fds_check(&n->cart, id);
}

/* FDS disk files' code: the block made from what RAM holds at pc. A block
 * checks RAM itself on entry (and returns without running if it differs),
 * so only where several files have code at pc is RAM compared here. */
static RecompFn ram_lookup(const Cpu *c) {
    const RecompProgram *p = recomp_program;
    /* first[a]: the first entry at address a or later (ram_entries is sorted) */
    static uint32_t first[0x10001];
    static int built;
    if (!built) {
        size_t e = 0;
        for (uint32_t a = 0; a <= 0x10000; a++) {
            while (e < p->ram_entries_n && p->ram_entries[e].pc < a) e++;
            first[a] = (uint32_t)e;
        }
        built = 1;
    }
    size_t lo = first[c->pc];
    if (lo < p->ram_entries_n && p->ram_entries[lo].pc == c->pc &&
        (lo + 1 == p->ram_entries_n || p->ram_entries[lo + 1].pc != c->pc))
        return p->ram_blocks[p->ram_entries[lo].block].fn;  /* the only one */
    for (; lo < p->ram_entries_n && p->ram_entries[lo].pc == c->pc; lo++) {
        const RecompRamBlock *b = &p->ram_blocks[p->ram_entries[lo].block];
        if (recomp_ram_is(c, b->start, b->bytes, b->len)) return b->fn;
    }
    return NULL;
}

/* ram_code: an open-addressing set of keys; the last slot counts them */
enum { RAM_CODE_SLOTS = 1 << 19 };

static int note_ram_key(Nes *n, uint64_t key) {  /* 1 if new */
    if (!n->ram_code && !(n->ram_code = calloc(RAM_CODE_SLOTS + 1, sizeof *n->ram_code))) return 0;
    uint64_t *count = &n->ram_code[RAM_CODE_SLOTS];
    for (uint32_t i = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 45);; i = (i + 1) & (RAM_CODE_SLOTS - 1)) {
        if (n->ram_code[i] == key) return 0;
        if (n->ram_code[i]) continue;
        if (*count >= RAM_CODE_SLOTS / 4 * 3) return 0;  /* full: the rest is not recorded */
        n->ram_code[i] = key, ++*count;
        return 1;
    }
}

static uint64_t ram_key(uint16_t pc, uint32_t bytes) { return 1ull << 40 | (uint64_t)pc << 24 | bytes; }

void nes_step(Nes *n) {
    Cpu *c = &n->cpu;
    uint64_t before = c->cycles;
    if (n->cart.mapper == 20 && c->pc == 0xE445 && !c->poll_prev) fds_disk_check(n);
    if (n->recomp && !c->poll_prev && !c->jammed) {
        RecompFn f = n->recomp(c->pc, nes_prg_bank(n, c->pc));
        if (!f && recomp_program->ram_entries_n) f = ram_lookup(c);
        if (f) {
            f(c);
            if (c->cycles != before) {  /* else a disk file's block found other code in RAM */
                n->steps_native++;
                n->cycles_native += c->cycles - before;
                return;
            }
        }
    }
    int kind = c->poll_prev ? FALLBACK_INTERRUPT : nes_miss_slot(n, c->pc) < 0x8000 ? FALLBACK_RAM : FALLBACK_ROM;
    if (c->poll_prev) {
        n->steps_interrupt++;
    } else {
        n->steps_interp++;
        /* without recompiled code this traces every instruction (nesrecomp --misses) */
        if (n->misses && !c->jammed) n->misses[nes_miss_slot(n, c->pc)]++;
        if (n->misses && !c->jammed && n->cart.mapper == 20 && c->pc < 0xE000)
            note_ram_key(n, ram_key(c->pc, (uint32_t)nes_peek(n, c->pc) << 16 |
                                               (uint32_t)nes_peek(n, (uint16_t)(c->pc + 1)) << 8 |
                                               nes_peek(n, (uint16_t)(c->pc + 2))));
    }
    cpu_step(c);
    n->cycles_fallback[kind] += c->cycles - before;
}

/* ---- save states ----
 * The whole machine is one struct, the same in interpreter and recompiled
 * builds (recompiled code keeps no state of its own), so a state from one
 * loads in the other. File: header, the Nes struct, then CHR RAM if any, then
 * the FDS disk sides (what the game wrote to them).
 * shortcut: the struct is stored raw, so a state only loads in builds with
 * the same layout (checked by size and version); write fields one by one
 * when states must survive program updates. */
enum { STATE_VERSION = 16 };  /* 2: submapper, VRC2/VRC4 fields; 3: MMC5, Namco 163, 64 KB PRG RAM;
                                4: MMC2/MMC4, SS88006, FME-7; 5: Bandai FCG EEPROMs; 6: TxSROM;
                                7: emphasis in the frame buffer; 8: TC0690 IRQ delay;
                                9: Namco 163 sound; 10: MMC5 sound; 11: Sunsoft 5B;
                                12: VRC6 sound; 13: VRC7;
                                14: regions (master clocks); 15: FDS; 16: more boards (cart.c) */

typedef struct {
    char magic[4];  /* "NESS" */
    uint32_t version, size, prg_hash;
} StateHeader;

/* the game a state belongs to: PRG ROM, or for FDS the disk image */
static uint32_t game_hash(const Nes *n) {
    return n->cart.disk ? n->cart.disk_hash : recomp_prg_hash(n->cart.prg, n->cart.prg_size);
}

int nes_state_save(Nes *n, const char *path) {
    StateHeader h = {{'N', 'E', 'S', 'S'}, STATE_VERSION, (uint32_t)sizeof *n, game_hash(n)};
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(n, sizeof *n, 1, f) == 1 &&
             (!n->cart.chr_ram || fwrite(n->cart.chr, n->cart.chr_size, 1, f) == 1) &&
             (!n->cart.disk || fwrite(n->cart.disk, FDS_RAW_SIDE, n->cart.disk_sides, f) == n->cart.disk_sides);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int nes_state_load(Nes *n, const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot open"); return -1; }
    StateHeader h;
    Nes *s = malloc(sizeof *s);
    uint8_t *chr = malloc(n->cart.chr_size);
    size_t disk_size = n->cart.disk ? (size_t)FDS_RAW_SIDE * n->cart.disk_sides : 0;
    uint8_t *disk = malloc(disk_size + 1);
    int ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "NESS", 4);
    if (!ok) snprintf(err, errlen, "not a save state");
    else if (h.version != STATE_VERSION || h.size != sizeof *n)
        ok = 0, snprintf(err, errlen, "saved by a different version of the program");
    else if (h.prg_hash != game_hash(n))
        ok = 0, snprintf(err, errlen, "saved with a different game");
    else if (fread(s, sizeof *s, 1, f) != 1 || s->cart.chr_size != n->cart.chr_size ||
             (n->cart.chr_ram && fread(chr, n->cart.chr_size, 1, f) != 1) ||
             (disk_size && fread(disk, disk_size, 1, f) != 1))
        ok = 0, snprintf(err, errlen, "file is truncated");
    fclose(f);
    if (ok) {
        /* the state, except what belongs to this process: memory, linked
         * code, statistics */
        s->cart.prg = n->cart.prg;
        s->cart.chr = n->cart.chr;
        s->cart.disk = n->cart.disk;
        s->recomp = n->recomp;
        s->misses = n->misses;
        s->ram_code = n->ram_code;
        s->steps_native = n->steps_native, s->steps_interp = n->steps_interp;
        s->steps_interrupt = n->steps_interrupt, s->cycles_native = n->cycles_native;
        memcpy(s->cycles_fallback, n->cycles_fallback, sizeof s->cycles_fallback);
        *n = *s;
        if (n->cart.chr_ram) memcpy(n->cart.chr, chr, n->cart.chr_size);
        if (disk_size) memcpy(n->cart.disk, disk, disk_size);
        n->ppu.cart = n->apu.cart = &n->cart;
        n->cpu.bus = n;
        map_memory(n);
    }
    free(s);
    free(chr);
    free(disk);
    return ok ? 0 : -1;
}

double nes_frame_rate(const Nes *n) { return n->cart.region == REGION_NTSC ? 60.0988 : 50.0070; }

void nes_run_frame(Nes *n) {
    n->ppu.frame_done = n->cpu.yield = 0;
    while (!n->ppu.frame_done && !n->cpu.jammed) nes_step(n);
}

/* PRG RAM in a ROM window (MMC5) has no bank: UINT32_MAX, which no recompiled table has */
uint32_t nes_prg_bank(Nes *n, uint16_t a) {
    int w = (a >> 13) & 3;
    return (n->cart.prg_ram_window >> w & 1) ? UINT32_MAX : n->cart.prg_off[w] >> 13;
}

size_t nes_miss_slots(Nes *n) { return 0x8000u + (size_t)n->cart.prg_size * 4; }

void nes_misses_write(Nes *n, FILE *f) {
    for (uint32_t a = 0; a < 0x8000 && n->cart.mapper != 20; a++)  /* FDS: ram_code instead */
        if (n->misses[a]) fprintf(f, "MISS $%04X %u (RAM)\n", a, n->misses[a]);
    for (uint32_t i = 0; n->ram_code && i < RAM_CODE_SLOTS; i++)
        if (n->ram_code[i])
            fprintf(f, "MISS $%04X 1 (RAM %06X)\n", (unsigned)(n->ram_code[i] >> 24 & 0xFFFF),
                    (unsigned)(n->ram_code[i] & 0xFFFFFF));
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
    uint64_t listed_ram = 0;
    FILE *f = fopen(path, "r");
    char line[96];
    while (f && fgets(line, sizeof line, f)) {
        unsigned a, count, bank, bytes;
        size_t slot = SIZE_MAX;
        if (sscanf(line, "MISS $%4x %u (RAM %6x)", &a, &count, &bytes) == 3) {
            note_ram_key(n, ram_key((uint16_t)a, bytes));
            listed_ram++;
            continue;
        }
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
    if (n->ram_code) fresh += (long)(n->ram_code[RAM_CODE_SLOTS] - listed_ram);  /* the file's keys are distinct */
    free(listed);
    if (!f) return -1;
    fclose(f);
    return fresh;
}

size_t nes_miss_slot(Nes *n, uint16_t a) {
    if (a < 0x8000) return a;
    if (nes_prg_bank(n, a) == UINT32_MAX) return 0x6000u | (a & 0x1FFF);  /* PRG RAM: as RAM code */
    return 0x8000u + (((size_t)nes_prg_bank(n, a) * 4 + ((a >> 13) & 3)) << 13 | (a & 0x1FFF));
}

uint8_t nes_peek(Nes *n, uint16_t a) {
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a >= 0x6000) return cart_cpu_read(&n->cart, a, 0);
    return 0;
}
