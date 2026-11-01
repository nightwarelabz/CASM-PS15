/* CASM-PS15 rasterizer: software scanline triangles at 480x270, 8-pixel
 * perspective-correct spans (with ASM inner kernel), PS1 ABR blend modes,
 * RGB555 Bayer ordered dithering, 3D star/moon sky dome, depth-tested lines,
 * occluded glow sprites, and 4x6 tactical HUD bitmap font. */
#include "engine.h"

_Static_assert(offsetof(Span64, row_px)  == 0,   "Span64.row_px offset");
_Static_assert(offsetof(Span64, row_zb)  == 8,   "Span64.row_zb offset");
_Static_assert(offsetof(Span64, tex)     == 16,  "Span64.tex offset");
_Static_assert(offsetof(Span64, x)       == 24,  "Span64.x offset");
_Static_assert(offsetof(Span64, count)   == 28,  "Span64.count offset");
_Static_assert(offsetof(Span64, invz)    == 32,  "Span64.invz offset");
_Static_assert(offsetof(Span64, dinvz)   == 36,  "Span64.dinvz offset");
_Static_assert(offsetof(Span64, u)       == 40,  "Span64.u offset");
_Static_assert(offsetof(Span64, v)       == 48,  "Span64.v offset");
_Static_assert(offsetof(Span64, r)       == 56,  "Span64.r offset");
_Static_assert(offsetof(Span64, g)       == 64,  "Span64.g offset");
_Static_assert(offsetof(Span64, b)       == 72,  "Span64.b offset");
_Static_assert(offsetof(Span64, inv_fog) == 80,  "Span64.inv_fog offset");
_Static_assert(offsetof(Span64, dither)  == 96,  "Span64.dither offset");
_Static_assert(sizeof(Span64)            == 112, "Span64 total size");

/* ---------- portable C reference implementations of ASM hot loops ----- */
void ps15_c_fill32(uint32_t *dst, uint32_t value, size_t count) {
    for (size_t i = 0; i < count; i++) dst[i] = value;
}

void ps15_c_upscale2x(uint32_t *dst, const uint32_t *src, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int dst_stride = w * 2;
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = src + (size_t)y * (size_t)w;
        uint32_t *drow0 = dst + (size_t)(y * 2) * (size_t)dst_stride;
        uint32_t *drow1 = drow0 + dst_stride;
        for (int x = 0; x < w; x++) {
            uint32_t p = srow[x];
            drow0[x * 2] = p; drow0[x * 2 + 1] = p;
            drow1[x * 2] = p; drow1[x * 2 + 1] = p;
        }
    }
}

void ps15_c_upscale2x_crt(uint32_t *dst, const uint32_t *src, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int dst_stride = w * 2;
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = src + (size_t)y * (size_t)w;
        uint32_t *drow0 = dst + (size_t)(y * 2) * (size_t)dst_stride;
        uint32_t *drow1 = drow0 + dst_stride;
        for (int x = 0; x < w; x++) {
            uint32_t p = srow[x];
            uint32_t s = p - ((p >> 2) & 0x003F3F3Fu);
            drow0[x * 2] = p; drow0[x * 2 + 1] = p;
            drow1[x * 2] = s; drow1[x * 2 + 1] = s;
        }
    }
}

void ps15_c_span_tex64(const Span64 *s) {
    if (s->count <= 0) return;
    uint32_t *px = s->row_px + s->x;
    float *zb = s->row_zb + s->x;
    const uint32_t *tex = s->tex;
    int32_t x = s->x;
    int32_t n = s->count;
    float invz = s->invz, dinvz = s->dinvz;
    uint32_t u = s->u, du = s->du;
    uint32_t v = s->v, dv = s->dv;
    uint32_t r = (uint32_t)s->r, dr = (uint32_t)s->dr;
    uint32_t g = (uint32_t)s->g, dg = (uint32_t)s->dg;
    uint32_t b = (uint32_t)s->b, db = (uint32_t)s->db;
    int32_t inv_fog = s->inv_fog;
    int32_t fogr = s->fogr, fogg = s->fogg, fogb = s->fogb;

    while (n > 0) {
        if (invz > *zb) {
            uint32_t tx = (u >> 16) & 63u;
            uint32_t ty = (v >> 16) & 63u;
            uint32_t t = tex[(ty << 6) | tx];
            if (!(t & TEX_MASK_BIT)) {
                *zb = invz;
                int32_t rl = ((int32_t)r) >> 16; if (rl < 0) rl = 0;
                int32_t gl = ((int32_t)g) >> 16; if (gl < 0) gl = 0;
                int32_t bl = ((int32_t)b) >> 16; if (bl < 0) bl = 0;

                int32_t cr = ((int32_t)((t >> 16) & 255u) * rl) >> 7;
                int32_t cg = ((int32_t)((t >> 8)  & 255u) * gl) >> 7;
                int32_t cb = ((int32_t)( t        & 255u) * bl) >> 7;
                if (cr > 255) cr = 255;
                if (cg > 255) cg = 255;
                if (cb > 255) cb = 255;

                cr = (cr * inv_fog + fogr) >> 8;
                cg = (cg * inv_fog + fogg) >> 8;
                cb = (cb * inv_fog + fogb) >> 8;

                int32_t d = s->dither[x & 3];
                cr += d; if (cr < 0) cr = 0; if (cr > 255) cr = 255;
                cg += d; if (cg < 0) cg = 0; if (cg > 255) cg = 255;
                cb += d; if (cb < 0) cb = 0; if (cb > 255) cb = 255;

                *px = (((uint32_t)cr & 0xF8u) << 16) |
                      (((uint32_t)cg & 0xF8u) << 8)  |
                      ((uint32_t)cb & 0xF8u);
            }
        }
        invz += dinvz;
        u += du; v += dv;
        r += dr; g += dg; b += db;
        px++; zb++; x++; n--;
    }
}

void ps15_c_span_abr64(const Span64 *s, int abr_mode) {
    if (s->count <= 0) return;
    uint32_t *px = s->row_px + s->x;
    const float *zb = s->row_zb + s->x;
    const uint32_t *tex = s->tex;
    int32_t x = s->x;
    int32_t n = s->count;
    float invz = s->invz, dinvz = s->dinvz;
    uint32_t u = s->u, du = s->du;
    uint32_t v = s->v, dv = s->dv;
    uint32_t r = (uint32_t)s->r, dr = (uint32_t)s->dr;
    uint32_t g = (uint32_t)s->g, dg = (uint32_t)s->dg;
    uint32_t b = (uint32_t)s->b, db = (uint32_t)s->db;
    int32_t inv_fog = s->inv_fog;
    int32_t fogr = s->fogr, fogg = s->fogg, fogb = s->fogb;

    while (n > 0) {
        if (invz > *zb) {
            uint32_t tx = (u >> 16) & 63u;
            uint32_t ty = (v >> 16) & 63u;
            uint32_t t = tex[(ty << 6) | tx];
            if (!(t & TEX_MASK_BIT)) {
                int32_t rl = ((int32_t)r) >> 16; if (rl < 0) rl = 0;
                int32_t gl = ((int32_t)g) >> 16; if (gl < 0) gl = 0;
                int32_t bl = ((int32_t)b) >> 16; if (bl < 0) bl = 0;

                int32_t cr = ((int32_t)((t >> 16) & 255u) * rl) >> 7;
                int32_t cg = ((int32_t)((t >> 8)  & 255u) * gl) >> 7;
                int32_t cb = ((int32_t)( t        & 255u) * bl) >> 7;
                if (cr > 255) cr = 255;
                if (cg > 255) cg = 255;
                if (cb > 255) cb = 255;

                cr = (cr * inv_fog + fogr) >> 8;
                cg = (cg * inv_fog + fogg) >> 8;
                cb = (cb * inv_fog + fogb) >> 8;

                uint32_t bg = *px;
                int32_t br  = (int32_t)((bg >> 16) & 255u);
                int32_t bgc = (int32_t)((bg >> 8)  & 255u);
                int32_t bb  = (int32_t)( bg        & 255u);

                if (abr_mode == 1) {
                    cr = (br + cr) >> 1;
                    cg = (bgc + cg) >> 1;
                    cb = (bb + cb) >> 1;
                } else if (abr_mode == 2) {
                    cr = br + cr;
                    cg = bgc + cg;
                    cb = bb + cb;
                } else if (abr_mode == 3) {
                    cr = br - cr;
                    cg = bgc - cg;
                    cb = bb - cb;
                } else {
                    cr = br + (cr >> 2);
                    cg = bgc + (cg >> 2);
                    cb = bb + (cb >> 2);
                }

                int32_t d = s->dither[x & 3];
                cr += d; if (cr < 0) cr = 0; if (cr > 255) cr = 255;
                cg += d; if (cg < 0) cg = 0; if (cg > 255) cg = 255;
                cb += d; if (cb < 0) cb = 0; if (cb > 255) cb = 255;

                *px = (((uint32_t)cr & 0xF8u) << 16) |
                      (((uint32_t)cg & 0xF8u) << 8)  |
                      ((uint32_t)cb & 0xF8u);
            }
        }
        invz += dinvz;
        u += du; v += dv;
        r += dr; g += dg; b += db;
        px++; zb++; x++; n--;
    }
}

