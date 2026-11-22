/* sm83_gen OUT.inc: the interpreter's opcode switch (sm83_cpu.c), from the
 * instruction templates (sm83.c). Build-time tool. */
#include <stdio.h>

#include "sm83.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: sm83_gen OUT.inc\n");
        return 2;
    }
    Buf o = {0};
    sm83_cpu_switch(&o);
    buf_write_file(argv[1], &o);
    return 0;
}
