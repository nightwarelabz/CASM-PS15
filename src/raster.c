/* CASM-PS15 rasterizer: software triangles at 480x270, vertex snapping,
 * RGB555 quantization with ordered dither, depth fog, glow sprites, HUD. */
#include "engine.h"

#ifndef CASM_HAVE_ASM_FILL
void ps15_fill32(uint32_t *dst, uint32_t value, size_t count) {
    for (size_t i = 0; i < count; i++) dst[i] = value;
}
#endif

FB *fb_new(int w, int h) {
    FB *fb = malloc(sizeof *fb);
    fb->w = w; fb->h = h;
    fb->px = malloc(sizeof(uint32_t) * (size_t)(w * h));
    fb->zb = malloc(sizeof(float) * (size_t)(w * h));
    return fb;
}
void fb_free(FB *fb) { free(fb->px); free(fb->zb); free(fb); }

static const int BAYER[4][4] = {
    { 0,  8,  2, 10 }, { 12,  4, 14,  6 }, { 3, 11,  1,  9 }, { 15,  7, 13,  5 }
};
uint32_t rgb555(int r, int g, int b, int x, int y) {
    int d = (BAYER[y & 3][x & 3] - 8) / 2;
    r = (int)clampf((float)(r + d), 0, 255);
    g = (int)clampf((float)(g + d), 0, 255);
    b = (int)clampf((float)(b + d), 0, 255);
    r = (r >> 3) << 3; g = (g >> 3) << 3; b = (b >> 3) << 3;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void fb_sky(FB *fb) {
    for (int y = 0; y < fb->h; y++) {
        float t = (float)y / fb->h;
        int r = (int)(10 + 14 * t), g = (int)(14 + 20 * t), b = (int)(26 + 30 * t);
        ps15_fill32(fb->px + (size_t)y * fb->w,
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b, (size_t)fb->w);
    }
    Rng rng = { 0xC350001ULL };
    for (int i = 0; i < 220; i++) {           /* faint stars, upper sky */
        int x = (int)(rng_next(&rng) % (uint32_t)fb->w);
        int y = (int)(rng_next(&rng) % (uint32_t)(fb->h / 2));
        int v = 90 + (int)(rng_next(&rng) % 90);
        fb->px[y * fb->w + x] = ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)(v + 20);
    }
}

void fb_clear_depth(FB *fb) {
    for (int i = 0; i < fb->w * fb->h; i++) fb->zb[i] = 1e9f;
}

void raster_tri(FB *fb, SVert a, SVert b, SVert c,
                const uint32_t *tex, int tw, int th,
                float tr, float tg, float tb, float fogz) {
    float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (d == 0) return;
    int minx = (int)fminf(a.x, fminf(b.x, c.x)), maxx = (int)fmaxf(a.x, fmaxf(b.x, c.x));
    int miny = (int)fminf(a.y, fminf(b.y, c.y)), maxy = (int)fmaxf(a.y, fmaxf(b.y, c.y));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= fb->w) maxx = fb->w - 1;
    if (maxy >= fb->h) maxy = fb->h - 1;
    float fog = clampf(fogz / 70.0f, 0, 0.78f);
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float w0 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
            float w1 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a.z + w1 * b.z + w2 * c.z;
            int idx = y * fb->w + x;
            if (z >= fb->zb[idx]) continue;
            float light = w0 * a.light + w1 * b.light + w2 * c.light;
            int r, g, bl;
            if (tex) {                        /* affine UV — the PS1 way */
                float u = w0 * a.u + w1 * b.u + w2 * c.u;
                float v = w0 * a.v + w1 * b.v + w2 * c.v;
                int tx = (int)(u * tw) % tw; if (tx < 0) tx += tw;
                int ty = (int)(v * th) % th; if (ty < 0) ty += th;
                uint32_t t = tex[ty * tw + tx];
                r = (int)(((t >> 16) & 255) * tr * light);
                g = (int)(((t >> 8) & 255) * tg * light);
                bl = (int)((t & 255) * tb * light);
            } else {
                r = (int)(tr * 255 * light);
                g = (int)(tg * 255 * light);
                bl = (int)(tb * 255 * light);
            }
            r = (int)(r * (1 - fog) + 34 * fog);
            g = (int)(g * (1 - fog) + 44 * fog);
            bl = (int)(bl * (1 - fog) + 62 * fog);
            fb->zb[idx] = z;
            fb->px[idx] = rgb555(r, g, bl, x, y);
        }
    }
}

void raster_glow(FB *fb, int cx, int cy, int radius, int r, int g, int b) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        if (y < 0 || y >= fb->h) continue;
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= fb->w) continue;
            float d2 = (float)((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float t = 1.0f - d2 / (float)(radius * radius);
            if (t <= 0) continue;
            int idx = y * fb->w + x;
            if (fb->zb[idx] < 30.0f && d2 > (float)(radius * radius) / 16) continue;
            uint32_t p = fb->px[idx];
            int rr = (int)clampf(((p >> 16) & 255) + r * t * t, 0, 255);
            int gg = (int)clampf(((p >> 8) & 255) + g * t * t, 0, 255);
            int bb = (int)clampf((p & 255) + b * t * t, 0, 255);
            fb->px[idx] = rgb555(rr, gg, bb, x, y);
        }
    }
}

void raster_rect(FB *fb, int x, int y, int w, int h, uint32_t col) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= fb->h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= fb->w) continue;
            fb->px[yy * fb->w + xx] = col;
        }
    }
}
void raster_bar(FB *fb, int x, int y, int w, int h, float frac, uint32_t col) {
    raster_rect(fb, x, y, w, h, 0x101018);
    raster_rect(fb, x, y, (int)(w * clampf(frac, 0, 1)), h, col);
}

/* 3x5 digit font */
static const uint8_t DIGITS[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}
};
void raster_number(FB *fb, int x, int y, int value, uint32_t col) {
    char buf[12];
    snprintf(buf, sizeof buf, "%d", value);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] < '0' || buf[i] > '9') continue;
        const uint8_t *g = DIGITS[buf[i] - '0'];
        for (int row = 0; row < 5; row++)
            for (int colbit = 0; colbit < 3; colbit++)
                if (g[row] & (4 >> colbit))
                    raster_rect(fb, x + i * 4 + colbit, y + row, 1, 1, col);
    }
}

uint32_t *fb_upscale2x(const FB *fb) {
    uint32_t *out = malloc(sizeof(uint32_t) * (size_t)(fb->w * 2) * (fb->h * 2));
    for (int y = 0; y < fb->h * 2; y++)
        for (int x = 0; x < fb->w * 2; x++)
            out[y * fb->w * 2 + x] = fb->px[(y / 2) * fb->w + (x / 2)];
    return out;
}
