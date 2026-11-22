/* The NES and the Famicom Disk System, from the file's bytes (core/detect.h):
 * an iNES / NES 2.0 header, an FDS image's header, or a raw disk side,
 * which starts with block 1 and "*NINTENDO-HVC*" (the disk BIOS checks it). */
#include <string.h>

#include "detect.h"

static int score(const uint8_t *d, size_t n) {
    if (n >= 16 && (!memcmp(d, "NES\x1A", 4) || !memcmp(d, "FDS\x1A", 4))) return 100;
    if (n >= 16 && d[0] == 1 && !memcmp(d + 1, "*NINTENDO-HVC*", 14)) return 100;
    return 0;
}

const SystemDetector nes_detector = {"nes", "NES / Famicom Disk System", ".nes .fds", score};
