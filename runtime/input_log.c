#include "input_log.h"

#include <stdlib.h>
#include <string.h>

#include "nes.h"

static const char MAGIC[5] = {'N', 'E', 'S', 'I', 1};

FILE *input_log_create(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f) fwrite(MAGIC, 1, sizeof MAGIC, f);
    return f;
}

void input_log_write(FILE *f, InputFrame fr) {
    uint8_t b[3] = {fr.pad[0], fr.pad[1], fr.flags};
    fwrite(b, 1, 3, f);
}

int input_log_load(const char *path, InputLog *log) {
    memset(log, 0, sizeof *log);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char head[5];
    fseek(f, 0, SEEK_END);
    long size = ftell(f) - (long)sizeof head;
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size % 3 || fread(head, 1, sizeof head, f) != sizeof head ||
        memcmp(head, MAGIC, sizeof MAGIC) != 0) {
        fclose(f);
        return -1;
    }
    log->count = size / 3;
    log->frames = malloc((size_t)log->count * sizeof *log->frames + 1);
    for (long i = 0; i < log->count; i++) {
        uint8_t b[3];
        if (fread(b, 1, 3, f) != 3) { fclose(f); input_log_free(log); return -1; }
        log->frames[i] = (InputFrame){{b[0], b[1]}, b[2]};
    }
    fclose(f);
    return 0;
}

void input_log_free(InputLog *log) {
    free(log->frames);
    memset(log, 0, sizeof *log);
}

uint8_t input_clean(uint8_t b) {
    if ((b & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) b &= (uint8_t)~(PAD_LEFT | PAD_RIGHT);
    if ((b & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) b &= (uint8_t)~(PAD_UP | PAD_DOWN);
    return b;
}
