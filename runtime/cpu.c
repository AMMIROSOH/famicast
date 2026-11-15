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
    if (c->poll_prev) {  /* sampled on the previous instruction's second-to-last cycle */
        int nmi = c->nmi;
        c->nmi = 0;
        c->poll = c->poll_prev = 0;
        enter_interrupt(c, nmi ? 0xFFFA : 0xFFFE);
        return;
    }
    switch (rd(c, c->pc++)) {
#include "cpu_switch.inc"
    }
}
