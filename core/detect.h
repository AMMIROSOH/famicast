/* Which system a file is for, from its bytes. Each system has a detector
 * (systems/<id>/runtime/detect.c) scoring the file's bytes 0-100:
 *   100  a header the console itself checks (NES "NES\x1A", the Game Boy's
 *        logo and header checksum, the Genesis "SEGA" at $100)
 *   1-50 a guess from the size and contents (the Atari 2600 has no header)
 * detect_system picks the best. The extension only breaks ties: a system
 * listing it scores +20. CMake's ROMS list uses the same rules
 * (cmake/detect_system.cmake; tests/test_detect.py checks they agree). */
#ifndef CORE_DETECT_H
#define CORE_DETECT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *id;     /* as recordings name the system: "nes", "gb", "md", "a26" */
    const char *name;   /* for messages: "NES / Famicom Disk System" */
    const char *exts;   /* the file extensions, space separated: ".nes .fds" */
    int (*score)(const uint8_t *data, size_t size);
} SystemDetector;

enum { DETECT_SURE = 90, DETECT_MIN = 30, DETECT_EXT_BONUS = 20 };

/* The index of the system `data` (from `path`, for its extension) is for,
 * among n detectors; -1 with a message in err when none fits or two fit
 * equally well (then: --system). */
int detect_system(const SystemDetector *const *dets, int n, const uint8_t *data, size_t size,
                  const char *path, char *err, size_t errlen);

#endif
