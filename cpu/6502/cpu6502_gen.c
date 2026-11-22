/* cpu6502_gen OUT.inc: the interpreter's opcode switch (cpu6502.c), from the
 * opcode table and templates (ops.c, c6502.c). Build-time tool. */
#include <stdio.h>

#include "c6502.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: cpu6502_gen OUT.inc\n");
        return 2;
    }
    ops_init();
    Buf o = {0};
    emit_cpu_switch(&o);
    buf_write_file(argv[1], &o);
    return 0;
}
