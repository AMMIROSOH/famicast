/* runtime.c — globals + stubs shared by the lifted code and the driver */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

unsigned char rt_ram[RT_RAM_SIZE];
Cpu rt_cpu;
Ppu rt_ppu;
void *rt_jmp[0x10000];
unsigned int rt_extra = 0;

void rt_halt(void) {
    fprintf(stderr,
        "HALT: no block at pc $%04X a=%02X x=%02X y=%02X s=%02X p=%02X",
        rt_cpu.pc, rt_cpu.a, rt_cpu.x, rt_cpu.y, rt_cpu.s, rt_cpu.p);
    fputc(10, stderr);
    unsigned char *sp = rt_ram + 0x100 + rt_cpu.s;
    fprintf(stderr, "  stack[S..S+7]: ");
    for (int i = 0; i < 8; i++) fprintf(stderr, "%02X ", sp[i]);
    fputc(10, stderr);
    exit(3);
}

void rt_bcd_trap(void) {
    fprintf(stderr, "TRAP: BCD-mode ADC/SBC at pc $%04X (stub, D=1 unsupported)\n", rt_cpu.pc);
    exit(4);
}

void rt_jmp_invalid(void) {
    fprintf(stderr, "TRAP: jump to unmapped/invalid pc $%04X\n", rt_cpu.pc);
    exit(5);
}
