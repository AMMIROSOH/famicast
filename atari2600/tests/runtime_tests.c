#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atari.h"
#include "palette.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    A26Tia tia;
    a26_tia_init(&tia);
    a26_tia_write(&tia, 0x1B, 0x80);
    a26_tia_write(&tia, 0x1C, 0x80);
    a26_tia_write(&tia, 0x0D, 0x10);
    assert(tia.reg[0x1B] == 0 && tia.reg[0x0D] == 0);
    a26_tia_tick(&tia);
    assert(tia.reg[0x1B] == 0x80 && tia.reg[0x0D] == 0);
    a26_tia_tick(&tia);
    assert(tia.reg[0x0D] == 0x10);
    tia.position[0] = tia.position[1] = 0;
    tia.x = 68;
    a26_tia_tick(&tia);
    assert((a26_tia_read(&tia, 7, 0) & 0x80) != 0);
    assert((a26_tia_read(&tia, 2, 0) & 0x80) != 0);
    a26_tia_write(&tia, 0x2C, 0);
    assert(a26_tia_read(&tia, 7, 0) == 0);
    a26_tia_write(&tia, 0x02, 0);
    assert(!a26_tia_ready(&tia));
    while (tia.x) a26_tia_tick(&tia);
    assert(a26_tia_ready(&tia));
    a26_tia_write(&tia, 0, 2);
    a26_tia_write(&tia, 0, 0);
    assert(tia.frames == 1 && tia.y == 0);
    tia.x = 0;
    a26_tia_write(&tia, 2, 0);
    assert(a26_tia_ready(&tia));
    tia.x = 30;
    a26_tia_write(&tia, 0x10, 0);
    assert(tia.position[0] == 1);
    a26_tia_write(&tia, 0x2A, 0);
    assert(tia.hmove_blank);
    assert(a26_palette_color(0x20, 0) != a26_palette_color(0x20, 1));
    assert(a26_palette_color(0x21, 0) == a26_palette_color(0x20, 0));
    tia.fire[0] = 0;
    a26_tia_write(&tia, 1, 0);
    assert(a26_tia_read(&tia, 0x0C, 0) == 0);
    a26_tia_write(&tia, 0x19, 15);
    for (int i = 0; i < 228 * 10; i++) a26_tia_tick(&tia);
    float samples[2048];
    int count = a26_tia_samples(&tia, samples, 2048);
    assert(count > 0);
    int audible = 0;
    for (int i = 0; i < count; i++) if (samples[i] > 0) audible = 1;
    assert(audible);

    uint8_t rom[4096] = {0};
    rom[0] = 0x4C; rom[1] = 0x00; rom[2] = 0xF0;
    rom[0xFFC] = 0; rom[0xFFD] = 0xF0;
    Atari2600 machine;
    assert(a26_init(&machine, rom, sizeof rom, A26_CART_AUTO, 0) == 0);
    InputFrame input = {0};
    input.pad[0] = PAD_UP | PAD_A | PAD_START;
    a26_frame(&machine, &input);
    assert(!(machine.bus.riot.input_a & 0x10));
    assert(!(machine.bus.riot.input_b & 1));
    assert(!machine.tia.input[4]);
    assert(a26_save(&machine, argv[1]) == 0);
    a26_frame(&machine, &input);
    uint64_t cycles = machine.bus.cpu.cycles;
    unsigned x = machine.tia.x, y = machine.tia.y;
    assert(a26_load(&machine, argv[1]) == 0);
    a26_frame(&machine, &input);
    assert(machine.bus.cpu.cycles == cycles && machine.tia.x == x && machine.tia.y == y);
    machine.rom_hash++;
    assert(a26_load(&machine, argv[1]) != 0);
    Atari2600 reference;
    assert(a26_init(&machine, rom, sizeof rom, A26_CART_AUTO, 0) == 0);
    assert(a26_init(&reference, rom, sizeof rom, A26_CART_AUTO, 0) == 0);
    reference.bus.riot.ram[0] = 1;
    machine.reference = &reference;
    a26_frame(&machine, &input);
    assert(machine.diff_failed);
    puts("Atari 2600 runtime tests passed");
    return 0;
}
