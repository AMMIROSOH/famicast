/* A .sms is the raw ROM image; some dumps carry a 512-byte copier header,
 * which is dropped (the size is then 512 over a multiple of 16 KB). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"

int sms_load_rom(const char *path, uint8_t **out, size_t *size, char *err, size_t errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "cannot open %s: %s", path, strerror(errno));
        return 1;
    }
    long n = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    if (n <= 0 || n > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        snprintf(err, errlen, "%s: not a Master System ROM (size)", path);
        return 1;
    }
    uint8_t *data = malloc((size_t)n);
    if (!data) { fclose(f); snprintf(err, errlen, "out of memory"); return 1; }
    if (fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(data); snprintf(err, errlen, "cannot read %s", path); return 1;
    }
    fclose(f);
    size_t skip = n % 0x4000 == 512 ? 512 : 0;
    if (skip) memmove(data, data + skip, (size_t)n - skip);
    *out = data;
    *size = (size_t)n - skip;
    return 0;
}
