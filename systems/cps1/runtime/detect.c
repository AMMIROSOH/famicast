/* The CPS-1 detector (core/detect.h): a zip holding every file of a game in
 * the table, by CRC and size. */
#include "cps1.h"
#include "detect.h"

static int score(const uint8_t *data, size_t size) {
    Zip z;
    if (zip_open(&z, data, size)) return 0;
    int found = cps1_match(&z) != NULL;
    zip_close(&z);
    return found ? 100 : 0;
}

const SystemDetector cps1_detector = {"cps1", "Capcom Play System I", ".zip", score};
