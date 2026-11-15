/* Reference 2A03 interpreter: one bus access per cycle (docs/02). */
#include "cpu_ops.h"

static void enter_interrupt(Cpu *c, uint16_t vector) {
    rd(c, c->pc);
    rd(c, c->pc);
    push(c, c->pc >> 8);
    push(c, c->pc & 0xFF);
    push(c, (uint8_t)((c->p & ~FLAG_B) | FLAG_U));
    c->p |= FLAG_I;
    uint16_t lo = rd(c, vector);
    c->pc = lo | rd(c, vector + 1) << 8;
}

void cpu_reset(Cpu *c) {
    c->p |= FLAG_I | FLAG_U;
    c->jammed = 0;
    rd(c, c->pc);
    rd(c, c->pc);
    for (int i = 0; i < 3; i++) {  /* pushes become reads during reset */
        rd(c, 0x100 | c->s);
        c->s--;
    }
    uint16_t lo = rd(c, 0xFFFC);
    c->pc = lo | rd(c, 0xFFFD) << 8;
}

void cpu_step(Cpu *c) {
    if (c->jammed) {
        c->cycles++;
        return;
    }
    /* shortcut: interrupts are polled before each instruction, not on the
     * last cycle of the previous one; upgrade when cpu_interrupts_v2 needs it. */
    if (c->nmi) {
        c->nmi = 0;
        enter_interrupt(c, 0xFFFA);
        return;
    }
    if (c->irq && !(c->p & FLAG_I)) {
        enter_interrupt(c, 0xFFFE);
        return;
    }
    switch (rd(c, c->pc++)) {
#include "cpu_switch.inc"
    }
}
