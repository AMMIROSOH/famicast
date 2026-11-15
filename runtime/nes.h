/* The NES system: CPU + PPU + cartridge + RAM + controllers, and the CPU bus. */
#ifndef NES_NES_H
#define NES_NES_H

#include <stdio.h>

#include "apu.h"
#include "cart.h"
#include "cpu.h"
#include "ppu.h"
#include "recomp_rt.h"

enum {  /* controller bits, in the order the shift register reports them */
    PAD_A = 0x01, PAD_B = 0x02, PAD_SELECT = 0x04, PAD_START = 0x08,
    PAD_UP = 0x10, PAD_DOWN = 0x20, PAD_LEFT = 0x40, PAD_RIGHT = 0x80
};

typedef struct Nes {
    Cpu cpu;
    Ppu ppu;
    Cart cart;
    Apu apu;
    uint8_t ram[0x800];
    uint8_t pad[2];       /* buttons held, set by the frontend */
    uint8_t pad_shift[2], strobe;
    uint8_t nmi_prev;
    uint8_t oam_dma_pending, oam_dma_page;  /* $4014 written: DMA at the next read */
    /* recompiled code (NULL: interpret everything) and fallback statistics */
    RecompFn (*recomp)(uint16_t pc, uint32_t bank);
    uint32_t *misses;     /* optional [nes_miss_slots()]: interpreted instructions
                             per (PRG bank, address); see nes_miss_slot() */
    uint64_t steps_native, steps_interp, steps_interrupt;
    /* coverage: CPU cycles in recompiled code, and in the interpreter by kind */
    uint64_t cycles_native, cycles_fallback[3];
} Nes;

enum { FALLBACK_ROM, FALLBACK_RAM, FALLBACK_INTERRUPT };  /* cycles_fallback[] */

/* Load an iNES image and power on. Returns 0, or -1 with a message in err.
 * Recompiled code linked into the program is used if its PRG hash matches. */
int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen);
void nes_reset(Nes *n);                /* reset button */
void nes_step(Nes *n);                 /* one block, or one interpreted instruction */
void nes_run_frame(Nes *n);            /* run until the next vblank starts */
uint8_t nes_peek(Nes *n, uint16_t a);  /* read without side effects (debug, tests) */
uint32_t nes_prg_bank(Nes *n, uint16_t a);  /* 8 KB PRG bank mapped at a ($8000+) */
/* Fallback statistics: slot = RAM address (< $8000), or for ROM
 * $8000 + ((bank * 4 + window) << 13 | (a & $1FFF)); see nes_miss_slot(). */
size_t nes_miss_slots(Nes *n);
size_t nes_miss_slot(Nes *n, uint16_t a);
/* Misses as text, the input of nesrecomp --misses: "MISS $ADDR COUNT bank B"
 * (ROM, 8 KB bank) or "MISS $ADDR COUNT (RAM)". */
void nes_misses_write(Nes *n, FILE *f);
/* Adds the counts of such a file to n->misses. Returns how many ROM
 * addresses of n->misses (count > 0) the file did not list, or -1 if it
 * cannot be read. */
long nes_misses_merge(Nes *n, const char *path);
void nes_sync(Nes *n);                 /* run the PPU up to the CPU (before inspecting it) */
/* Save states (between frames or steps). Interpreter and recompiled builds
 * of the same version share the format. Return 0, or -1 (load: message in err;
 * the machine is left unchanged). */
int nes_state_save(Nes *n, const char *path);
int nes_state_load(Nes *n, const char *path, char *err, size_t errlen);

#endif
