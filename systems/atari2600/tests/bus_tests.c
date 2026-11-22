#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"

static unsigned clocks;
static void tick_tia(void *tia) { (void)tia; clocks++; }
static uint8_t read_tia(void *tia, uint16_t address, uint8_t open_bus) {
    (void)tia; (void)address; (void)open_bus;
    return 0xAB;
}

int main(void) {
    A26Bus bus;
    uint8_t rom[8192];
    memset(rom, 0x21, 4096);
    memset(rom + 4096, 0x42, 4096);
    a26_bus_init(&bus);
    assert(a26_cart_init(&bus.cart, rom, sizeof rom, A26_CART_F8) == 0);
    bus.tia_tick = tick_tia;
    bus.tia_read = read_tia;
    assert(bus.cpu.decimal_enabled);
    cpu_write(&bus.cpu, 0x0080, 0x53);
    assert(cpu_read(&bus.cpu, 0x0180) == 0x53);
    assert(cpu_read(&bus.cpu, 0x2000) == 0xAB);
    assert(cpu_read(&bus.cpu, 0xF000) == 0x42);
    assert(cpu_read(&bus.cpu, 0xFFF8) == 0x21);
    assert(cpu_read(&bus.cpu, 0x1000) == 0x21);
    assert(clocks == 18);

    A26Riot riot;
    a26_riot_init(&riot);
    riot.input_a = 0xEF;
    assert(a26_riot_read(&riot, 0x280) == 0xEF);
    a26_riot_write(&riot, 0x281, 0x80);
    a26_riot_write(&riot, 0x280, 0x00);
    assert(a26_riot_read(&riot, 0x280) == 0x6F);
    a26_riot_write(&riot, 0x295, 1);
    a26_riot_tick(&riot);
    assert(a26_riot_read(&riot, 0x284) == 0);
    for (int i = 0; i < 8; i++) a26_riot_tick(&riot);
    assert(a26_riot_read(&riot, 0x285) & 0x80);
    assert(a26_riot_read(&riot, 0x284) == 0xFF);
    assert(a26_riot_read(&riot, 0x285) & 0x80);
    a26_riot_tick(&riot);
    assert(a26_riot_read(&riot, 0x284) == 0xFE);
    assert(!(a26_riot_read(&riot, 0x285) & 0x80));
    puts("Atari 2600 bus and RIOT tests passed");
    return 0;
}
