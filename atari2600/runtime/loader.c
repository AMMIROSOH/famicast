#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "7z.h"
#include "7zCrc.h"
#include "7zFile.h"

enum { MAX_ROM_SIZE = 32768, MAX_ARCHIVE_ALLOCATION = 32 * 1024 * 1024 };

typedef struct {
    ISzAlloc iface;
    size_t used;
} LimitedAllocator;

typedef union {
    max_align_t alignment;
    size_t size;
} AllocationHeader;

static void *limited_alloc(ISzAllocPtr allocator, size_t size) {
    LimitedAllocator *limited = (LimitedAllocator *)allocator;
    if (!size || size > MAX_ARCHIVE_ALLOCATION - limited->used) return NULL;
    AllocationHeader *header = malloc(sizeof *header + size);
    if (!header) return NULL;
    header->size = size;
    limited->used += size;
    return header + 1;
}

static void limited_free(ISzAllocPtr allocator, void *p) {
    if (!p) return;
    LimitedAllocator *limited = (LimitedAllocator *)allocator;
    AllocationHeader *header = (AllocationHeader *)p - 1;
    limited->used -= header->size;
    free(header);
}

static UInt16 lower_ascii(UInt16 c) { return c >= 'A' && c <= 'Z' ? (UInt16)(c + ('a' - 'A')) : c; }

static int is_rom_name(const UInt16 *name, size_t length) {
    if (length < 5) return 0;
    const UInt16 *extension = name + length - 5;
    return extension[0] == '.' &&
        ((lower_ascii(extension[1]) == 'a' && extension[2] == '2' && extension[3] == '6') ||
         (lower_ascii(extension[1]) == 'b' && lower_ascii(extension[2]) == 'i' && lower_ascii(extension[3]) == 'n'));
}

static int load_archive(const char *path, uint8_t **data, size_t *size, char *err, size_t errlen) {
    LimitedAllocator alloc = {{limited_alloc, limited_free}, 0};
    CFileInStream file;
    CLookToRead2 stream;
    CSzArEx archive;
    Byte buffer[16384];
    Byte *output = NULL;
    size_t output_size = 0, offset = 0, extracted = 0;
    UInt32 block = 0xFFFFFFFF, selected = 0, count = 0;
    int ok = 0;
    if (InFile_Open(&file.file, path)) {
        snprintf(err, errlen, "cannot open archive: %s", path);
        return -1;
    }
    FileInStream_CreateVTable(&file);
    file.wres = 0;
    LookToRead2_CreateVTable(&stream, False);
    stream.buf = buffer;
    stream.bufSize = sizeof buffer;
    stream.realStream = &file.vt;
    LookToRead2_INIT(&stream)
    CrcGenerateTable();
    SzArEx_Init(&archive);
    SRes result = SzArEx_Open(&archive, &stream.vt, &alloc.iface, &alloc.iface);
    if (result != SZ_OK) goto done;
    for (UInt32 i = 0; i < archive.NumFiles; i++) {
        if (SzArEx_IsDir(&archive, i)) continue;
        size_t length = SzArEx_GetFileNameUtf16(&archive, i, NULL);
        if (length > 4096) goto done;
        UInt16 *name = malloc(length * sizeof *name);
        if (!name) goto done;
        SzArEx_GetFileNameUtf16(&archive, i, name);
        int match = is_rom_name(name, length);
        free(name);
        if (match) { selected = i; count++; }
    }
    if (count != 1 || SzArEx_GetFileSize(&archive, selected) > MAX_ROM_SIZE) goto done;
    result = SzArEx_Extract(&archive, &stream.vt, selected, &block, &output,
                            &output_size, &offset, &extracted, &alloc.iface, &alloc.iface);
    if (result != SZ_OK || !extracted || extracted > MAX_ROM_SIZE) goto done;
    *data = malloc(extracted);
    if (!*data) goto done;
    memcpy(*data, output + offset, extracted);
    *size = extracted;
    ok = 1;
done:
    if (!ok) snprintf(err, errlen, "invalid, unsupported, or ambiguous 7z archive: %s", path);
    limited_free(&alloc.iface, output);
    SzArEx_Free(&archive, &alloc.iface);
    File_Close(&file.file);
    return ok ? 0 : -1;
}

int a26_load_rom(const char *path, uint8_t **data, size_t *size, char *err, size_t errlen) {
    *data = NULL;
    *size = 0;
    const char *extension = strrchr(path, '.');
    if (extension && strlen(extension) == 3 && lower_ascii(extension[1]) == '7' && lower_ascii(extension[2]) == 'z')
        return load_archive(path, data, size, err, errlen);
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
