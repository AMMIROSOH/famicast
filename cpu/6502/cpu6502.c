/* Reference 2A03 interpreter: one bus access per cycle (systems/nes/docs/architecture.md). */
#include "cpu6502_ops.h"

/* NMI/IRQ entry. An NMI seen in the first four cycles takes over an IRQ's
 * vector; no polling at the end, so the handler's first instruction always
 * runs (nesdev "CPU interrupts", cpu_interrupts_v2 3-nmi_and_irq). */
static void enter_interrupt(Cpu *c, uint16_t vector) {
    rd(c, c->pc);
    rd(c, c->pc);
    push(c, c->pc >> 8);
    push(c, c->pc & 0xFF);
    if (c->nmi) {
        vector = 0xFFFA;
        c->nmi = 0;
    }
    push(c, (uint8_t)((c->p & ~FLAG_B) | FLAG_U));
    c->p |= FLAG_I;
    uint16_t lo = rd(c, vector);
    c->pc = lo | rd(c, vector + 1) << 8;
    c->poll_prev = 0;
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
        enter_interrupt(c, c->nmi ? 0xFFFA : 0xFFFE);  /* NMI cleared inside */
        return;
    }
    switch (rd(c, c->pc++)) {
#include "cpu6502_switch.inc"
    }
}
