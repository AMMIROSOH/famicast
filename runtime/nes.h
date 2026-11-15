/* The NES system: CPU + PPU + cartridge + RAM + controllers, and the CPU bus. */
#ifndef NES_NES_H
#define NES_NES_H

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
    uint8_t ram[0x800];
    uint8_t open_bus;     /* last value on the CPU data bus */
    uint8_t pad[2];       /* buttons held, set by the frontend */
    uint8_t pad_shift[2], strobe;
    uint8_t nmi_prev;
    /* recompiled code (NULL: interpret everything) and fallback statistics */
    RecompFn (*recomp)(uint16_t pc);
    uint32_t *misses;     /* optional [0x10000]: interpreted instructions per PC */
    uint64_t steps_native, steps_interp, steps_interrupt;
} Nes;

/* Load an iNES image and power on. Returns 0, or -1 with a message in err.
 * Recompiled code linked into the program is used if its PRG hash matches. */
int nes_load(Nes *n, const uint8_t *rom, size_t size, char *err, size_t errlen);
void nes_reset(Nes *n);                /* reset button */
void nes_step(Nes *n);                 /* one block, or one interpreted instruction */
void nes_run_frame(Nes *n);            /* run until the next vblank starts */
uint8_t nes_peek(Nes *n, uint16_t a);  /* read without side effects (debug, tests) */

#endif
