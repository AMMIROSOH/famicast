/* The Capcom Play System I (CPS-1): a 68000 at 10 MHz running the game, a
 * Z80 at 3.58 MHz for sound (YM2151, OKI MSM6295), and the CPS-A / CPS-B
 * graphics chips (three scroll layers, sprites, stars). A game comes as a
 * zip of chip dumps; games.c says which chip goes where, and which CPS-B
 * the board has (its ID, ports and layer bits differ by game).
 * Sources: docs/sources.md (CPS-1). */
#ifndef CPS1_H
#define CPS1_H

#include <stddef.h>
#include <stdint.h>

#include "m68k_cpu.h"
#include "zip.h"

/* ---- the game table (games.c) and loading (load.c) ---- */

enum { CPS1_R_MAIN, CPS1_R_GFX, CPS1_R_SOUND, CPS1_R_OKI, CPS1_REGIONS };
enum {
    CPS1_LOAD_PLAIN,    /* the bytes as they are */
    CPS1_LOAD_EVEN,     /* an 8-bit chip on the 68000's upper byte: every other byte from offset */
    CPS1_LOAD_ODD,      /* the same on the lower byte (offset is odd) */
    CPS1_LOAD_SWAP16,   /* a 16-bit chip, its words byte-swapped into big-endian order */
    CPS1_LOAD_GFX4      /* a 16-bit graphics chip: 2 bytes into every 8, at offset's slot (0, 2, 4, 6) */
};
enum { CPS1_PLAIN, CPS1_QSOUND, CPS1_ODD };  /* board groups (docs/roadmap.md) */

typedef struct {
    uint32_t crc, size;
    uint8_t region, load;
    uint32_t offset;
} Cps1File;

#define CPS1_NONE 0xFF  /* a CPS-B port the board does not have */
typedef struct {
    uint8_t id_reg;              /* the ID port (a byte offset into the CPS-B registers) */
    uint16_t id_val;
    uint8_t mult_a, mult_b, mult_lo, mult_hi;  /* the multiplier's ports */
    uint8_t layer_ctrl, prio[4], pal_ctrl;
    uint16_t layer_enable[5];    /* the layer-control bits enabling scroll1-3, stars 1-2 */
} Cps1B;

typedef struct {
    const char *id, *title;
    uint8_t group;
    uint32_t region_size[CPS1_REGIONS];
    Cps1B b;
    uint8_t nfiles;
    Cps1File files[32];
} Cps1Game;

extern const Cps1Game cps1_games[];
extern const int cps1_game_count;

typedef struct {
    const Cps1Game *game;
    uint8_t *region[CPS1_REGIONS];
    uint32_t size[CPS1_REGIONS];
} Cps1Rom;

/* The game whose files are all in the zip (by CRC and size), or NULL;
 * other entries (PAL dumps, clones of a merged set) do not matter. */
const Cps1Game *cps1_match(const Zip *z);
const Cps1Game *cps1_match_in(const Zip *z, const Cps1Game *games, int n);
/* 0, or -1 with the first missing or damaged file (its CRC) in err. */
int cps1_load(const Cps1Game *g, const Zip *z, Cps1Rom *r, char *err, size_t errlen);
void cps1_unload(Cps1Rom *r);

/* ---- the machine (cps1.c) ---- */

#define CPS1_WIDTH 384
#define CPS1_HEIGHT 224
#define CPS1_LINES 262          /* 8 MHz pixel clock, 512 per line: 59.64 Hz */
#define CPS1_FIRST_LINE 16      /* the visible lines are 16-239 */
#define CPS1_VBLANK_LINE 240
#define CPS1_CLOCK 10000000     /* 68000 */
#define CPS1_LINE_CLOCKS 640    /* 10 MHz / (8 MHz / 512) */

typedef struct Cps1 {
    M68k cpu;
    const Cps1Rom *rom;
    uint8_t ram[0x10000];       /* $FF0000-$FFFFFF */
    uint8_t gfxram[0x30000];    /* $900000-$92FFFF */
    uint16_t cpsa[0x20], cpsb[0x20];  /* $800100 and $800140, by word */
    uint16_t mult_a, mult_b;
    uint8_t sound_cmd, sound_fade;
    uint16_t pad[2];            /* core/input_log.h PAD_* bits, players 1-2 */
    uint8_t dip[3];             /* DIP switches A-C, as read (active low) */
    int line;
    uint64_t line_end;          /* cpu.cycles when the current line ends */
    uint16_t frame[CPS1_WIDTH * CPS1_HEIGHT];  /* palette indices */
} Cps1;

void cps1_init(Cps1 *s, const Cps1Rom *r);
void cps1_frame(Cps1 *s);       /* 262 lines; the picture is left in s->frame */
void cps1_set_input(Cps1 *s, int player, uint16_t pad);  /* buttons 1-3 = B, A, C; Start, and Select = coin */

#endif
