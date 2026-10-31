/* Procedural painted textures: brick, wet concrete, riveted metal,
 * black camo, and a face/visor plate. 64x64, deterministic, zero assets. */
#include "engine.h"

static uint32_t pack(int r, int g, int b) {
    r = (int)clampf((float)r, 0, 255); g = (int)clampf((float)g, 0, 255);
    b = (int)clampf((float)b, 0, 255);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static int hash2(int x, int y, uint32_t seed) {
    uint32_t h = ((uint32_t)x * 374761393u + (uint32_t)y * 668265263u) ^ seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 255);
}

void texgen_all(Tex out[TEX_COUNT]) {
    for (int k = 0; k < TEX_COUNT; k++) {
        out[k].w = out[k].h = 64;
        out[k].px = malloc(sizeof(uint32_t) * 64 * 64);
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int n = hash2(x, y, 7);
            /* brick: courses with mortar lines, per-brick value shift */
            {
                int row = y / 8, off = (row & 1) ? 16 : 0;
                int mortar = (y % 8 == 0) || ((x + off) % 32 == 0);
                int bv = hash2(x / 32 + off, row, 21) / 3;
                out[TEX_BRICK].px[y * 64 + x] = mortar
                    ? pack(52, 50, 48)
                    : pack(104 + bv + n / 6, 62 + bv / 2 + n / 9, 48 + n / 10);
            }
            /* wet concrete: low noise, darker streaks running down */
            {
                int streak = hash2(x, 0, 33) / 4;
                int v = 74 + n / 4 - streak / 2 - (y / 16);
                out[TEX_CONCRETE].px[y * 64 + x] = pack(v, v + 4, v + 10);
            }
            /* riveted metal plate: panel border + corner rivets */
            {
                bool border = x < 2 || y < 2 || x > 61 || y > 61;
                bool rivet = ((x == 5 || x == 58) && (y == 5 || y == 58));
                int v = 84 + n / 5;
                out[TEX_METAL].px[y * 64 + x] = rivet ? pack(150, 152, 158)
                    : border ? pack(44, 46, 52) : pack(v, v + 2, v + 8);
            }
            /* black camo: blotches of near-black greys/greens */
            {
                int cell = hash2(x / 11, y / 9, 55);
                int v = 26 + cell / 6 + n / 8;
                out[TEX_CAMO].px[y * 64 + x] = (cell > 150)
                    ? pack(v, v + 6, v) : pack(v, v + 2, v + 5);
            }
            /* face plate: dark balaclava with a visor band */
            {
                bool visor = y >= 24 && y < 34;
                int v = 30 + n / 7;
                out[TEX_FACE].px[y * 64 + x] = visor ? pack(12, 16, 22)
                                               : pack(v, v, v + 3);
            }
        }
    }
}
void texgen_free(Tex t[TEX_COUNT]) {
    for (int k = 0; k < TEX_COUNT; k++) free(t[k].px);
}