#ifndef CASM_HAVE_ASM_FILL
void ps15_fill32(uint32_t *dst, uint32_t value, size_t count) {
    ps15_c_fill32(dst, value, count);
}
void ps15_upscale2x(uint32_t *dst, const uint32_t *src, int w, int h) {
    ps15_c_upscale2x(dst, src, w, h);
}
void ps15_upscale2x_crt(uint32_t *dst, const uint32_t *src, int w, int h) {
    ps15_c_upscale2x_crt(dst, src, w, h);
}
void ps15_span_tex64(const Span64 *s) {
    ps15_c_span_tex64(s);
}
void ps15_span_abr64(const Span64 *s, int abr_mode) {
    ps15_c_span_abr64(s, abr_mode);
}
#endif

static uint32_t WHITE_TEX64[64 * 64];
static int white_tex_ready = 0;

/* ---------- framebuffer lifecycle & Bayer RGB555 DAC ------------------ */
FB *fb_new(int w, int h) {
    if (!white_tex_ready) {
        ps15_fill32(WHITE_TEX64, 0x00FFFFFFu, 64 * 64);
        white_tex_ready = 1;
    }
    FB *fb = calloc(1, sizeof *fb);
    fb->w = w; fb->h = h;
    fb->px = calloc((size_t)(w * h), sizeof(uint32_t));
    fb->zb = calloc((size_t)(w * h), sizeof(float));
    fb->settings.snap_mode = SNAP_PS15;
    fb->settings.dither_mode = DITHER_BAYER555;
    fb->settings.filter_mode = FILTER_POINT;
    fb->settings.profile = GFX_PS15;
    fb->settings.affine_uv = false;
    fb->settings.wireframe = false;
    fb->settings.fov_scale = 1.0f;
    return fb;
}

void fb_set_profile(FB *fb, GraphicProfile profile) {
    if (!fb) return;
    fb->settings.profile = profile;
    switch (profile) {
        case GFX_PS1:
            fb->settings.snap_mode = SNAP_PS1;
            fb->settings.dither_mode = DITHER_BAYER555;
            fb->settings.filter_mode = FILTER_POINT;
            fb->settings.affine_uv = true;
            break;
        case GFX_PS15:
            fb->settings.snap_mode = SNAP_PS15;
            fb->settings.dither_mode = DITHER_BAYER555;
            fb->settings.filter_mode = FILTER_POINT;
            fb->settings.affine_uv = false;
            break;
        case GFX_PS2:
        default:
            fb->settings.snap_mode = SNAP_OFF;
            fb->settings.dither_mode = DITHER_RGB888;
            fb->settings.filter_mode = FILTER_BILINEAR;
            fb->settings.affine_uv = false;
            break;
    }
}

void fb_free(FB *fb) {
    if (!fb) return;
    free(fb->px);
    free(fb->zb);
    free(fb);
}

/* Balanced PS1.5 4x4 Bayer ordered dither offsets (-2 .. +2) for silky 15-bit gradients */
static const int32_t BAYER_BIAS[4][4] = {
    { -2,  1, -1,  2 },
    {  2, -1,  1, -2 },
    { -1,  2, -2,  1 },
    {  1, -2,  2, -1 }
};

uint32_t rgb555(int r, int g, int b, int x, int y) {
    int d = BAYER_BIAS[y & 3][x & 3];
    r += d; if (r < 0) r = 0; if (r > 255) r = 255;
    g += d; if (g < 0) g = 0; if (g > 255) g = 255;
    b += d; if (b < 0) b = 0; if (b > 255) b = 255;
    return (((uint32_t)r & 0xF8u) << 16) |
           (((uint32_t)g & 0xF8u) << 8)  |
           ((uint32_t)b & 0xF8u);
}

