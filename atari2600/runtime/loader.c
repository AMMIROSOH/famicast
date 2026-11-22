#include "loader.h"

#include <stdio.h>
#include <stdlib.h>

enum { MAX_ROM_SIZE = 32768 };

int a26_load_rom(const char *path, uint8_t **data, size_t *size, char *err, size_t errlen) {
    *data = NULL;
    *size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) { snprintf(err, errlen, "cannot open ROM: %s", path); return -1; }
    uint8_t *bytes = malloc(MAX_ROM_SIZE + 1);
    if (!bytes) { fclose(file); snprintf(err, errlen, "out of memory"); return -1; }
    size_t length = fread(bytes, 1, MAX_ROM_SIZE + 1, file);
    int failed = ferror(file) || !length || length > MAX_ROM_SIZE;
    fclose(file);
    if (failed) { free(bytes); snprintf(err, errlen, "invalid ROM size or read error"); return -1; }
    *data = bytes;
    *size = length;
    return 0;
}
