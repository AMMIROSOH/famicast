/* Deliberately wrong compiled instruction to test lockstep diagnostics. */
#include "recomp.h"

static void wrong_instruction(Cpu *cpu) {
    cpu_step(cpu);
    cpu->a ^= 1;
}

static A26RecompFn lookup(unsigned bank, unsigned offset) {
    return bank == 0 && offset == 0 ? wrong_instruction : NULL;
}

/* FNV-1a of the fixed test cartridge produced by make_fixtures.c. */
static const A26RecompProgram program = {0x245772D8, NULL, 4096, "a26_fixed.a26", 2, 0, lookup};
const A26RecompProgram *const a26_recomp_program = &program;
