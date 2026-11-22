/* The Atari 2600, from the file's bytes (core/detect.h). Its cartridges have
 * no header, so this is a guess: a size the cartridge code takes (2K, 4K,
 * 8K, 16K or 32K) and a reset vector into the cartridge window ($1000-$1FFF
 * on the 6507's 13-bit bus). Weak on purpose: the .a26 extension
 * (DETECT_EXT_BONUS) or --system confirms it. */
#include "detect.h"

static int score(const uint8_t *d, size_t n) {
    if (n != 2048 && n != 4096 && n != 8192 && n != 16384 && n != 32768) return 0;
    return d[n - 3] & 0x10 ? 30 : 10;  /* the reset vector's high byte, in the last bank */
}

const SystemDetector a26_detector = {"a26", "Atari 2600", ".a26", score};
