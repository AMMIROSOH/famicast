/* m68k_gen OUT.inc: the interpreter's handlers and opcode table
 * (m68k_cpu.c), from the instruction templates (m68k.c). Build-time tool. */
#include <stdio.h>

#include "m68k.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: m68k_gen OUT.inc\n");
        return 2;
    }
    Buf o = {0};
    m68k_cpu_handlers(&o);
    buf_write_file(argv[1], &o);
    return 0;
}