uint32_t quantize_pixel(const FB *fb, int r, int g, int b, int x, int y) {
    if (fb->settings.dither_mode == DITHER_BAYER555)
        return rgb555(r, g, b, x, y);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    if (fb->settings.dither_mode == DITHER_FLAT555) {
        return (((uint32_t)r & 0xF8u) << 16) |
               (((uint32_t)g & 0xF8u) << 8)  |
               ((uint32_t)b & 0xF8u);
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint32_t blend_ps1(const FB *fb, uint32_t bg,
                                 int fr, int fg, int fb_c,
                                 BlendMode mode, int x, int y) {
    if (mode == BLEND_OPAQUE) return quantize_pixel(fb, fr, fg, fb_c, x, y);
    int br = (int)((bg >> 16) & 255u);
    int bgc = (int)((bg >> 8) & 255u);
    int bb = (int)(bg & 255u);
    int or_, og, ob;
    switch (mode) {
        case BLEND_HALF:
            or_ = (br + fr) >> 1;
            og  = (bgc + fg) >> 1;
            ob  = (bb + fb_c) >> 1;
            break;
        case BLEND_ADD:
            or_ = br + fr; og = bgc + fg; ob = bb + fb_c;
            break;
        case BLEND_SUB:
            or_ = br - fr; og = bgc - fg; ob = bb - fb_c;
            break;
        case BLEND_QUARTER:
        default:
            or_ = br + (fr >> 2);
            og  = bgc + (fg >> 2);
            ob  = bb + (fb_c >> 2);
            break;
    }
    return quantize_pixel(fb, or_, og, ob, x, y);
}

/* ---------- 3D camera-aware cold-war nocturne sky dome & clouds ------- */
void fb_sky_cam(FB *fb, const Camera *cam) {
    float yaw = cam ? cam->yaw : 0.15f;
    float pitch = cam ? cam->pitch : -0.15f;
    float F = 215.0f * (fb->settings.fov_scale > 0.1f ? fb->settings.fov_scale : 1.0f);
    float horizon_y = (float)fb->h * 0.5f + sinf(pitch) * F / fmaxf(0.25f, cosf(pitch));

    for (int y = 0; y < fb->h; y++) {
        float dy = (horizon_y - (float)y) / (float)fb->h; /* >0 above horizon */
        int r, g, b;
        if (dy >= 0.0f) {
            float t = clampf(1.0f - dy * 1.55f, 0.0f, 1.0f);
            float ts = t * t * (3.0f - 2.0f * t);
            r = (int)(10.0f + 16.0f * ts);
            g = (int)(15.0f + 24.0f * ts);
            b = (int)(29.0f + 33.0f * t);
        } else {
            float t = clampf(-dy * 2.6f, 0.0f, 1.0f);
            r = (int)lerpf(26.0f, 12.0f, t);
            g = (int)lerpf(39.0f, 17.0f, t);
            b = (int)lerpf(62.0f, 28.0f, t);
        }
        uint32_t *row = fb->px + (size_t)y * (size_t)fb->w;
        if (fb->settings.dither_mode == DITHER_BAYER555) {
            uint32_t pat[4];
            for (int k = 0; k < 4; k++) pat[k] = rgb555(r, g, b, k, y);
            int x = 0;
            for (; x + 3 < fb->w; x += 4) {
                row[x] = pat[0]; row[x + 1] = pat[1];
                row[x + 2] = pat[2]; row[x + 3] = pat[3];
            }
            for (; x < fb->w; x++) row[x] = pat[x & 3];
        } else {
            uint32_t c = quantize_pixel(fb, r, g, b, 0, y);
            ps15_fill32(row, c, (size_t)fb->w);
        }
    }

    /* Subtle overcast night cloud bands above horizon (matches Greyport nocturne) */
    for (int y = 0; y < fb->h; y += 2) {
        float dy = (horizon_y - (float)y) / (float)fb->h;
        if (dy < 0.02f || dy > 0.72f) continue;
        float band_env = sinf(clampf((dy - 0.02f) / 0.70f, 0.0f, 1.0f) * PS15_PI);
        for (int x = 0; x < fb->w; x += 4) {
            float az = yaw + ((float)x - (float)fb->w * 0.5f) / F;
            float w = sinf(az * 3.1f + dy * 9.0f) * 0.5f +
                      cosf(az * 6.7f - dy * 14.0f) * 0.5f;
            if (w <= 0.12f) continue;
            int lift = (int)((w - 0.12f) * band_env * 16.0f);
            if (lift <= 0) continue;
            for (int yy = y; yy < y + 2 && yy < fb->h; yy++) {
                for (int xx = x; xx < x + 4 && xx < fb->w; xx++) {
                    fb->px[yy * fb->w + xx] = blend_ps1(
                        fb, fb->px[yy * fb->w + xx],
                        lift / 2 + 1, (lift * 3) / 4 + 1, lift + 3,
                        BLEND_ADD, xx, yy);
                }
            }
        }
    }

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);

    /* Subtle high-zenith pinpoint stars peeking through night mist */
    Rng rng = { 0xC350001ULL };
    for (int i = 0; i < 120; i++) {
        float az = rng_f01(&rng) * 2.0f * PS15_PI;
        float el = 0.34f + rng_f01(&rng) * 1.12f;
        int lum = 48 + (int)(rng_next(&rng) % 62u);
        int warm = (int)(rng_next(&rng) & 3u);
        V3 dir = v3(sinf(az) * cosf(el), sinf(el), cosf(az) * cosf(el));
        float vx = cy * dir.x - sy * dir.z;
        float vz0 = sy * dir.x + cy * dir.z;
        float py = cp * dir.y - sp * vz0;
        float pz = sp * dir.y + cp * vz0;
        if (pz <= 0.1f) continue;
        int sx = (int)(fb->w * 0.5f + vx * F / pz);
        int sy_px = (int)(fb->h * 0.5f - py * F / pz);
        if (sx < 0 || sx >= fb->w || sy_px < 0 || sy_px >= fb->h) continue;
        int sr = warm == 0 ? lum + 10 : lum - 4;
        int sg = lum;
        int sb = warm == 0 ? lum - 4 : lum + 16;
        fb->px[sy_px * fb->w + sx] = quantize_pixel(fb, sr, sg, sb, sx, sy_px);
    }

    /* Hazy pale moon high to the west over open harbor water */
    V3 mdir = v3_norm(v3(-0.84f, 0.48f, 0.25f));
    float mvx = cy * mdir.x - sy * mdir.z;
    float mvz0 = sy * mdir.x + cy * mdir.z;
    float mpy = cp * mdir.y - sp * mvz0;
    float mpz = sp * mdir.y + cp * mvz0;
    if (mpz > 0.15f) {
        int mx = (int)(fb->w * 0.5f + mvx * F / mpz);
        int my = (int)(fb->h * 0.5f - mpy * F / mpz);
        if (mx > -32 && mx < fb->w + 32 && my > -32 && my < fb->h + 32) {
            for (int yy = my - 22; yy <= my + 22; yy++) {
                if (yy < 0 || yy >= fb->h) continue;
                for (int xx = mx - 22; xx <= mx + 22; xx++) {
                    if (xx < 0 || xx >= fb->w) continue;
                    int dx = xx - mx, dy_m = yy - my;
                    int d2 = dx * dx + dy_m * dy_m;
                    if (d2 <= 36) {
                        int shade = 148 - (dx * 3 + dy_m * 2);
                        fb->px[yy * fb->w + xx] =
                            quantize_pixel(fb, shade, shade + 12, shade + 28, xx, yy);
                    } else if (d2 < 484) {
                        float h = 1.0f - (float)d2 / 484.0f;
                        h *= h;
                        fb->px[yy * fb->w + xx] = blend_ps1(
                            fb, fb->px[yy * fb->w + xx],
                            (int)(20 * h), (int)(28 * h), (int)(46 * h),
                            BLEND_ADD, xx, yy);
                    }
                }
            }
        }
    }
}

void fb_sky(FB *fb) {
    fb_sky_cam(fb, NULL);
}

void fb_clear_depth(FB *fb) {
    /* IEEE-754 0.0f has bit representation 0x00000000: clear via ASM fill */
    ps15_fill32((uint32_t *)fb->zb, 0u, (size_t)(fb->w * fb->h));
    memset(&fb->stats, 0, sizeof fb->stats);
}

float fb_depth_at(const FB *fb, int x, int y) {
    if (x < 0 || x >= fb->w || y < 0 || y >= fb->h) return 1e9f;
    float iz = fb->zb[y * fb->w + x];
    return iz > 1e-7f ? (1.0f / iz) : 1e9f;
}

/* ---------- scanline perspective-correct PS1.5 triangle rasterizer ---- */
typedef struct {
    float x, y, iz, uz, vz, lr, lg, lb;
} RVert;

static const float INV_SPAN[9] = {
    0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.2f, 1.0f / 6.0f, 1.0f / 7.0f, 0.125f
};

static inline int ceil_subpix(float v) {
    return (int)ceilf(v - 0.5f);
}

void raster_tri_ex(FB *fb, SVertEx a, SVertEx b, SVertEx c,
                   const uint32_t *tex, int tw, int th,
                   BlendMode blend, uint32_t flags,
                   float fog, int fog_r, int fog_g, int fog_b) {
    fb->stats.tris_submitted++;
    if (flags & RF_MIRROR_Y) {
        SVertEx tmp = b; b = c; c = tmp;
    }
    float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    /* Front-facing 3D CCW triangles project to area > 0 in screen space. */
    if (fabsf(area) < 1e-4f) { fb->stats.tris_culled++; return; }
    if (area < 0.0f) {
        if (!(flags & RF_DOUBLE_SIDED)) {
            fb->stats.tris_culled++;
            return;
        }
        SVertEx tmp = b; b = c; c = tmp;
        area = -area;
    }

    if (fb->settings.wireframe) {
        raster_line(fb, a.x, a.y, a.z, b.x, b.y, b.z, 0x38d8ff, BLEND_OPAQUE);
        raster_line(fb, b.x, b.y, b.z, c.x, c.y, c.z, 0x38d8ff, BLEND_OPAQUE);
        raster_line(fb, c.x, c.y, c.z, a.x, a.y, a.z, 0x38d8ff, BLEND_OPAQUE);
    }

    bool affine = fb->settings.affine_uv || (flags & RF_AFFINE_UV) != 0;
    float aiz = 1.0f / fmaxf(a.z, 0.05f);
    float biz = 1.0f / fmaxf(b.z, 0.05f);
    float ciz = 1.0f / fmaxf(c.z, 0.05f);

    RVert rv[3] = {
        { a.x, a.y, aiz, affine ? a.u : a.u * aiz, affine ? a.v : a.v * aiz,
          a.lr * 128.0f, a.lg * 128.0f, a.lb * 128.0f },
        { b.x, b.y, biz, affine ? b.u : b.u * biz, affine ? b.v : b.v * biz,
          b.lr * 128.0f, b.lg * 128.0f, b.lb * 128.0f },
        { c.x, c.y, ciz, affine ? c.u : c.u * ciz, affine ? c.v : c.v * ciz,
          c.lr * 128.0f, c.lg * 128.0f, c.lb * 128.0f }
    };

    /* Constant planar gradients across the entire triangle */
    float inv_area = 1.0f / area;
    float dx1 = rv[1].x - rv[0].x, dy1 = rv[1].y - rv[0].y;
    float dx2 = rv[2].x - rv[0].x, dy2 = rv[2].y - rv[0].y;

    #define GRAD_X(f) (((rv[1].f - rv[0].f) * dy2 - (rv[2].f - rv[0].f) * dy1) * inv_area)
    #define GRAD_Y(f) (((rv[2].f - rv[0].f) * dx1 - (rv[1].f - rv[0].f) * dx2) * inv_area)
    float diz_dx = GRAD_X(iz), diz_dy = GRAD_Y(iz);
    float duz_dx = GRAD_X(uz), duz_dy = GRAD_Y(uz);
    float dvz_dx = GRAD_X(vz), dvz_dy = GRAD_Y(vz);
    float dlr_dx = GRAD_X(lr), dlr_dy = GRAD_Y(lr);
    float dlg_dx = GRAD_X(lg), dlg_dy = GRAD_Y(lg);
    float dlb_dx = GRAD_X(lb), dlb_dy = GRAD_Y(lb);
    #undef GRAD_X
    #undef GRAD_Y

    const RVert origin = rv[0];
    /* Sort rv[0].y <= rv[1].y <= rv[2].y for two-half scanline edge walk */
    if (rv[0].y > rv[1].y) { RVert t = rv[0]; rv[0] = rv[1]; rv[1] = t; }
    if (rv[1].y > rv[2].y) { RVert t = rv[1]; rv[1] = rv[2]; rv[2] = t; }
    if (rv[0].y > rv[1].y) { RVert t = rv[0]; rv[0] = rv[1]; rv[1] = t; }
    if (rv[2].y - rv[0].y < 1e-4f) return;

    fb->stats.tris_drawn++;

    int fog_i = (flags & RF_NO_FOG) ? 0 : (int)lrintf(clampf(fog, 0.0f, 1.0f) * 256.0f);
    int inv_fog_i = 256 - fog_i;
    int fogr_p = fog_i * fog_r;
    int fogg_p = fog_i * fog_g;
    int fogb_p = fog_i * fog_b;

    bool can_asm_tex = (tex == NULL && white_tex_ready) ||
                       (tex != NULL && tw == 64 && th == 64);
    bool fast_asm64 = (can_asm_tex && !affine &&
                       fb->settings.dither_mode == DITHER_BAYER555 &&
                       fb->settings.filter_mode == FILTER_POINT);
    const uint32_t *asm_tex = tex ? tex : WHITE_TEX64;
    bool fade_fog = (blend != BLEND_OPAQUE && blend != BLEND_HALF);
    bool pot_tex = (tex != NULL && tw > 0 && th > 0 &&
                    (tw & (tw - 1)) == 0 && (th & (th - 1)) == 0);
    uint32_t tw_mask = pot_tex ? (uint32_t)(tw - 1) : 0;
    uint32_t th_mask = pot_tex ? (uint32_t)(th - 1) : 0;

    float long_dy = rv[2].y - rv[0].y;
    float long_slope = (rv[2].x - rv[0].x) / long_dy;
    /* Determine whether the middle vertex lies to the right or left of v0->v2 */
    float mid_x_on_long = rv[0].x + long_slope * (rv[1].y - rv[0].y);
    bool long_is_left = (mid_x_on_long <= rv[1].x);

    for (int half = 0; half < 2; half++) {
        const RVert *e_top = (half == 0) ? &rv[0] : &rv[1];
        const RVert *e_bot = (half == 0) ? &rv[1] : &rv[2];
        float short_dy = e_bot->y - e_top->y;
        if (short_dy <= 0.0f) continue;

        int y0 = ceil_subpix(e_top->y);
        int y1 = ceil_subpix(e_bot->y);
        if (y0 < 0) y0 = 0;
        if (y1 > fb->h) y1 = fb->h;
        if (y0 >= y1) continue;

        float short_slope = (e_bot->x - e_top->x) / short_dy;

        for (int y = y0; y < y1; y++) {
            float yf = (float)y + 0.5f;
            float xl = rv[0].x + long_slope * (yf - rv[0].y);
            float xs = e_top->x + short_slope * (yf - e_top->y);
            float x_left  = long_is_left ? xl : xs;
            float x_right = long_is_left ? xs : xl;

            int ix0 = ceil_subpix(x_left);
            int ix1 = ceil_subpix(x_right);
            if (ix0 < 0) ix0 = 0;
            if (ix1 > fb->w) ix1 = fb->w;
            if (ix0 >= ix1) continue;

            fb->stats.spans_drawn++;
            float sdx = ((float)ix0 + 0.5f) - origin.x;
            float sdy = yf - origin.y;

            float iz = origin.iz + diz_dx * sdx + diz_dy * sdy;
            float uz = origin.uz + duz_dx * sdx + duz_dy * sdy;
            float vz = origin.vz + dvz_dx * sdx + dvz_dy * sdy;
            float lr = origin.lr + dlr_dx * sdx + dlr_dy * sdy;
            float lg = origin.lg + dlg_dx * sdx + dlg_dy * sdy;
            float lb = origin.lb + dlb_dx * sdx + dlb_dy * sdy;

            if (fast_asm64) {
                Span64 sp;
                sp.row_px = fb->px + (size_t)y * (size_t)fb->w;
                sp.row_zb = fb->zb + (size_t)y * (size_t)fb->w;
                sp.tex = asm_tex;
                sp.dinvz = diz_dx;
                sp.dr = (int32_t)lrintf(dlr_dx * 65536.0f);
                sp.dg = (int32_t)lrintf(dlg_dx * 65536.0f);
                sp.db = (int32_t)lrintf(dlb_dx * 65536.0f);
                sp.inv_fog = inv_fog_i;
                sp.fogr = fade_fog ? 0 : fogr_p;
                sp.fogg = fade_fog ? 0 : fogg_p;
                sp.fogb = fade_fog ? 0 : fogb_p;
                sp.dither[0] = BAYER_BIAS[y & 3][0];
                sp.dither[1] = BAYER_BIAS[y & 3][1];
                sp.dither[2] = BAYER_BIAS[y & 3][2];
                sp.dither[3] = BAYER_BIAS[y & 3][3];

                float z0 = 1.0f / fmaxf(iz, 1e-6f);
                float u0 = tex ? (uz * z0 * 64.0f * 65536.0f) : 0.0f;
                float v0 = tex ? (vz * z0 * 64.0f * 65536.0f) : 0.0f;

                int x = ix0;
                while (x < ix1) {
                    int n = ix1 - x;
                    if (n > 8) n = 8;
                    float iz1 = iz + diz_dx * (float)n;
                    float uz1 = uz + duz_dx * (float)n;
                    float vz1 = vz + dvz_dx * (float)n;
                    float z1 = 1.0f / fmaxf(iz1, 1e-6f);
                    float u1 = tex ? (uz1 * z1 * 64.0f * 65536.0f) : 0.0f;
                    float v1 = tex ? (vz1 * z1 * 64.0f * 65536.0f) : 0.0f;
                    float inv_n = INV_SPAN[n];

                    sp.x = x;
                    sp.count = n;
                    sp.invz = iz;
                    sp.u  = (uint32_t)(int32_t)lrintf(u0);
                    sp.du = (uint32_t)(int32_t)lrintf((u1 - u0) * inv_n);
                    sp.v  = (uint32_t)(int32_t)lrintf(v0);
                    sp.dv = (uint32_t)(int32_t)lrintf((v1 - v0) * inv_n);
                    sp.r  = (int32_t)lrintf(lr * 65536.0f);
                    sp.g  = (int32_t)lrintf(lg * 65536.0f);
                    sp.b  = (int32_t)lrintf(lb * 65536.0f);

                    if (blend == BLEND_OPAQUE)
                        ps15_span_tex64(&sp);
                    else
                        ps15_span_abr64(&sp, (int)blend);

                    iz = iz1; uz = uz1; vz = vz1;
                    u0 = u1; v0 = v1;
                    lr += dlr_dx * (float)n;
                    lg += dlg_dx * (float)n;
                    lb += dlb_dx * (float)n;
                    x += n;
                }
                continue;
            }

            /* General path: semi-transparency ABR modes, untextured Gouraud,
             * non-64x64 textures, affine PS1 UV mode, and alt DAC modes. */
            uint32_t *row_px = fb->px + (size_t)y * (size_t)fb->w;
            float    *row_zb = fb->zb + (size_t)y * (size_t)fb->w;
            bool write_z = (blend == BLEND_OPAQUE);

            for (int x = ix0; x < ix1; x++) {
                if (iz > row_zb[x]) {
                    int tr_c = 255, tg_c = 255, tb_c = 255;
                    bool skip = false;
                    if (tex) {
                        float u, v;
                        if (affine) { u = uz; v = vz; }
                        else {
                            float z = 1.0f / fmaxf(iz, 1e-6f);
                            u = uz * z; v = vz * z;
                        }
                        if (fb->settings.filter_mode == FILTER_BILINEAR && tw > 1 && th > 1) {
                            float fx = u * (float)tw - 0.5f;
                            float fy = v * (float)th - 0.5f;
                            int x0 = (int)floorf(fx);
                            int y0 = (int)floorf(fy);
                            int x_frac = (int)((fx - (float)x0) * 256.0f);
                            int y_frac = (int)((fy - (float)y0) * 256.0f);
                            int x1 = x0 + 1;
                            int y1 = y0 + 1;
                            if (pot_tex) {
                                x0 &= tw_mask; x1 &= tw_mask;
                                y0 &= th_mask; y1 &= th_mask;
                            } else {
                                x0 = (x0 % tw + tw) % tw; x1 = (x1 % tw + tw) % tw;
                                y0 = (y0 % th + th) % th; y1 = (y1 % th + th) % th;
                            }
                            uint32_t c00 = tex[y0 * tw + x0];
                            uint32_t c10 = tex[y0 * tw + x1];
                            uint32_t c01 = tex[y1 * tw + x0];
                            uint32_t c11 = tex[y1 * tw + x1];

                            if ((c00 | c10 | c01 | c11) & TEX_MASK_BIT) {
                                int tx = (int)floorf(u * (float)tw) % tw; if (tx < 0) tx += tw;
                                int ty = (int)floorf(v * (float)th) % th; if (ty < 0) ty += th;
                                uint32_t t = tex[ty * tw + tx];
                                if (t & TEX_MASK_BIT) skip = true;
                                tr_c = (int)((t >> 16) & 255u);
                                tg_c = (int)((t >> 8)  & 255u);
                                tb_c = (int)( t        & 255u);
                            } else {
                                int inv_xf = 256 - x_frac, inv_yf = 256 - y_frac;
                                int r0 = (((c00 >> 16) & 255) * inv_xf + ((c10 >> 16) & 255) * x_frac) >> 8;
                                int g0 = (((c00 >>  8) & 255) * inv_xf + ((c10 >>  8) & 255) * x_frac) >> 8;
                                int b0 = (( c00        & 255) * inv_xf + ( c10        & 255) * x_frac) >> 8;

                                int r1 = (((c01 >> 16) & 255) * inv_xf + ((c11 >> 16) & 255) * x_frac) >> 8;
                                int g1 = (((c01 >>  8) & 255) * inv_xf + ((c11 >>  8) & 255) * x_frac) >> 8;
                                int b1 = (( c01        & 255) * inv_xf + ( c11        & 255) * x_frac) >> 8;

                                tr_c = (r0 * inv_yf + r1 * y_frac) >> 8;
                                tg_c = (g0 * inv_yf + g1 * y_frac) >> 8;
                                tb_c = (b0 * inv_yf + b1 * y_frac) >> 8;
                            }
                        } else {
                            int tx, ty;
                            if (pot_tex) {
                                tx = (int)((uint32_t)(int32_t)floorf(u * (float)tw) & tw_mask);
                                ty = (int)((uint32_t)(int32_t)floorf(v * (float)th) & th_mask);
                            } else {
                                tx = (int)floorf(u * (float)tw) % tw; if (tx < 0) tx += tw;
                                ty = (int)floorf(v * (float)th) % th; if (ty < 0) ty += th;
                            }
                            uint32_t t = tex[ty * tw + tx];
                            if (t & TEX_MASK_BIT) skip = true;
                            tr_c = (int)((t >> 16) & 255u);
                            tg_c = (int)((t >> 8)  & 255u);
                            tb_c = (int)( t        & 255u);
                        }
                    }
                    if (!skip) {
                        if (write_z) row_zb[x] = iz;
                        int rl = (int)lr; if (rl < 0) rl = 0;
                        int gl = (int)lg; if (gl < 0) gl = 0;
                        int bl = (int)lb; if (bl < 0) bl = 0;
                        int cr = (tr_c * rl) >> 7; if (cr > 255) cr = 255;
                        int cg = (tg_c * gl) >> 7; if (cg > 255) cg = 255;
                        int cb = (tb_c * bl) >> 7; if (cb > 255) cb = 255;
                        if (blend == BLEND_OPAQUE || blend == BLEND_HALF) {
                            cr = (cr * inv_fog_i + fogr_p) >> 8;
                            cg = (cg * inv_fog_i + fogg_p) >> 8;
                            cb = (cb * inv_fog_i + fogb_p) >> 8;
                        } else {
                            /* Additive / subtractive effects fade out with fog */
                            cr = (cr * inv_fog_i) >> 8;
                            cg = (cg * inv_fog_i) >> 8;
                            cb = (cb * inv_fog_i) >> 8;
                        }
                        row_px[x] = blend_ps1(fb, row_px[x], cr, cg, cb, blend, x, y);
                    }
                }
                iz += diz_dx; uz += duz_dx; vz += dvz_dx;
                lr += dlr_dx; lg += dlg_dx; lb += dlb_dx;
            }
        }
    }
}

void raster_tri(FB *fb, SVert a, SVert b, SVert c,
                const uint32_t *tex, int tw, int th,
                float tr, float tg, float tb, float fogz) {
    float fog = clampf(fogz / 70.0f, 0.0f, 0.78f);
    SVertEx ea = { a.x, a.y, a.z, a.u, a.v, a.light * tr, a.light * tg, a.light * tb };
    SVertEx eb = { b.x, b.y, b.z, b.u, b.v, b.light * tr, b.light * tg, b.light * tb };
    SVertEx ec = { c.x, c.y, c.z, c.u, c.v, c.light * tr, c.light * tg, c.light * tb };
    raster_tri_ex(fb, ea, eb, ec, tex, tw, th, BLEND_OPAQUE, RF_DOUBLE_SIDED,
                  fog, 28, 42, 68);
}

/* ---------- depth-tested 2D/3D line, glow sprites & HUD font ---------- */
void raster_line(FB *fb, float x0, float y0, float z0,
                 float x1, float y1, float z1,
                 uint32_t col, BlendMode blend) {
    float dx = x1 - x0, dy = y1 - y0;
    float steps = fmaxf(fabsf(dx), fabsf(dy));
    if (steps < 0.5f) return;
    if (steps > 2000.0f) steps = 2000.0f;
    int n = (int)ceilf(steps);
    float iz0 = 1.0f / fmaxf(z0, 0.1f);
    float iz1 = 1.0f / fmaxf(z1, 0.1f);
    int cr = (int)((col >> 16) & 255u);
    int cg = (int)((col >> 8)  & 255u);
    int cb = (int)( col        & 255u);
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        int x = (int)lrintf(x0 + dx * t);
        int y = (int)lrintf(y0 + dy * t);
        if (x < 0 || x >= fb->w || y < 0 || y >= fb->h) continue;
        float iz = lerpf(iz0, iz1, t);
        int idx = y * fb->w + x;
        if (iz + 0.002f >= fb->zb[idx]) {
            if (blend == BLEND_OPAQUE) fb->zb[idx] = iz;
            fb->px[idx] = blend_ps1(fb, fb->px[idx], cr, cg, cb, blend, x, y);
        }
    }
}

