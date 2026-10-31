/* Persistence: tiny binary save with magic, version and checksum. */
#include "engine.h"

#define SAVE_MAGIC 0xCA151501u
static uint32_t checksum(const SaveState *s) {
    return (uint32_t)(s->player_pos.x * 977) ^ (uint32_t)(s->player_pos.z * 131) ^
           s->frames * 2654435761u ^ (uint32_t)(s->alert * 1000) ^ 0x51ab3u;
}
int save_write(const char *path, const SaveState *sin) {
    SaveState s = *sin;
    s.magic = SAVE_MAGIC;
    s.version = 1;
    s.checksum = checksum(&s);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(&s, sizeof s, 1, f);
    fclose(f);
    return 0;
}
int save_read(const char *path, SaveState *s) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(s, sizeof *s, 1, f) != 1) { fclose(f); return -1; }
    fclose(f);
    if (s->magic != SAVE_MAGIC || s->version != 1) return -1;
    if (s->checksum != checksum(s)) return -1;
    return 0;
}
