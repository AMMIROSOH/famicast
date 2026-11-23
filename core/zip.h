/* Zip files, read in memory: the central directory, stored and deflate
 * entries (RFC 1951, our own inflate), the CRC-32 check. Arcade games come
 * as zips of chip dumps (systems/cps1). No ZIP64, no encryption. */
#ifndef CORE_ZIP_H
#define CORE_ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;      /* into the zip's data, not NUL-terminated */
    uint16_t name_len;
    uint16_t method;       /* 0 stored, 8 deflate */
    uint32_t crc, size, csize;
    uint32_t header;       /* offset of the local header */
} ZipEntry;

typedef struct {
    const uint8_t *data;
    size_t size;
    int count;
    ZipEntry *entries;
} Zip;

/* 0, or -1: not a zip or a damaged directory. `data` must outlive the Zip. */
int zip_open(Zip *z, const uint8_t *data, size_t size);
void zip_close(Zip *z);
/* The entry with this CRC and size, or NULL. */
const ZipEntry *zip_find_crc(const Zip *z, uint32_t crc, uint32_t size);
/* Unpacks e into out (e->size bytes): 0, or -1 if damaged, of an unknown
 * method, or its CRC does not match. */
int zip_read(const Zip *z, const ZipEntry *e, uint8_t *out);
uint32_t zip_crc32(const uint8_t *p, size_t n);
/* Raw deflate data: the bytes written, or -1 if damaged or over cap. */
long inflate_raw(const uint8_t *in, size_t n, uint8_t *out, size_t cap);

#endif
