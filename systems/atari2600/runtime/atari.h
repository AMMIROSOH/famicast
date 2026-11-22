#ifndef ATARI2600_H
#define ATARI2600_H

#include "bus.h"
#include "tia.h"
#include "input_log.h"
#include "recomp.h"

typedef struct Atari2600 {
    A26Bus bus;
    A26Tia tia;
    uint32_t rom_hash;
    uint32_t audio_phase[2];
    uint16_t audio_lfsr[2];
    unsigned sample_phase;
    int pal;
    const A26RecompProgram *program;
    struct Atari2600 *reference;
    uint8_t *trace;
    uint64_t compiled_cycles, total_cycles;
    int diff_failed;
} Atari2600;

int a26_init(Atari2600 *atari, const uint8_t *rom, size_t size, A26CartType type, int pal);
void a26_frame(Atari2600 *atari, const InputFrame *input);
int a26_save(Atari2600 *atari, const char *path);
int a26_load(Atari2600 *atari, const char *path);

#endif
