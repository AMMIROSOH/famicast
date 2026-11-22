/* z80_gen OUT.inc: the interpreter's opcode switch (z80_cpu.c), from the
 * instruction templates (z80.c). Build-time tool. */
#include <stdio.h>

#include "z80.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: z80_gen OUT.inc\n");
        return 2;
    }
    Buf o = {0};
    z80_cpu_switch(&o);
    buf_write_file(argv[1], &o);
    return 0;
}
