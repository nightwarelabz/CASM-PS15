/* Persistence: portable little-endian binary save with magic, version, and
 * IEEE-754 bit-exact CRC32 checksum (zero float-to-uint UB). */
#include "engine.h"

#define SAVE_MAGIC   0xCA151502u
#define SAVE_VERSION 2u

static uint32_t f32_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float bits_f32(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static void put_u32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_u32_le(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void pack_payload(uint8_t out[28], const SaveState *s) {
    put_u32_le(out + 0,  s->magic);
    put_u32_le(out + 4,  s->version);
    put_u32_le(out + 8,  f32_bits(s->player_pos.x));
    put_u32_le(out + 12, f32_bits(s->player_pos.y));
    put_u32_le(out + 16, f32_bits(s->player_pos.z));
    put_u32_le(out + 20, f32_bits(s->alert));
    put_u32_le(out + 24, s->frames);
}

int save_write(const char *path, const SaveState *sin) {
    if (!path || !sin) return -1;
    SaveState s = *sin;
    s.magic = SAVE_MAGIC;
    s.version = SAVE_VERSION;
    uint8_t raw[32];
    pack_payload(raw, &s);
    uint32_t crc = crc32_bytes(raw, 28);
    put_u32_le(raw + 28, crc);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t wr = fwrite(raw, 1, sizeof raw, f);
    int rc = fclose(f);
    return (wr == sizeof raw && rc == 0) ? 0 : -1;
}

int save_read(const char *path, SaveState *s) {
    if (!path || !s) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t raw[32];
    size_t rd = fread(raw, 1, sizeof raw, f);
    fclose(f);
    if (rd != sizeof raw) return -1;

    uint32_t expected_crc = crc32_bytes(raw, 28);
    uint32_t stored_crc = get_u32_le(raw + 28);
    if (expected_crc != stored_crc) return -1;

    s->magic        = get_u32_le(raw + 0);
    s->version      = get_u32_le(raw + 4);
    s->player_pos.x = bits_f32(get_u32_le(raw + 8));
    s->player_pos.y = bits_f32(get_u32_le(raw + 12));
    s->player_pos.z = bits_f32(get_u32_le(raw + 16));
    s->alert        = bits_f32(get_u32_le(raw + 20));
    s->frames       = get_u32_le(raw + 24);
    s->checksum     = stored_crc;

    if (s->magic != SAVE_MAGIC || s->version != SAVE_VERSION) return -1;
    return 0;
}
