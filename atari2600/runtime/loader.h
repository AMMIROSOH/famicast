#ifndef ATARI2600_LOADER_H
#define ATARI2600_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* Loads a raw cartridge file.
 * The caller owns *data and frees it with free(). */
int a26_load_rom(const char *path, uint8_t **data, size_t *size, char *err, size_t errlen);

#endif
