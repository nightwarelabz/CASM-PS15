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
 * C owns the engine; assembly owns hot inner loops only, with bit-identical
 * portable C fallbacks so every machine renders the exact same frames. */

/* ---------- assembly hot loops (fill_x86_64.s / fill_arm64.S + C ref) -- */
typedef struct {
    uint32_t       *row_px;     /* offset 0:  &fb->px[y * fb->w] */
    float          *row_zb;     /* offset 8:  &fb->zb[y * fb->w] (1/z) */
    const uint32_t *tex;        /* offset 16: 64x64 texels */
    int32_t         x;          /* offset 24: start x */
    int32_t         count;      /* offset 28: span pixel count (> 0) */
    float           invz;       /* offset 32: 1/z at x */
    float           dinvz;      /* offset 36: d(1/z)/dx */
    uint32_t        u;          /* offset 40: 16.16 fixed-point texel u */
    uint32_t        du;         /* offset 44: du/dx (16.16) */
    uint32_t        v;          /* offset 48: 16.16 fixed-point texel v */
    uint32_t        dv;         /* offset 52: dv/dx (16.16) */
    int32_t         r;          /* offset 56: 8.16 Gouraud R (128<<16 = 1x) */
    int32_t         dr;         /* offset 60: dr/dx */
    int32_t         g;          /* offset 64: 8.16 Gouraud G */
    int32_t         dg;         /* offset 68: dg/dx */
    int32_t         b;          /* offset 72: 8.16 Gouraud B */
    int32_t         db;         /* offset 76: db/dx */
    int32_t         inv_fog;    /* offset 80: 256 - fog (0..256) */
    int32_t         fogr;       /* offset 84: fog * fog_col_r */
    int32_t         fogg;       /* offset 88: fog * fog_col_g */
    int32_t         fogb;       /* offset 92: fog * fog_col_b */
    int32_t         dither[4];  /* offset 96: Bayer signed bias for (x & 3) */
} Span64;

void ps15_fill32(uint32_t *dst, uint32_t value, size_t count);
void ps15_upscale2x(uint32_t *dst, const uint32_t *src, int w, int h);
void ps15_upscale2x_crt(uint32_t *dst, const uint32_t *src, int w, int h);
void ps15_span_tex64(const Span64 *s);
void ps15_span_abr64(const Span64 *s, int abr_mode);

/* portable C reference kernels (always compiled for differential testing) */
void ps15_c_fill32(uint32_t *dst, uint32_t value, size_t count);
void ps15_c_upscale2x(uint32_t *dst, const uint32_t *src, int w, int h);
void ps15_c_upscale2x_crt(uint32_t *dst, const uint32_t *src, int w, int h);
void ps15_c_span_tex64(const Span64 *s);
void ps15_c_span_abr64(const Span64 *s, int abr_mode);

/* ---------- math ---------- */
#define PS15_PI 3.14159265358979323846f
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
static inline V3 v3_norm(V3 a) {
    float l = v3_len(a);
    return l > 1e-6f ? v3_scale(a, 1.0f / l) : v3(0, 1, 0);
}
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
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

/* ---------- PS1.5 hardware blend, dither & snap modes ---------- */
typedef enum {
    BLEND_OPAQUE  = 0,  /* opaque z-write */
    BLEND_HALF    = 1,  /* PS1 ABR 0: 0.5*B + 0.5*F */
    BLEND_ADD     = 2,  /* PS1 ABR 1: 1.0*B + 1.0*F (volumetric beams, glow) */
    BLEND_SUB     = 3,  /* PS1 ABR 2: 1.0*B - 1.0*F (contact drop shadows) */
    BLEND_QUARTER = 4   /* PS1 ABR 3: 1.0*B + 0.25*F (rain, wet reflections) */
} BlendMode;

typedef enum {
    SNAP_PS15 = 0,      /* subtle 0.5-px grid snap (PS1.5 default) */
    SNAP_PS1  = 1,      /* coarse 1.5-px grid wobble (classic PS1) */
    SNAP_OFF  = 2       /* floating-point subpixel projection */
} SnapMode;

typedef enum {
    DITHER_BAYER555 = 0, /* 15-bit RGB555 + 4x4 Bayer ordered dither */
    DITHER_FLAT555  = 1, /* 15-bit RGB555 without dither */
    DITHER_RGB888   = 2  /* 24-bit unquantized reference */
} DitherMode;

