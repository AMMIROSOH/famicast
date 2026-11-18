/* gbrecomp: the Game Boy recompiler (build-time tool). So far:
 *   gbrecomp --cpu-switch OUT.inc   the interpreter's opcode switch (gb/runtime/cpu.c)
 *   gbrecomp --ops                  every opcode: "CB? XX size name" (tests) */
#include <stdio.h>
#include <string.h>

#include "sm83.h"

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--cpu-switch")) {
        Buf o = {0};
        sm83_cpu_switch(&o);
        buf_write_file(argv[2], &o);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--ops")) {
        Lines l;
        char name[32];
        for (int cb = 0; cb < 2; cb++)
            for (int i = 0; i < 256; i++) {
                sm83_body(i, cb, &l, name, sizeof name);
                printf("%s%02X %d %s\n", cb ? "CB" : "", i, cb ? 2 : sm83_size(i), name);
            }
        return 0;
    }
    fprintf(stderr, "usage: gbrecomp --cpu-switch OUT.inc | --ops\n");
    return 2;
}
