#ifndef CASM_ENGINE_H
#define CASM_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================= CASM-PS15 — C + assembly PS1.5 engine =================
 * From scratch, zero dependencies beyond libc/libm.
 * C owns the engine; assembly owns hot inner loops only. */

/* ---------- assembly hot loops (fill_x86_64.s; C fallback per file) --- */
void ps15_fill32(uint32_t *dst, uint32_t value, size_t count);

/* ---------- math ---------- */
typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static inline V3 v3_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3_scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3_cross(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(V3 a) { return sqrtf(v3_dot(a, a)); }
static inline V3 v3_norm(V3 a) { float l = v3_len(a); return l > 1e-6f ? v3_scale(a, 1.0f / l) : v3(0, 1, 0); }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* deterministic RNG (SplitMix64) — same sequence on every machine */
typedef struct { uint64_t s; } Rng;
static inline uint32_t rng_next(Rng *r) {
    uint64_t z = (r->s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return (uint32_t)((z ^ (z >> 31)) >> 32) ^ (uint32_t)(z ^ (z >> 31));
}
static inline float rng_f01(Rng *r) { return (rng_next(r) >> 8) * (1.0f / 16777216.0f); }

/* ---------- framebuffer / rasterizer ---------- */
#define FB_W 480
#define FB_H 270
typedef struct {
    int w, h;
    uint32_t *px;   /* 0xRRGGBB */
    float *zb;
} FB;
FB *fb_new(int w, int h);
void fb_free(FB *fb);
void fb_sky(FB *fb);                       /* night gradient + stars */
void fb_clear_depth(FB *fb);
uint32_t rgb555(int r, int g, int b, int x, int y); /* quantize + Bayer dither */

typedef struct { float x, y, z, u, v, light; } SVert;  /* screen-space */
void raster_tri(FB *fb, SVert a, SVert b, SVert c,
                const uint32_t *tex, int tw, int th,
                float tr, float tg, float tb, float fogz);
void raster_glow(FB *fb, int cx, int cy, int radius, int r, int g, int b);
void raster_rect(FB *fb, int x, int y, int w, int h, uint32_t col);
void raster_bar(FB *fb, int x, int y, int w, int h, float frac, uint32_t col);
void raster_number(FB *fb, int x, int y, int value, uint32_t col); /* 3x5 digits */
uint32_t *fb_upscale2x(const FB *fb);      /* caller frees; 960x540 out */

/* ---------- textures (procedural, painted detail at 64x64) ---------- */
enum { TEX_BRICK, TEX_CONCRETE, TEX_METAL, TEX_CAMO, TEX_FACE, TEX_COUNT };
typedef struct { int w, h; uint32_t *px; } Tex;
void texgen_all(Tex out[TEX_COUNT]);
void texgen_free(Tex t[TEX_COUNT]);

/* ---------- meshes ---------- */
typedef struct { V3 p; float u, v; V3 n; } MVert;
typedef struct { int a, b, c; } MTri;
typedef struct { MVert *verts; int nv; MTri *tris; int nt; } Mesh;
Mesh mesh_box(float sx, float sy, float sz);
Mesh mesh_plane(float sx, float sz, float tile);
void mesh_free(Mesh *m);

/* humanoid built from part meshes, posed by the animator */
enum { PART_HIPS, PART_TORSO, PART_HEAD, PART_ARM_L, PART_ARM_R,
       PART_LEG_L, PART_LEG_R, PART_COUNT };
typedef struct { Mesh parts[PART_COUNT]; V3 pivot[PART_COUNT]; } Humanoid;
Humanoid humanoid_new(void);
void humanoid_free(Humanoid *h);

/* ---------- renderer ---------- */
typedef struct { V3 pos; float yaw, pitch; } Camera;
typedef struct { V3 pos; float range; float r, g, b; } PointLight; /* sodium */
typedef struct {
    const PointLight *lights; int nlights;
    float amb_r, amb_g, amb_b;
} LightRig;
void render_mesh(FB *fb, const Camera *cam, const Mesh *m,
                 V3 pos, float yaw, const Tex *tex,
                 float tr, float tg, float tb, const LightRig *rig);

/* pose: joint angles in radians */
typedef struct {
    float swing;      /* arm/leg swing phase amplitude result */
    float arm_l, arm_r, leg_l, leg_r, knee_l, knee_r, lean, head_yaw;
} Pose;
void render_humanoid(FB *fb, const Camera *cam, const Humanoid *h,
                     V3 pos, float yaw, const Pose *pose,
                     const Tex *texset, float tr, float tg, float tb,
                     const LightRig *rig);

/* ---------- animation ---------- */
typedef struct { float t; Pose pose; } Keyframe;
typedef struct { const Keyframe *keys; int nkeys; float dur; } Clip;
extern const Clip CLIP_IDLE, CLIP_WALK, CLIP_RUN;
Pose anim_sample(const Clip *clip, float t);
Pose anim_blend(Pose a, Pose b, float w);

/* ---------- physics ---------- */
typedef struct { V3 min, max; } AABB;
typedef struct {
    V3 pos, vel;          /* pos = feet centre */
    float radius, height;
    bool grounded, grappling;
    V3 grapple_anchor; float grapple_len;
} Body;
void physics_step(Body *b, const AABB *world, int n, float dt);
void grapple_fire(Body *b, V3 anchor);
void grapple_release(Body *b);
float world_ground_y(const AABB *world, int n, V3 p); /* top under p, or 0 */

/* ---------- audio (synth mixer; renders PCM, writes WAV) ---------- */
#define AU_RATE 22050
typedef struct { float *buf; int len, cap; } Mixer;
void mixer_init(Mixer *m, int seconds);
void mixer_free(Mixer *m);
void au_tone(Mixer *m, float at, float dur, float f0, float f1, float gain);
void au_noise(Mixer *m, float at, float dur, float gain, uint32_t seed);
void au_footstep(Mixer *m, float at, bool wet);
void au_grapple(Mixer *m, float at);
void au_hum(Mixer *m, float dur);                 /* sodium lamp hum bed */
void au_positional(float dist, float *gain, float *pan); /* pan unused in mono mix, reported */
int  au_write_wav(const char *path, const Mixer *m);

/* ---------- perception (stealth AI) ---------- */
typedef struct {
    V3 pos; float yaw;
    float alert;          /* 0 calm .. 1 alarmed */
    int waypoint; float wait;
} Guard;
bool los_clear(V3 a, V3 b, const AABB *world, int n);   /* XZ segment test */
void guard_update(Guard *g, const V3 *waypoints, int nw, V3 player,
                  float player_noise, const AABB *world, int n, float dt);

/* ---------- asset loading (assets.c) ---------- */
Mesh mesh_load_obj(const char *path);          /* empty mesh on failure */
int tex_load_ppm(const char *path, Tex *out);  /* 0 ok, -1 failure; P3+P6 */

/* ---------- live audio (streaming synth for device backends) ---------- */
#define AU_MAX_VOICES 32
typedef struct {
    int active, kind;              /* kind: 0 tone, 1 noise */
    float t, dur, f0, f1, gain, phase, lp;
    uint32_t rng;
} LiveVoice;
typedef struct {
    LiveVoice voices[AU_MAX_VOICES];
    float time;
    int hum_on;
} AudioLive;
void au_live_init(AudioLive *a);
void au_live_tone(AudioLive *a, float dur, float f0, float f1, float gain);
void au_live_noise(AudioLive *a, float dur, float gain, uint32_t seed);
void au_live_footstep(AudioLive *a, int wet);
void au_live_grapple(AudioLive *a);
void au_live_render(AudioLive *a, float *out, int n); /* mono, AU_RATE */

/* camera collision for the third-person rig */
V3 camera_resolve(V3 cam, const AABB *world, int n, float radius);

/* ---------- persistence ---------- */
typedef struct {
    uint32_t magic, version;
    V3 player_pos; float alert; uint32_t frames; uint32_t checksum;
} SaveState;
int save_write(const char *path, const SaveState *s);
int save_read(const char *path, SaveState *s);   /* 0 ok, -1 corrupt/missing */

#endif
