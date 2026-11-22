/* The Master System, from the file's bytes (core/detect.h). Export games
 * carry "TMR SEGA" at $1FF0, $3FF0 or $7FF0 (the BIOS checks it); its last
 * byte's high nibble is the region, 3 or 4 for the Master System (5-7 is the
 * Game Gear). Many Japanese and Korean games have no header: a size in 8 KB
 * steps is too weak alone, the .sms extension confirms it. */
#include "detect.h"
#include "sms.h"

static int score(const uint8_t *d, size_t n) {
    long h = sms_header(d, n);
    if (h >= 0) {
        unsigned region = d[h + 15] >> 4;
        return region == 3 || region == 4 ? 100 : region >= 5 && region <= 7 ? 0 : 70;
    }
    return n && n % 0x2000 == 0 && n <= 0x100000 ? 20 : 0;
}

const SystemDetector sms_detector = {"sms", "Master System / Mark III", ".sms", score};