void raster_glow_z(FB *fb, int cx, int cy, int radius, float depth_z,
                   int r, int g, int b) {
    if (radius <= 0) return;
    float lamp_iz = (depth_z > 0.1f) ? (1.0f / (depth_z - 0.35f)) : 1e6f;
    float inv_r2 = 1.0f / (float)(radius * radius);
    int y0 = cy - radius; if (y0 < 0) y0 = 0;
    int y1 = cy + radius; if (y1 >= fb->h) y1 = fb->h - 1;
    int x0 = cx - radius; if (x0 < 0) x0 = 0;
    int x1 = cx + radius; if (x1 >= fb->w) x1 = fb->w - 1;

    for (int y = y0; y <= y1; y++) {
        int dy2 = (y - cy) * (y - cy);
        for (int x = x0; x <= x1; x++) {
            int d2 = (x - cx) * (x - cx) + dy2;
            float t = 1.0f - (float)d2 * inv_r2;
            if (t <= 0.0f) continue;
            int idx = y * fb->w + x;
            /* Occlude halo behind closer geometry while blooming over far background */
            if (fb->zb[idx] > lamp_iz) continue;
            float fall = t * t * (0.35f + 0.65f * t);
            float core = (t > 0.72f) ? ((t - 0.72f) * 3.57f) : 0.0f;
            core *= core * 48.0f;
            fb->px[idx] = blend_ps1(fb, fb->px[idx],
                                    (int)(r * fall + core),
                                    (int)(g * fall + core * 0.85f),
                                    (int)(b * fall + core * 0.55f),
                                    BLEND_ADD, x, y);
        }
    }
}