typedef enum {
    FILTER_POINT    = 0, /* PS1 nearest-neighbor point sampling */
    FILTER_BILINEAR = 1  /* PS2 Emotion Engine GS bilinear texture filtering */
} TexFilterMode;

typedef enum {
    GFX_PS1  = 0, /* 15-bit RGB555, Bayer dither, 1.5px snap, affine UV, point-filtered */
    GFX_PS15 = 1, /* 15-bit RGB555, Bayer dither, 0.5px snap, perspective UV, point-filtered */
    GFX_PS2  = 2  /* 24-bit TrueColor, 0px subpixel, perspective UV, bilinear-filtered, bloom, DoF */
} GraphicProfile;

enum {
    RF_NONE         = 0,
    RF_DOUBLE_SIDED = 1u << 0,  /* disable backface culling */
    RF_UNLIT        = 1u << 1,  /* emissive surface (windows, optics, beams) */
    RF_NO_FOG       = 1u << 2,  /* bypass atmospheric fog */
    RF_AFFINE_UV    = 1u << 3,  /* force classic PS1 affine UV interpolation */
    RF_MIRROR_Y     = 1u << 4,  /* planar mirror flip (inverts winding) */
    RF_WET_SPEC     = 1u << 5,  /* view-dependent Blinn-Phong wet/metallic specular */
    RF_RIM_LIT      = 1u << 6   /* grazing Fresnel silhouette rim lighting */
};

typedef struct {
    SnapMode      snap_mode;
    DitherMode    dither_mode;
    TexFilterMode filter_mode;     /* PS1 point vs PS2 bilinear texture filtering */
    GraphicProfile profile;        /* active generational graphics profile */
    bool          affine_uv;       /* global toggle for PS1 vs PS1.5 comparison */
    bool          wireframe;       /* debug wireframe overlay */
    bool          crt_scanlines;   /* Trinitron 25% odd-row phosphor scanlines */
    bool          visor_mode;      /* Crimson tactical night-optic visor filter */
    float         fov_scale;       /* focal-length multiplier (1.0 default, >1 zoom/portrait) */
} RenderSettings;

typedef struct {
    int tris_submitted;
    int tris_culled;
    int tris_drawn;
    int spans_drawn;
} RenderStats;

/* ---------- framebuffer / rasterizer ---------- */
#define FB_W 480
#define FB_H 270
#define TEX_MASK_BIT 0x80000000u  /* PS1 transparent texel mask bit */

typedef struct { V3 pos; float yaw, pitch; } Camera;

typedef struct {
    int w, h;
    uint32_t *px;               /* 0xRRGGBB */
    float    *zb;               /* stores 1/z (0.0f = far infinity) */
    RenderSettings settings;
    RenderStats    stats;
} FB;

FB *fb_new(int w, int h);
void fb_free(FB *fb);
void fb_sky(FB *fb);
void fb_sky_cam(FB *fb, const Camera *cam); /* 3D star dome + moon + horizon */
void fb_clear_depth(FB *fb);
float fb_depth_at(const FB *fb, int x, int y); /* linear z at pixel (1e9 far) */
uint32_t rgb555(int r, int g, int b, int x, int y);
uint32_t quantize_pixel(const FB *fb, int r, int g, int b, int x, int y);

typedef struct { float x, y, z, u, v, light; } SVert;
typedef struct {
    float x, y, z, u, v;
    float lr, lg, lb;           /* per-vertex RGB Gouraud light (1.0 = neutral) */
} SVertEx;

void raster_tri(FB *fb, SVert a, SVert b, SVert c,
                const uint32_t *tex, int tw, int th,
                float tr, float tg, float tb, float fogz);
void raster_tri_ex(FB *fb, SVertEx a, SVertEx b, SVertEx c,
                   const uint32_t *tex, int tw, int th,
                   BlendMode blend, uint32_t flags,
                   float fog, int fog_r, int fog_g, int fog_b);
void raster_line(FB *fb, float x0, float y0, float z0,
                 float x1, float y1, float z1,
                 uint32_t col, BlendMode blend);
void raster_glow(FB *fb, int cx, int cy, int radius, int r, int g, int b);
void raster_glow_z(FB *fb, int cx, int cy, int radius, float depth_z,
                   int r, int g, int b);
void raster_rect(FB *fb, int x, int y, int w, int h, uint32_t col);
void raster_line2d(FB *fb, int x0, int y0, int x1, int y1, uint32_t col);
void raster_bar(FB *fb, int x, int y, int w, int h, float frac, uint32_t col);
void raster_number(FB *fb, int x, int y, int value, uint32_t col);
void raster_text(FB *fb, int x, int y, const char *str, uint32_t col);
void raster_7seg(FB *fb, int x, int y, int scale, const char *str, uint32_t col);
void hud_draw_visor(FB *fb, float yaw, float op_time_s, int ammo,
                    float light_vis, float alert, float sig_level, bool grappling);
