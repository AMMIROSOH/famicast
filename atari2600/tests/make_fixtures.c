#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Test cartridges consist only of these generated instructions. */
static int write_rom(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    int failed = fwrite(data, 1, size, f) != size;
    if (fclose(f)) failed = 1;
    return failed;
}

static void make_banked(uint8_t *rom, unsigned banks, uint8_t first, uint8_t last,
                        const uint8_t *start, size_t start_size) {
    size_t size = (size_t)banks * 4096;
    size_t final = size - 4096;
    memset(rom, 0xEA, size);
    memcpy(rom + final, start, start_size);
    rom[final + 0x10] = 0xAD; rom[final + 0x11] = first; rom[final + 0x12] = 0xFF;
    rom[0x13] = 0xAD; rom[0x14] = last; rom[0x15] = 0xFF;
    rom[final + 0x16] = 0x4C; rom[final + 0x17] = 0x00; rom[final + 0x18] = 0xF0;
    if (banks == 2) {
        /* The immediate operand and the next opcode each hit a hotspot. */
        rom[final + 0x17] = 0xF7; rom[final + 0x18] = 0xFF;
        rom[final + 0xFF7] = 0xA9;
        rom[0xFF8] = 0x42;
        rom[final + 0xFF9] = 0x4C;
        rom[final + 0xFFA] = 0x00; rom[final + 0xFFB] = 0xF0;
    }
    rom[size - 4] = 0x00; rom[size - 3] = 0xF0;
}

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    uint8_t fixed[4096], small[2048], banked[32768];
    memset(fixed, 0xEA, sizeof fixed);
    /* Construct JMP $F010 in RIOT RAM and execute it before each loop. */
    const uint8_t start[] = {0xA9, 0x4C, 0x85, 0x80, 0xA9, 0x10, 0x85, 0x81,
                             0xA9, 0xF0, 0x85, 0x82, 0x4C, 0x80, 0x00};
    memcpy(fixed, start, sizeof start);
    fixed[0x10] = 0x4C; fixed[0x11] = 0x00; fixed[0x12] = 0xF0;
    fixed[0xFFC] = 0x00; fixed[0xFFD] = 0xF0;
    memcpy(small, fixed, sizeof small);
    small[2048 - 4] = 0x00; small[2048 - 3] = 0xF0;
    if (write_rom(argv[1], fixed, sizeof fixed) || write_rom(argv[3], small, sizeof small)) return 1;
    /* The last bank reads the first-bank hotspot, then returns to the last. */
    make_banked(banked, 2, 0xF8, 0xF9, start, sizeof start);
    if (write_rom(argv[2], banked, 8192)) return 1;
    make_banked(banked, 4, 0xF6, 0xF9, start, sizeof start);
    if (write_rom(argv[4], banked, 16384)) return 1;
    make_banked(banked, 8, 0xF4, 0xFB, start, sizeof start);
    return write_rom(argv[5], banked, 32768);
}