void raster_glow(FB *fb, int cx, int cy, int radius, int r, int g, int b) {
    raster_glow_z(fb, cx, cy, radius, 1e6f, r, g, b);
}

void raster_rect(FB *fb, int x, int y, int w, int h, uint32_t col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > fb->w) ? fb->w : (x + w);
    int y1 = (y + h > fb->h) ? fb->h : (y + h);
    if (x0 >= x1 || y0 >= y1) return;
    size_t span = (size_t)(x1 - x0);
    for (int yy = y0; yy < y1; yy++)
        ps15_fill32(fb->px + (size_t)yy * (size_t)fb->w + (size_t)x0, col, span);
}

void raster_bar(FB *fb, int x, int y, int w, int h, float frac, uint32_t col) {
    raster_rect(fb, x - 1, y - 1, w + 2, h + 2, 0x080a10);
    raster_rect(fb, x, y, w, h, 0x161a26);
    int fw = (int)((float)w * clampf(frac, 0.0f, 1.0f));
    if (fw > 0) raster_rect(fb, x, y, fw, h, col);
}

/* Crisp 5x5 tactical arcade/PS1 bitmap font (25-bit packed rows + drop shadow) */
#define G5(r0, r1, r2, r3, r4) \
    (((uint32_t)(r0) << 20) | ((uint32_t)(r1) << 15) | ((uint32_t)(r2) << 10) | \
     ((uint32_t)(r3) << 5)  |  (uint32_t)(r4))

static uint32_t glyph_bits(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case '0': return G5(0x0E, 0x13, 0x15, 0x19, 0x0E);
        case '1': return G5(0x04, 0x0C, 0x04, 0x04, 0x0E);
        case '2': return G5(0x1E, 0x01, 0x0E, 0x10, 0x1F);
        case '3': return G5(0x1E, 0x01, 0x0E, 0x01, 0x1E);
        case '4': return G5(0x12, 0x12, 0x1F, 0x02, 0x02);
        case '5': return G5(0x1F, 0x10, 0x1E, 0x01, 0x1E);
        case '6': return G5(0x0F, 0x10, 0x1E, 0x11, 0x0E);
        case '7': return G5(0x1F, 0x01, 0x02, 0x04, 0x04);
        case '8': return G5(0x0E, 0x11, 0x0E, 0x11, 0x0E);
        case '9': return G5(0x0E, 0x11, 0x0F, 0x01, 0x1E);
        case 'A': return G5(0x0E, 0x11, 0x1F, 0x11, 0x11);
        case 'B': return G5(0x1E, 0x11, 0x1E, 0x11, 0x1E);
        case 'C': return G5(0x0F, 0x10, 0x10, 0x10, 0x0F);
        case 'D': return G5(0x1E, 0x11, 0x11, 0x11, 0x1E);
        case 'E': return G5(0x1F, 0x10, 0x1E, 0x10, 0x1F);
        case 'F': return G5(0x1F, 0x10, 0x1E, 0x10, 0x10);
        case 'G': return G5(0x0F, 0x10, 0x13, 0x11, 0x0F);
        case 'H': return G5(0x11, 0x11, 0x1F, 0x11, 0x11);
        case 'I': return G5(0x0E, 0x04, 0x04, 0x04, 0x0E);
        case 'J': return G5(0x07, 0x02, 0x02, 0x12, 0x0C);
        case 'K': return G5(0x11, 0x12, 0x1C, 0x12, 0x11);
        case 'L': return G5(0x10, 0x10, 0x10, 0x10, 0x1F);
        case 'M': return G5(0x11, 0x1B, 0x15, 0x11, 0x11);
        case 'N': return G5(0x11, 0x19, 0x15, 0x13, 0x11);
        case 'O': return G5(0x0E, 0x11, 0x11, 0x11, 0x0E);
        case 'P': return G5(0x1E, 0x11, 0x1E, 0x10, 0x10);
        case 'Q': return G5(0x0E, 0x11, 0x15, 0x12, 0x0D);
        case 'R': return G5(0x1E, 0x11, 0x1E, 0x12, 0x11);
        case 'S': return G5(0x0F, 0x10, 0x0E, 0x01, 0x1E);
        case 'T': return G5(0x1F, 0x04, 0x04, 0x04, 0x04);
        case 'U': return G5(0x11, 0x11, 0x11, 0x11, 0x0E);
        case 'V': return G5(0x11, 0x11, 0x11, 0x0A, 0x04);
        case 'W': return G5(0x11, 0x11, 0x15, 0x1B, 0x11);
        case 'X': return G5(0x11, 0x0A, 0x04, 0x0A, 0x11);
        case 'Y': return G5(0x11, 0x0A, 0x04, 0x04, 0x04);
        case 'Z': return G5(0x1F, 0x02, 0x04, 0x08, 0x1F);
        case ':': return G5(0x00, 0x04, 0x00, 0x04, 0x00);
        case '.': return G5(0x00, 0x00, 0x00, 0x00, 0x04);
        case '-': return G5(0x00, 0x00, 0x0E, 0x00, 0x00);
        case '+': return G5(0x00, 0x04, 0x0E, 0x04, 0x00);
        case '/': return G5(0x01, 0x02, 0x04, 0x08, 0x10);
        case '%': return G5(0x19, 0x1A, 0x04, 0x0B, 0x13);
        case '[': return G5(0x0E, 0x08, 0x08, 0x08, 0x0E);
        case ']': return G5(0x0E, 0x02, 0x02, 0x02, 0x0E);
        case '!': return G5(0x04, 0x04, 0x04, 0x00, 0x04);
        case '(': return G5(0x02, 0x04, 0x04, 0x04, 0x02);
        case ')': return G5(0x08, 0x04, 0x04, 0x04, 0x08);
        default:  return 0;
    }
}
#undef G5