void fb_set_profile(FB *fb, GraphicProfile profile);
void fb_apply_visor(FB *fb, float time_s);
void fb_bloom(FB *fb, float threshold, float intensity);
void fb_dof(FB *fb, float focus_dist, float focus_range, float max_blur);
uint32_t *fb_upscale2x(const FB *fb);
uint32_t *fb_upscale2x_crt(const FB *fb);

/* ---------- textures (procedural painted 64x64 + file PPM) ---------- */
enum {
    TEX_BRICK,
    TEX_CONCRETE,
    TEX_METAL,
    TEX_CAMO,
    TEX_FACE,
    TEX_VEST,
    TEX_WINDOW,
    TEX_HAZARD,
    TEX_GRATE,
    TEX_SHADOW,
    TEX_PATCH,      /* 64x64 dedicated gold-bordered NSA eagle shoulder crest */
    TEX_WEAPON,     /* 64x64 brushed steel slide, polymer grip & baffles */
    TEX_WATER,      /* 64x64 tileable harbor canal waves & specular ripples */
    TEX_BEAM,       /* 64x64 smooth volumetric searchlight & muzzle falloff */
    TEX_TERMINAL,   /* 64x64 classified espionage CRT data terminal face */
    TEX_INSIGNIA,   /* 64x64 Regime 3-bar banner/armband, trooper mask & mecha plate */
    TEX_COUNT
};
typedef struct { int w, h; uint32_t *px; } Tex;
void texgen_all(Tex out[TEX_COUNT]);
void texgen_free(Tex t[TEX_COUNT]);

/* ---------- meshes ---------- */
typedef struct { V3 p; float u, v; V3 n; } MVert;
typedef struct { int a, b, c; } MTri;
typedef struct {
    MVert *verts; int nv;
    MTri  *tris;  int nt;
    float  radius;              /* bounding-sphere radius from mesh origin */
    V3     center;              /* local bounding center */
} Mesh;

Mesh mesh_box(float sx, float sy, float sz);
Mesh mesh_box_tiled(float sx, float sy, float sz, float tile_m, float subdiv_m);
Mesh mesh_plane(float sx, float sz, float tile);
Mesh mesh_plane_subdiv(float sx, float sz, float tile, int divs);
Mesh mesh_cone(float r_top, float r_bot, float h, int segs, bool caps);
Mesh mesh_quad_xz(float sx, float sz);
Mesh mesh_grapple_claw(void);
void mesh_compute_bounds(Mesh *m);
void mesh_smooth_normals(Mesh *m, float crease_angle_rad);
void mesh_free(Mesh *m);

/* sculpted 11-part articulated tactical humanoid + first-person viewmodel */
enum {
    PART_HIPS,
    PART_TORSO,
    PART_HEAD,
    PART_ARM_L,
    PART_ARM_R,
    PART_FOREARM_L,
    PART_FOREARM_R,
    PART_LEG_L,
    PART_LEG_R,
    PART_SHIN_L,
    PART_SHIN_R,
    PART_COUNT
};
typedef struct {
    Mesh parts[PART_COUNT];
    V3   pivot[PART_COUNT];
    int  part_tex[PART_COUNT];  /* index into Tex[TEX_COUNT] */
    bool is_guard;
} Humanoid;

Humanoid humanoid_new(void);        /* tactical espionage operator (DeBrief style) */
Humanoid humanoid_guard_new(void);  /* armed Regime patrol trooper */
void humanoid_free(Humanoid *h);

/* Towering bipedal Regime Patrol Walker (Mecha Type-1) */
typedef struct {
    Mesh pelvis, torso, head, arm_l, arm_r, leg_l, leg_r, shin_l, shin_r;
} MechaMesh;
MechaMesh mecha_mesh_new(void);
void mecha_mesh_free(MechaMesh *m);

typedef struct {
    V3    pos;
    float yaw;
    float anim_t;
    int   waypoint;
    float wait;
    float alert;        /* 0 patrol .. 1 hunting */
    float sensor_hp;    /* 0..1 (head crimson sensor slit) */
    float actuator_hp;  /* 0..1 (leg hydraulic actuators) */
    float coolant_hp;   /* 0..1 (exposed rear power/coolant spine) */
    bool  step_event;   /* pulsed true on each heavy walker footfall */
} Mecha;

