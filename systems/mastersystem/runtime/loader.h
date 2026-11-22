#ifndef SMS_LOADER_H
#define SMS_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* Reads a raw .sms; the caller frees *out. Returns 0 on success. */
int sms_load_rom(const char *path, uint8_t **out, size_t *size, char *err, size_t errlen);

#endif