void raster_text(FB *fb, int x, int y, const char *str, uint32_t col) {
    if (!str) return;
    int cx = x;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\n') { cx = x; y += 7; continue; }
        uint32_t g = glyph_bits(str[i]);
        if (g) {
            for (int row = 0; row < 5; row++) {
                int bits = (int)((g >> ((4 - row) * 5)) & 0x1Fu);
                for (int cb = 0; cb < 5; cb++) {
                    if (bits & (16 >> cb)) {
                        int px = cx + cb, py = y + row;
                        if (px + 1 >= 0 && px + 1 < fb->w && py + 1 >= 0 && py + 1 < fb->h)
                            fb->px[(py + 1) * fb->w + (px + 1)] = 0x06080eu;
                        if (px >= 0 && px < fb->w && py >= 0 && py < fb->h)
                            fb->px[py * fb->w + px] = col;
                    }
                }
            }
        }
        cx += 6;
    }
}

void raster_number(FB *fb, int x, int y, int value, uint32_t col) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", value);
    raster_text(fb, x, y, buf, col);
}

void raster_line2d(FB *fb, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 1024; guard++) {
        if (x0 >= 0 && x0 < fb->w && y0 >= 0 && y0 < fb->h)
            fb->px[y0 * fb->w + x0] = col;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Crisp 7-segment digital instrument display (ammo & OP CLOCK) */
void raster_7seg(FB *fb, int x, int y, int scale, const char *str, uint32_t col) {
    static const uint8_t SEG[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    if (!str || scale < 1) return;
    int w = 5 * scale, h = 9 * scale, th = (scale > 1) ? 2 : 1;
    int cx = x;
    for (int i = 0; str[i]; i++) {
        char ch = str[i];
        if (ch == ':') {
            raster_rect(fb, cx + 1, y + h / 3, th, th, col);
            raster_rect(fb, cx + 1, y + (h * 2) / 3, th, th, col);
            cx += 3 * scale;
            continue;
        }
        if (ch == ' ') { cx += w + 2 * scale; continue; }
        if (ch >= '0' && ch <= '9') {
            uint8_t m = SEG[ch - '0'];
            int mh = h / 2;
            /* Dim unlit ghost segments for authentic 1987 amber LCD/LED look */
            uint32_t ghost = 0x261a08u;
            raster_rect(fb, cx + th, y, w - 2 * th, th, (m & 0x01) ? col : ghost);
            raster_rect(fb, cx + w - th, y + th, th, mh - th, (m & 0x02) ? col : ghost);
            raster_rect(fb, cx + w - th, y + mh + 1, th, mh - th, (m & 0x04) ? col : ghost);
            raster_rect(fb, cx + th, y + h - th, w - 2 * th, th, (m & 0x08) ? col : ghost);
            raster_rect(fb, cx, y + mh + 1, th, mh - th, (m & 0x10) ? col : ghost);
            raster_rect(fb, cx, y + th, th, mh - th, (m & 0x20) ? col : ghost);
            raster_rect(fb, cx + th, y + mh, w - 2 * th, th, (m & 0x40) ? col : ghost);
        }
        cx += w + 2 * scale;
    }
}

/* Diegetic DeBrief Amber SIGINT Visor HUD (compass strip, light-reactive body
 * silhouette pictogram, 7-segment ammo & curved mag, OP CLOCK, SIG bars) */
void hud_draw_visor(FB *fb, float yaw, float op_time_s, int ammo,
                    float light_vis, float alert, float sig_level, bool grappling) {
    const uint32_t amb_hi  = 0xffbe3cu;
    const uint32_t amb_mid = 0xd69428u;
    const uint32_t amb_dim = 0x6e4a14u;
    int hw = fb->w / 2;

    /* 1. Top Horizontal Compass Strip + Cardinal Markers + Center Pointer */
    int comp_w = 220, cx0 = hw - comp_w / 2, cy = 14;
    raster_rect(fb, cx0, cy, comp_w, 1, amb_mid);
    raster_rect(fb, cx0, cy, 1, 5, amb_hi);
    raster_rect(fb, cx0 + comp_w - 1, cy, 1, 5, amb_hi);
    /* Upward amber center pointer triangle */
    raster_rect(fb, hw, cy - 4, 1, 1, amb_hi);
    raster_rect(fb, hw - 1, cy - 3, 3, 1, amb_hi);
    raster_rect(fb, hw - 2, cy - 2, 5, 2, amb_hi);

    float deg_center = yaw * (180.0f / PS15_PI);
    int base_deg = ((int)floorf(deg_center / 15.0f)) * 15;
    for (int d = base_deg - 90; d <= base_deg + 90; d += 15) {
        float off = ((float)d - deg_center) * 1.25f;
        int tx = hw + (int)lrintf(off);
        if (tx <= cx0 + 3 || tx >= cx0 + comp_w - 3) continue;
        int nd = ((d % 360) + 360) % 360;
        bool major = (nd % 45 == 0);
        raster_rect(fb, tx, cy + 1, 1, major ? 5 : 3, major ? amb_hi : amb_dim);
        if (nd == 0)   raster_text(fb, tx - 2, cy + 7, "N", amb_hi);
        if (nd == 90)  raster_text(fb, tx - 2, cy + 7, "E", amb_mid);
        if (nd == 180) raster_text(fb, tx - 2, cy + 7, "S", amb_mid);
        if (nd == 270) raster_text(fb, tx - 2, cy + 7, "W", amb_mid);
    }

    /* 2. Bottom-Left: Light-Reactive Segmented Body Pictogram + 7-Seg Ammo */
    int bx = 24, by = fb->h - 58;
    float lv = clampf(light_vis, 0.0f, 1.0f);
    uint32_t body_col = (alert > 0.60f) ? 0xff5a36u
                      : (lv > 0.55f)    ? 0xffd45cu
                      : (lv > 0.28f)    ? amb_mid : 0x8a5e1cu;
    /* Head */
    raster_rect(fb, bx - 3, by, 6, 1, body_col);
    raster_rect(fb, bx - 3, by + 6, 6, 1, body_col);
    raster_rect(fb, bx - 3, by, 1, 7, body_col);
    raster_rect(fb, bx + 2, by, 1, 7, body_col);
    /* Shoulders & Torso */
    raster_rect(fb, bx - 7, by + 8, 14, 1, body_col);
    raster_rect(fb, bx - 5, by + 8, 1, 13, body_col);
    raster_rect(fb, bx + 4, by + 8, 1, 13, body_col);
    raster_rect(fb, bx - 5, by + 20, 10, 1, body_col);
    /* Arms */
    raster_line2d(fb, bx - 7, by + 8, bx - 10, by + 22, body_col);
    raster_line2d(fb, bx + 6, by + 8, bx + 9,  by + 22, body_col);
    /* Legs & Boots */
    raster_line2d(fb, bx - 4, by + 21, bx - 5, by + 42, body_col);
    raster_line2d(fb, bx - 1, by + 21, bx - 2, by + 42, body_col);
    raster_line2d(fb, bx + 1, by + 21, bx + 2, by + 42, body_col);
    raster_line2d(fb, bx + 3, by + 21, bx + 4, by + 42, body_col);
    raster_rect(fb, bx - 6, by + 42, 4, 2, body_col);
    raster_rect(fb, bx + 2, by + 42, 4, 2, body_col);

    /* 7-Segment Ammo Counter + Curved Magazine Icon */
    char abuf[8];
    snprintf(abuf, sizeof abuf, "%d", ammo < 0 ? 0 : ammo);
    int ax = (ammo >= 10) ? 44 : 52;
    raster_7seg(fb, ax, fb->h - 34, 2, abuf, amb_hi);
    int mx = 74, my = fb->h - 34;
    for (int row = 0; row < 18; row++) {
        int curve = (row * row) / 55;
        raster_rect(fb, mx + curve, my + row, 1, 1, amb_mid);
        raster_rect(fb, mx + curve + 7, my + row, 1, 1, amb_mid);
        if (row % 4 == 2)
            raster_rect(fb, mx + curve + 2, my + row, 4, 1,
                        (ammo > (4 - row / 4) * 2) ? amb_hi : amb_dim);
    }
    raster_rect(fb, mx, my, 8, 1, amb_mid);
    raster_rect(fb, mx + 5, my + 17, 8, 1, amb_mid);
    /* Bottom-left instrument bracket */
    raster_rect(fb, 12, fb->h - 11, 78, 1, amb_mid);
    raster_rect(fb, 12, fb->h - 15, 1, 4, amb_mid);
    raster_rect(fb, 89, fb->h - 15, 1, 4, amb_mid);
    if (grappling) raster_text(fb, 44, fb->h - 45, "HOOK", 0x74e8a8u);

    /* 3. Bottom-Right: OP CLOCK 7-Seg Timer + SIG 12-Bar Spectrum Meter */
    int rx = fb->w - 94;
    raster_text(fb, rx, fb->h - 54, "OP CLOCK", amb_mid);
    int tot = (int)fmaxf(0.0f, op_time_s);
    int mm = (tot / 60) % 100, ss = tot % 60;
    char tbuf[12];
    snprintf(tbuf, sizeof tbuf, "%02d:%02d", mm, ss);
    raster_7seg(fb, rx, fb->h - 45, 2, tbuf, amb_hi);

    raster_text(fb, rx, fb->h - 21, "SIG", amb_mid);
    float sig = clampf(fmaxf(sig_level, light_vis * 0.85f), 0.05f, 1.0f);
    for (int b = 0; b < 12; b++) {
        float wave = 0.45f + 0.55f * fabsf(sinf(op_time_s * 3.1f + (float)b * 0.75f));
        int bh = 2 + (int)lrintf(11.0f * sig * wave);
        if (bh > 12) bh = 12;
        uint32_t bc = (b >= 9 && sig > 0.72f) ? 0xff6238u : amb_hi;
        raster_rect(fb, rx + 22 + b * 5, fb->h - 13 - bh, 3, bh, bc);
    }
    raster_rect(fb, rx - 4, fb->h - 11, 86, 1, amb_mid);
    raster_rect(fb, rx - 4, fb->h - 15, 1, 4, amb_mid);
    raster_rect(fb, rx + 81, fb->h - 15, 1, 4, amb_mid);
}

/* Crimson Tactical Optic Visor post-filter (DeBrief night-sensor mode) */
void fb_apply_visor(FB *fb, float time_s) {
    int scan_row = ((int)(time_s * 45.0f)) % fb->h;
    if (scan_row < 0) scan_row += fb->h;
    int hw = fb->w / 2, hh = fb->h / 2;

    for (int y = 0; y < fb->h; y++) {
        uint32_t *row = fb->px + (size_t)y * (size_t)fb->w;
        int scan = (y & 1) ? -14 : 6;
        if (abs(y - scan_row) <= 1) scan += 22;
        int dy = (y - hh);
        int dy2 = dy * dy * 3;
        for (int x = 0; x < fb->w; x++) {
            uint32_t c = row[x];
            int r = (int)((c >> 16) & 255u);
            int g = (int)((c >> 8)  & 255u);
            int b = (int)( c        & 255u);
            /* Boost dark shadow gain + map into crimson/ruby phosphor palette */
            int lum = (r * 77 + g * 150 + b * 29) >> 8;
            int lift = lum + ((255 - lum) * lum) / 210 + 18 + scan;
            int dx = x - hw;
            int vig = (dx * dx + dy2) >> 11;
            lift -= vig;
            if (lift < 8) lift = 8;
            int vr = (lift * 235) >> 8;
            int vg = (lift * 48  + (lum > 150 ? (lum - 150) * 140 : 0)) >> 8;
            int vb = (lift * 68  + (lum > 165 ? (lum - 165) * 90  : 0)) >> 8;
            row[x] = quantize_pixel(fb, vr, vg, vb, x, y);
        }
    }
    /* Tactical optic corner framing brackets */
    uint32_t brk = 0xe83848u;
    raster_rect(fb, 14, 24, 18, 1, brk); raster_rect(fb, 14, 24, 1, 14, brk);
    raster_rect(fb, fb->w - 32, 24, 18, 1, brk); raster_rect(fb, fb->w - 15, 24, 1, 14, brk);
    raster_rect(fb, 14, fb->h - 26, 18, 1, brk); raster_rect(fb, 14, fb->h - 39, 1, 14, brk);
    raster_rect(fb, fb->w - 32, fb->h - 26, 18, 1, brk); raster_rect(fb, fb->w - 15, fb->h - 39, 1, 14, brk);
}

/* Separable 2-pass bloom filter: downsamples bright highlights to half-res (240x135),
 * applies a separable 5-tap Gaussian blur, and additively composites onto fb */
void fb_bloom(FB *fb, float threshold, float intensity) {
    if (!fb || fb->w < 2 || fb->h < 2) return;
    int hw = fb->w / 2, hh = fb->h / 2;
    int thresh_lum = (int)lrintf(clampf(threshold, 0.0f, 1.0f) * 255.0f);
    float gain = fmaxf(0.0f, intensity);

    uint32_t *bright = malloc(sizeof(uint32_t) * (size_t)(hw * hh));
    uint32_t *blurred = malloc(sizeof(uint32_t) * (size_t)(hw * hh));
    if (!bright || !blurred) {
        free(bright);
        free(blurred);
        return;
    }

    /* Pass 1: Extract bright pixels downsampled by 2x2 box average */
    for (int hy = 0; hy < hh; hy++) {
        int y0 = hy * 2, y1 = y0 + 1;
        const uint32_t *r0 = fb->px + (size_t)y0 * (size_t)fb->w;
        const uint32_t *r1 = fb->px + (size_t)y1 * (size_t)fb->w;
        uint32_t *dst = bright + (size_t)hy * (size_t)hw;
        for (int hx = 0; hx < hw; hx++) {
            int x0 = hx * 2, x1 = x0 + 1;
            uint32_t p00 = r0[x0], p01 = r0[x1];
            uint32_t p10 = r1[x0], p11 = r1[x1];

            int r = (((p00 >> 16) & 255) + ((p01 >> 16) & 255) +
                     ((p10 >> 16) & 255) + ((p11 >> 16) & 255)) >> 2;
            int g = (((p00 >> 8)  & 255) + ((p01 >> 8)  & 255) +
                     ((p10 >> 8)  & 255) + ((p11 >> 8)  & 255)) >> 2;
            int b = (( p00        & 255) + ( p01        & 255) +
                     ( p10        & 255) + ( p11        & 255)) >> 2;

            int lum = (r * 77 + g * 150 + b * 29) >> 8;
            if (lum > thresh_lum) {
                int excess = lum - thresh_lum;
                int factor = (excess * 256) / (256 - thresh_lum + 1);
                r = (r * factor) >> 8;
                g = (g * factor) >> 8;
                b = (b * factor) >> 8;
                dst[hx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else {
                dst[hx] = 0;
            }
        }
    }

    /* Pass 2: Separable Horizontal 5-tap Gaussian Blur [1, 4, 6, 4, 1] / 16 */
    for (int y = 0; y < hh; y++) {
        const uint32_t *src = bright + (size_t)y * (size_t)hw;
        uint32_t *dst = blurred + (size_t)y * (size_t)hw;
        for (int x = 0; x < hw; x++) {
            int x_m2 = (x >= 2) ? x - 2 : 0;
            int x_m1 = (x >= 1) ? x - 1 : 0;
            int x_p1 = (x + 1 < hw) ? x + 1 : hw - 1;
            int x_p2 = (x + 2 < hw) ? x + 2 : hw - 1;

            uint32_t c0 = src[x_m2], c1 = src[x_m1], c2 = src[x];
            uint32_t c3 = src[x_p1], c4 = src[x_p2];

            int r = ((int)((c0 >> 16) & 255) * 1 +
                     (int)((c1 >> 16) & 255) * 4 +
                     (int)((c2 >> 16) & 255) * 6 +
                     (int)((c3 >> 16) & 255) * 4 +
                     (int)((c4 >> 16) & 255) * 1) >> 4;
            int g = ((int)((c0 >> 8)  & 255) * 1 +
                     (int)((c1 >> 8)  & 255) * 4 +
                     (int)((c2 >> 8)  & 255) * 6 +
                     (int)((c3 >> 8)  & 255) * 4 +
                     (int)((c4 >> 8)  & 255) * 1) >> 4;
            int b = ((int)( c0        & 255) * 1 +
                     (int)( c1        & 255) * 4 +
                     (int)( c2        & 255) * 6 +
                     (int)( c3        & 255) * 4 +
                     (int)( c4        & 255) * 1) >> 4;

            dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* Pass 3: Separable Vertical 5-tap Gaussian Blur [1, 4, 6, 4, 1] / 16 into bright */
    for (int y = 0; y < hh; y++) {
        int y_m2 = (y >= 2) ? y - 2 : 0;
        int y_m1 = (y >= 1) ? y - 1 : 0;
        int y_p1 = (y + 1 < hh) ? y + 1 : hh - 1;
        int y_p2 = (y + 2 < hh) ? y + 2 : hh - 1;

        const uint32_t *r0 = blurred + (size_t)y_m2 * (size_t)hw;
        const uint32_t *r1 = blurred + (size_t)y_m1 * (size_t)hw;
        const uint32_t *r2 = blurred + (size_t)y    * (size_t)hw;
        const uint32_t *r3 = blurred + (size_t)y_p1 * (size_t)hw;
        const uint32_t *r4 = blurred + (size_t)y_p2 * (size_t)hw;
        uint32_t *dst = bright + (size_t)y * (size_t)hw;

        for (int x = 0; x < hw; x++) {
            uint32_t c0 = r0[x], c1 = r1[x], c2 = r2[x], c3 = r3[x], c4 = r4[x];

            int r = ((int)((c0 >> 16) & 255) * 1 +
                     (int)((c1 >> 16) & 255) * 4 +
                     (int)((c2 >> 16) & 255) * 6 +
                     (int)((c3 >> 16) & 255) * 4 +
                     (int)((c4 >> 16) & 255) * 1) >> 4;
            int g = ((int)((c0 >> 8)  & 255) * 1 +
                     (int)((c1 >> 8)  & 255) * 4 +
                     (int)((c2 >> 8)  & 255) * 6 +
                     (int)((c3 >> 8)  & 255) * 4 +
                     (int)((c4 >> 8)  & 255) * 1) >> 4;
            int b = ((int)( c0        & 255) * 1 +
                     (int)( c1        & 255) * 4 +
                     (int)( c2        & 255) * 6 +
                     (int)( c3        & 255) * 4 +
                     (int)( c4        & 255) * 1) >> 4;

            dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* Pass 4: Additive composite onto full-res fb */
    int igain = (int)lrintf(gain * 256.0f);
    for (int y = 0; y < fb->h; y++) {
        uint32_t *drow = fb->px + (size_t)y * (size_t)fb->w;
        int hy = y >> 1;
        if (hy >= hh) hy = hh - 1;
        const uint32_t *srow = bright + (size_t)hy * (size_t)hw;
        for (int x = 0; x < fb->w; x++) {
            int hx = x >> 1;
            if (hx >= hw) hx = hw - 1;
            uint32_t bc = srow[hx];
            if (!bc) continue;
            int br = (int)((bc >> 16) & 255u);
            int bg = (int)((bc >> 8)  & 255u);
            int bb = (int)( bc        & 255u);

            uint32_t orig = drow[x];
            int or_ = (int)((orig >> 16) & 255u) + ((br * igain) >> 8);
            int og  = (int)((orig >> 8)  & 255u) + ((bg * igain) >> 8);
            int ob  = (int)( orig        & 255u) + ((bb * igain) >> 8);

            drow[x] = quantize_pixel(fb, or_, og, ob, x, y);
        }
    }

    free(bright);
    free(blurred);
}

/* Cinematic Depth of Field (DoF): keeps objects around focus_dist sharp,
 * and applies a soft progressive optical blur to out-of-focus background/foreground */
void fb_dof(FB *fb, float focus_dist, float focus_range, float max_blur) {
    if (!fb || fb->w < 4 || fb->h < 4 || max_blur <= 0.01f) return;
    int w = fb->w, h = fb->h;
    uint32_t *blurred = malloc(sizeof(uint32_t) * (size_t)(w * h));
    if (!blurred) return;

    /* Pass 1: 5-tap horizontal blur */
    for (int y = 0; y < h; y++) {
        const uint32_t *src = fb->px + (size_t)y * (size_t)w;
        uint32_t *dst = blurred + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            int x_m2 = (x >= 2) ? x - 2 : 0;
            int x_m1 = (x >= 1) ? x - 1 : 0;
            int x_p1 = (x + 1 < w) ? x + 1 : w - 1;
            int x_p2 = (x + 2 < w) ? x + 2 : w - 1;

            uint32_t c0 = src[x_m2], c1 = src[x_m1], c2 = src[x];
            uint32_t c3 = src[x_p1], c4 = src[x_p2];

            int r = (((c0 >> 16) & 255) + ((c1 >> 16) & 255) * 2 +
                     ((c2 >> 16) & 255) * 4 + ((c3 >> 16) & 255) * 2 +
                     ((c4 >> 16) & 255)) >> 3;
            int g = (((c0 >>  8) & 255) + ((c1 >>  8) & 255) * 2 +
                     ((c2 >>  8) & 255) * 4 + ((c3 >>  8) & 255) * 2 +
                     ((c4 >>  8) & 255)) >> 3;
            int b = (( c0        & 255) + ( c1        & 255) * 2 +
                     ( c2        & 255) * 4 + ( c3        & 255) * 2 +
                     ( c4        & 255)) >> 3;

            dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* Pass 2: Vertical blur & depth-weighted blend back into fb */
    for (int y = 0; y < h; y++) {
        int y_m2 = (y >= 2) ? y - 2 : 0;
        int y_m1 = (y >= 1) ? y - 1 : 0;
        int y_p1 = (y + 1 < h) ? y + 1 : h - 1;
        int y_p2 = (y + 2 < h) ? y + 2 : h - 1;

        const uint32_t *r0 = blurred + (size_t)y_m2 * (size_t)w;
        const uint32_t *r1 = blurred + (size_t)y_m1 * (size_t)w;
        const uint32_t *r2 = blurred + (size_t)y    * (size_t)w;
        const uint32_t *r3 = blurred + (size_t)y_p1 * (size_t)w;
        const uint32_t *r4 = blurred + (size_t)y_p2 * (size_t)w;

        uint32_t *drow = fb->px + (size_t)y * (size_t)w;
        const float *zrow = fb->zb + (size_t)y * (size_t)w;

        for (int x = 0; x < w; x++) {
            float iz = zrow[x];
            float z = (iz > 1e-6f) ? (1.0f / iz) : 1000.0f;
            float d = fabsf(z - focus_dist);
            float coc = 0.0f;
            if (d > focus_range) {
                coc = clampf((d - focus_range) / max_blur, 0.0f, 0.82f);
            }
            if (coc <= 0.01f) continue;

            uint32_t c0 = r0[x], c1 = r1[x], c2 = r2[x], c3 = r3[x], c4 = r4[x];
            int br = (((c0 >> 16) & 255) + ((c1 >> 16) & 255) * 2 +
                      ((c2 >> 16) & 255) * 4 + ((c3 >> 16) & 255) * 2 +
                      ((c4 >> 16) & 255)) >> 3;
            int bg = (((c0 >>  8) & 255) + ((c1 >>  8) & 255) * 2 +
                      ((c2 >>  8) & 255) * 4 + ((c3 >>  8) & 255) * 2 +
                      ((c4 >>  8) & 255)) >> 3;
            int bb = (( c0        & 255) + ( c1        & 255) * 2 +
                      ( c2        & 255) * 4 + ( c3        & 255) * 2 +
                      ( c4        & 255)) >> 3;

            uint32_t orig = drow[x];
            int or_ = (int)((orig >> 16) & 255);
            int og  = (int)((orig >>  8) & 255);
            int ob  = (int)( orig        & 255);

            int fr = (int)lerpf((float)or_, (float)br, coc);
            int fg = (int)lerpf((float)og, (float)bg, coc);
            int fb_b = (int)lerpf((float)ob, (float)bb, coc);

            drow[x] = quantize_pixel(fb, fr, fg, fb_b, x, y);
        }
    }

    free(blurred);
}

uint32_t *fb_upscale2x(const FB *fb) {
    uint32_t *out = malloc(sizeof(uint32_t) * (size_t)(fb->w * 2) * (size_t)(fb->h * 2));
    if (fb->settings.crt_scanlines)
        ps15_upscale2x_crt(out, fb->px, fb->w, fb->h);
    else
        ps15_upscale2x(out, fb->px, fb->w, fb->h);
    return out;
}

uint32_t *fb_upscale2x_crt(const FB *fb) {
    uint32_t *out = malloc(sizeof(uint32_t) * (size_t)(fb->w * 2) * (size_t)(fb->h * 2));
    ps15_upscale2x_crt(out, fb->px, fb->w, fb->h);
    return out;
}