typedef struct {
    Mesh sleeve_l, sleeve_r, glove_l, glove_r, pistol_frame, pistol_slide;
} Viewmodel;
Viewmodel viewmodel_new(void);
void viewmodel_free(Viewmodel *vm);

/* ---------- renderer ---------- */
typedef struct { V3 pos; float range; float r, g, b; } PointLight;
typedef struct {
    V3 pos, dir;
    float range, cos_inner, cos_outer;
    float r, g, b;
} SpotLight;
typedef struct {
    const PointLight *lights; int nlights;
    float amb_r, amb_g, amb_b;
    const SpotLight *spots; int nspots;
    float fog_r, fog_g, fog_b, fog_dist;
} LightRig;

void render_mesh(FB *fb, const Camera *cam, const Mesh *m,
                 V3 pos, float yaw, const Tex *tex,
                 float tr, float tg, float tb, const LightRig *rig);
void render_mesh_ex(FB *fb, const Camera *cam, const Mesh *m,
                    V3 pos, float yaw, float pitch, float scale,
                    const Tex *tex, float tr, float tg, float tb,
                    const LightRig *rig, BlendMode blend, uint32_t flags);
void render_mesh_mirror(FB *fb, const Camera *cam, const Mesh *m,
                        V3 pos, float yaw, float water_y, float ripple_t,
                        const Tex *tex, float tr, float tg, float tb,
                        const LightRig *rig, uint32_t flags);
void render_water_plane(FB *fb, const Camera *cam, const Mesh *m,
                        V3 pos, const Tex *water_tex, float time_s,
                        float tr, float tg, float tb, const LightRig *rig,
                        BlendMode blend);
float light_exposure_at(const LightRig *rig, V3 wp);

/* pose: joint angles in radians */
typedef struct {
    float swing;
    float arm_l, arm_r, leg_l, leg_r, knee_l, knee_r, lean, head_yaw;
    float elbow_l, elbow_r, crouch, aim;
    float head_pitch, torso_yaw;
} Pose;

/* deterministic 3D particle pool (sparks, shattered lamps, muzzle gas, steam) */
#define PS15_MAX_PARTICLES 96
typedef struct {
    V3       pos, vel;
    float    life, max_life;
    float    gravity;
    uint32_t col;
    bool     glow;
} Particle;

typedef struct {
    Particle items[PS15_MAX_PARTICLES];
    Rng      rng;
} ParticlePool;

void particles_init(ParticlePool *pp, uint64_t seed);
void particles_spawn_burst(ParticlePool *pp, V3 pos, V3 dir_bias, int count,
                           float speed, float gravity, float life,
                           uint32_t col, bool glow);
void particles_update(ParticlePool *pp, float dt);
void particles_render(FB *fb, const Camera *cam, const ParticlePool *pp);

void render_humanoid(FB *fb, const Camera *cam, const Humanoid *h,
                     V3 pos, float yaw, const Pose *pose,
                     const Tex *texset, float tr, float tg, float tb,
                     const LightRig *rig);
void render_mecha(FB *fb, const Camera *cam, const MechaMesh *mm,
                  const Mecha *mc, const Tex *texset, const LightRig *rig);
bool render_project(const FB *fb, const Camera *cam, V3 wp,
                    int *sx, int *sy, float *out_z);
void render_viewmodel(FB *fb, const Camera *cam, const Viewmodel *vm,
                      const Tex *texset, const LightRig *rig,
                      float bob_phase, float bob_amp, float pitch_sway,
                      bool crouching, bool grappling);
void render_viewmodel_ex(FB *fb, const Camera *cam, const Viewmodel *vm,
                         const Tex *texset, const LightRig *rig,
                         float bob_phase, float bob_amp, float pitch_sway,
                         bool crouching, bool grappling, float recoil);
void render_blob_shadow(FB *fb, const Camera *cam, const Mesh *quad,
                        const Tex *shadow_tex, V3 pos, float radius);
void render_line3d(FB *fb, const Camera *cam, V3 a, V3 b,
                   uint32_t col, BlendMode blend);
void render_glow3d(FB *fb, const Camera *cam, V3 pos, float world_radius,
                   int r, int g, int b);
void render_rain(FB *fb, const Camera *cam, float time_s, int count);

