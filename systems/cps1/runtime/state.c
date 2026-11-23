/* Save states (cps1.h): "CPS1ST01", the game's id (16 bytes), the size of
 * Cps1, then Cps1 with its pointers cleared; loading checks all three and
 * reads into a copy, so a refused file leaves the machine as it was. */
#include <stdio.h>
#include <string.h>

#include "cps1.h"

static const char MAGIC[8] = {'C', 'P', 'S', '1', 'S', 'T', '0', '1'};

int cps1_state_save(const Cps1 *s, const char *path) {
    static Cps1 copy;
    char id[16] = {0};
    strncpy(id, s->rom->game->id, sizeof id - 1);
    copy = *s;
    copy.rom = NULL, copy.cpu.bus = NULL, copy.z80.bus = NULL, copy.oki.rom = NULL;
    uint32_t size = sizeof copy;
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    int bad = fwrite(MAGIC, 1, 8, f) != 8 || fwrite(id, 1, 16, f) != 16 || fwrite(&size, 4, 1, f) != 1 ||
              fwrite(&copy, 1, sizeof copy, f) != sizeof copy;
    return fclose(f) || bad;
}

int cps1_state_load(Cps1 *s, const char *path, char *err, size_t errlen) {
    static Cps1 loaded;
    char magic[8], id[17] = {0};
    uint32_t size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return snprintf(err, errlen, "cannot open state %s", path), 1;
    int bad = fread(magic, 1, 8, f) != 8 || memcmp(magic, MAGIC, 8) || fread(id, 1, 16, f) != 16 ||
              fread(&size, 4, 1, f) != 1 || size != sizeof loaded || fread(&loaded, 1, sizeof loaded, f) != sizeof loaded;
    fclose(f);
    if (bad) return snprintf(err, errlen, "%s: not a CPS-1 state of this version", path), 1;
    if (strncmp(id, s->rom->game->id, 16))
        return snprintf(err, errlen, "%s: a state of %s, not %s", path, id, s->rom->game->id), 1;
    const Cps1Rom *rom = s->rom;
    *s = loaded;
    s->rom = rom, s->cpu.bus = s, s->z80.bus = s;
    s->oki.rom = rom->region[CPS1_R_OKI];
    return 0;
}
