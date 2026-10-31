/* CASM-PS15 trial: a from-scratch software renderer in C.
 * 480x270 framebuffer, vertex snapping, flat shading, RGB555 quantization,
 * depth fog. Scene: ground, two warehouse blocks, a watchtower — the
 * banner's palette: deep blue-grey shadow, sodium amber light pools. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "engine.h"

#define W 480
#define H 270

static uint32_t fb[W * H];
static fix16    zb[W * H];

#ifndef CASM_HAVE_ASM_FILL
void ps15_fill32(uint32_t *dst, uint32_t value, size_t count) {
    for (size_t i = 0; i < count; i++) dst[i] = value;
}
#endif

static void zb_clear(void) {
    /* depth buffer is fix16; clear through the same fill path with a
     * far-plane pattern (fill is 32-bit, depth values are 32-bit). */
    ps15_fill32((uint32_t *)zb, 0x7fffffffu, (size_t)(W * H));
}

/* RGB555 quantization — the PS1.5 color step. */
static uint32_t quantize(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    r = (r >> 3) << 3; g = (g >> 3) << 3; b = (b >> 3) << 3;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

typedef struct { float x, y, z; } V3;

static int tris_drawn;

/* Project with vertex snapping to the low-res grid (subtle PS1 wobble). */
static void tri(V3 a, V3 b, V3 c, float camz, float yaw,
                int r, int g, int bl) {
    V3 p[3] = { a, b, c };
    int sx[3], sy[3]; float sz[3];
    float cy = cosf(yaw), syaw = sinf(yaw);
    for (int i = 0; i < 3; i++) {
        float x = p[i].x * cy - p[i].z * syaw;
        float z = p[i].x * syaw + p[i].z * cy + camz;
        if (z < 0.5f) return;
        float f = 190.0f / z;
        int px = (int)lrintf(W * 0.5f + x * f);
        int py = (int)lrintf(H * 0.62f - p[i].y * f);
        px = (px / 2) * 2;            /* snap: half-pixel grid */
        py = (py / 2) * 2;
        sx[i] = px; sy[i] = py; sz[i] = z;
    }
    /* flat light from face normal (y-ish faces read lit, walls shadow) */
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float ny = fabsf(uz * vx - ux * vz);
    float shade = 0.45f + 0.55f * (ny / (sqrtf(ux*ux+uy*uy+uz*uz) *
                                        sqrtf(vx*vx+vy*vy+vz*vz) + 1e-6f));
    /* depth fog toward blue-grey */
    float zavg = (sz[0] + sz[1] + sz[2]) / 3.0f;
    float fog = zavg / 60.0f; if (fog > 0.75f) fog = 0.75f;
    int rr = (int)((r * shade) * (1 - fog) + 38 * fog);
    int gg = (int)((g * shade) * (1 - fog) + 48 * fog);
    int bb = (int)((bl * shade) * (1 - fog) + 66 * fog);
    uint32_t col = quantize(rr, gg, bb);

    int minx = sx[0], maxx = sx[0], miny = sy[0], maxy = sy[0];
    for (int i = 1; i < 3; i++) {
        if (sx[i] < minx) minx = sx[i];
        if (sx[i] > maxx) maxx = sx[i];
        if (sy[i] < miny) miny = sy[i];
        if (sy[i] > maxy) maxy = sy[i];
    }
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= W) maxx = W - 1;
    if (maxy >= H) maxy = H - 1;
    float d = (float)((sy[1] - sy[2]) * (sx[0] - sx[2]) +
                      (sx[2] - sx[1]) * (sy[0] - sy[2]));
    if (d == 0) return;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float w0 = ((sy[1] - sy[2]) * (x - sx[2]) + (sx[2] - sx[1]) * (y - sy[2])) / d;
            float w1 = ((sy[2] - sy[0]) * (x - sx[2]) + (sx[0] - sx[2]) * (y - sy[2])) / d;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            fix16 zf = (fix16)(z * FIX_ONE);
            int idx = y * W + x;
            if (zf < zb[idx]) { zb[idx] = zf; fb[idx] = col; }
        }
    }
    tris_drawn++;
}

static void box(float x, float y, float z, float sx, float sy, float sz,
                float camz, float yaw, int r, int g, int b) {
    V3 v[8];
    for (int i = 0; i < 8; i++)
        v[i] = (V3){ x + ((i & 1) ? sx : 0), y + ((i & 2) ? sy : 0),
                     z + ((i & 4) ? sz : 0) };
    int f[6][4] = { {0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3} };
    for (int i = 0; i < 6; i++) {
        tri(v[f[i][0]], v[f[i][1]], v[f[i][2]], camz, yaw, r, g, b);
        tri(v[f[i][0]], v[f[i][2]], v[f[i][3]], camz, yaw, r, g, b);
    }
}

static void render(float yaw) {
    ps15_fill32(fb, quantize(16, 22, 34), (size_t)(W * H));  /* night sky */
    zb_clear();
    tris_drawn = 0;
    float camz = 0.0f;
    /* ground plane, wet dark concrete — scene sits ahead of the camera */
    tri((V3){-40,0, 8}, (V3){40,0, 8}, (V3){40,0, 70}, camz, yaw, 52, 58, 68);
    tri((V3){-40,0, 8}, (V3){40,0, 70}, (V3){-40,0, 70}, camz, yaw, 52, 58, 68);
    /* warehouses */
    box(-9, 0, 22, 10, 5, 8, camz, yaw, 88, 84, 78);
    box( 4, 0, 28, 12, 7, 9, camz, yaw, 76, 74, 72);
    /* watchtower + sodium lamp head */
    box(-2.5f, 0, 34, 1.2f, 11, 1.2f, camz, yaw, 60, 60, 64);
    box(-3.4f, 11, 33.1f, 3, 0.6f, 3, camz, yaw, 232, 148, 52);
}

static int write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char px[3] = { (unsigned char)(fb[i] >> 16),
                                (unsigned char)(fb[i] >> 8),
                                (unsigned char)fb[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int main(void) {
    const int frames = 120;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < frames; i++)
        render(-0.12f + 0.002f * i);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                (t1.tv_nsec - t0.tv_nsec) / 1e6;
    double per = ms / frames;
    if (write_ppm("frame.ppm") != 0) { perror("frame.ppm"); return 1; }
    printf("CASM-PS15 trial — C + x86_64 assembly fill\n");
    printf("framebuffer: %dx%d software, RGB555, vertex-snapped\n", W, H);
    printf("frames: %d  tris/frame: %d\n", frames, tris_drawn);
    printf("mean: %.2f ms/frame  (%.0f fps)\n", per, 1000.0 / per);
    printf("wrote frame.ppm\n");
    return 0;
}
