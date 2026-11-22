/* The Genesis / Mega Drive, from the file's bytes (core/detect.h): "SEGA"
 * (some games " SEGA") at $100, the header the console's TMSS checks. */
#include <string.h>

#include "detect.h"

static int score(const uint8_t *d, size_t n) {
    if (n < 0x200) return 0;
    return !memcmp(d + 0x100, "SEGA", 4) || !memcmp(d + 0x100, " SEGA", 5) ? 100 : 0;
}

const SystemDetector md_detector = {"md", "Genesis / Mega Drive", ".md .gen .bin", score};