/* ---------- animation ---------- */
typedef struct { float t; Pose pose; } Keyframe;
typedef struct { const Keyframe *keys; int nkeys; float dur; } Clip;
extern const Clip CLIP_IDLE, CLIP_WALK, CLIP_RUN, CLIP_CROUCH, CLIP_AIM;
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
bool physics_wallrun_step(Body *b, V3 *out_normal, float *wall_timer,
                          const AABB *world, int n, float dt);
bool physics_mantle_step(Body *b, V3 move_dir, const AABB *world, int n, float dt);
bool physics_can_stand(const Body *b, const AABB *world, int n, float stand_h);
void grapple_fire(Body *b, V3 anchor);
bool grapple_fire_ray(Body *b, V3 eye, V3 aim_dir,
                      const AABB *world, int nworld,
                      const V3 *anchors, int nanchors, float max_dist);
void grapple_release(Body *b);
float world_ground_y(const AABB *world, int n, V3 p);
V3 camera_resolve(V3 cam, const AABB *world, int n, float radius);
Camera tps_camera_step(Camera prev, const Body *b, float yaw, float pitch,
                       bool crouching, bool aiming,
                       const AABB *world, int nworld, float dt);

/* ---------- audio (synth mixer; renders PCM, writes WAV) ---------- */
#define AU_RATE 22050
typedef struct { float *buf; int len, cap; } Mixer;
void mixer_init(Mixer *m, int seconds);
void mixer_free(Mixer *m);
void au_tone(Mixer *m, float at, float dur, float f0, float f1, float gain);
void au_noise(Mixer *m, float at, float dur, float gain, uint32_t seed);
void au_footstep(Mixer *m, float at, bool wet);
void au_footstep_ex(Mixer *m, float at, bool wet, float gain_scale);
void au_grapple(Mixer *m, float at);
void au_pistol_shot(Mixer *m, float at);
void au_glass_break(Mixer *m, float at);
void au_visor_toggle(Mixer *m, float at);
void au_terminal_hack(Mixer *m, float at);
void au_mecha_step(Mixer *m, float at, float gain_scale);
void au_hum(Mixer *m, float dur);
void au_positional(float dist, float *gain, float *pan);
int  au_write_wav(const char *path, const Mixer *m);

/* ---------- perception (stealth AI & 3D hitscan raycasts) ---------- */
typedef struct {
    V3 pos; float yaw;
    float alert;          /* 0 calm .. 1 alarmed */
    int waypoint; float wait;
    float stunned;        /* >0 tranquilized / stunned countdown (seconds) */
    V3    investigate;    /* acoustic distraction target when luring */
    float inv_timer;      /* >0 investigating distraction */
} Guard;
bool  los_clear(V3 a, V3 b, const AABB *world, int n);
float ray_hit_aabb(V3 orig, V3 dir, float max_d, const AABB *box);
float ray_hit_sphere(V3 orig, V3 dir, float max_d, V3 center, float radius);
void  guard_update(Guard *g, const V3 *waypoints, int nw, V3 player,
                   float player_noise, const AABB *world, int n, float dt);
void  guard_update_ex(Guard *g, const V3 *waypoints, int nw, V3 player,
                      float player_noise, float player_vis,
                      const AABB *world, int n, float dt);
void  guard_hear_ping(Guard *g, V3 ping_pos, float max_range);
void  mecha_init(Mecha *m, V3 pos, float yaw);
void  mecha_update(Mecha *m, const V3 *waypoints, int nw, V3 player,
                   float player_noise, float player_vis,
                   const AABB *world, int n, float dt);

/* ---------- asset loading (assets.c) ---------- */
Mesh mesh_load_obj(const char *path);
int  tex_load_ppm(const char *path, Tex *out);

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
    uint32_t next_slot;
} AudioLive;
void au_live_init(AudioLive *a);
void au_live_tone(AudioLive *a, float dur, float f0, float f1, float gain);
void au_live_noise(AudioLive *a, float dur, float gain, uint32_t seed);
void au_live_footstep(AudioLive *a, int wet);
void au_live_grapple(AudioLive *a);
void au_live_pistol(AudioLive *a);
void au_live_glass(AudioLive *a);
void au_live_visor(AudioLive *a);
void au_live_hack(AudioLive *a);
void au_live_mecha_step(AudioLive *a, float gain_scale);
void au_live_render(AudioLive *a, float *out, int n);

/* ---------- persistence ---------- */
typedef struct {
    uint32_t magic, version;
    V3 player_pos; float alert; uint32_t frames; uint32_t checksum;
} SaveState;
int save_write(const char *path, const SaveState *s);
int save_read(const char *path, SaveState *s);

#endif
