/* runtime.h — shared state between the lifted x86 code and the C driver. */
#include <stdint.h>

#define RT_RAM_SIZE 0x10000

/* P bits (6502 status). The B bit (0x10) comes from the reset vector; we keep
 * it fixed. D (0x08) enables BCD — the codegen traps on BCD arithmetic. */
#define P_Z 0x01
#define P_C 0x02
#define P_I 0x04
#define P_D 0x08
#define P_B 0x10
#define P_V 0x40
#define P_N 0x80

typedef struct {
    unsigned char a;   /* A */
    unsigned char x;   /* X */
    unsigned char y;   /* Y */
    unsigned char s;   /* S (stack pointer) */
    unsigned char p;   /* status */
    unsigned char nmi; /* pending NMI, set by driver, consumed by rt_nmi_entry */
    /* padding */
    unsigned int pc;   /* program counter (16-bit value) */
} Cpu;

typedef struct {
    unsigned long long pos;  /* PPU position: frame-absolute PPU cycles */
    unsigned char puctl, pumask, oamaddr, ptr;
    unsigned char oam[256];  /* OAM buffer (Y, tile, attr, X) x 64 max */
    unsigned char sp_ovf;    /* sprite overflow latch */
    unsigned char s0_hit;    /* sprite-0 hit latch (stub: stays 0) */
} Ppu;

extern unsigned char rt_ram[RT_RAM_SIZE];
extern Cpu rt_cpu;
extern Ppu rt_ppu;
extern void *rt_jmp[0x10000];  /* dispatch table: pc -> block entry */
extern unsigned int rt_extra;  /* runtime cycle penalties (ZP-indexed RMW page
                                  cross), accumulated by lifted code, consumed
                                  by the driver per block */

void rt_halt(void);
void rt_bcd_trap(void);
void rt_jmp_invalid(void);
void rt_nmi_entry(void);

/* PPU access (called by the driver at the exact PPU position) */
unsigned char rt_ppu_read(unsigned short addr);
void rt_ppu_write(unsigned short addr, unsigned char v);

/* PPU timing constants (NTSC, see docs/04) */
#define PPU_LINE 341u            /* PPU cycles per line */
#define PPU_LINES 262u
#define PPU_FRAME (PPU_LINE * PPU_LINES)   /* 89342 */
#define NMI_POS (241u * PPU_LINE + 1u)     /* line 241, cycle 1 */
#define PPU_PER_CPU 3u
