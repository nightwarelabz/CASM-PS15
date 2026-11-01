/* CASM-PS15 renderer: frustum culling, per-vertex RGB Gouraud lighting
 * (hemisphere + moon + sodium point lights + dynamic spotlights + Blinn-Phong
 * wet/metallic specular + Fresnel silhouette rim), near-plane clipping, PS1.5
 * vertex snapping, 11-part hierarchical humanoid kinematics, Mecha, particles & rain. */
#include "engine.h"

#define FOCAL_LEN 215.0f
#define NEAR_Z    0.22f

static inline float focal_of(const FB *fb) {
    return (fb && fb->settings.fov_scale > 0.1f)
         ? (FOCAL_LEN * fb->settings.fov_scale)
         : FOCAL_LEN;
}

/* ---------- per-vertex RGB Gouraud + wet specular + Fresnel rim ------- */
static void light_at(const LightRig *rig, V3 cam_pos, V3 wp, V3 n, uint32_t flags,
                     float *lr, float *lg, float *lb) {
    if (!rig) { *lr = *lg = *lb = 1.0f; return; }

    /* Hemisphere sky-to-ground ambient + cool directional moonlight */
    float sky_w = 0.5f + 0.5f * n.y;
    float r = rig->amb_r * lerpf(0.70f, 1.06f, sky_w);
    float g = rig->amb_g * lerpf(0.73f, 1.12f, sky_w);
    float b = rig->amb_b * lerpf(0.68f, 1.28f, sky_w);

    if (rig->fog_dist < 500.0f) {
        V3 moon = v3_norm(v3(-0.40f, 0.62f, -0.67f));
        float md = fmaxf(0.0f, v3_dot(n, moon));
        r += 0.18f * md;
        g += 0.25f * md;
        b += 0.42f * md;
    }

    bool do_spec = (flags & RF_WET_SPEC) != 0;
    bool do_rim  = (flags & RF_RIM_LIT)  != 0;
    V3 V = (do_spec || do_rim) ? v3_norm(v3_sub(cam_pos, wp)) : v3(0, 1, 0);

    /* Grazing Fresnel silhouette rim light for sculpted low-poly character facets */
    float rim_k = 0.0f;
    if (do_rim) {
        float nv = clampf(v3_dot(n, V), 0.0f, 1.0f);
        float f = 1.0f - nv;
        rim_k = f * f * f * clampf(0.45f + 0.55f * (n.y + 0.5f), 0.2f, 1.0f);
        if (rig->fog_dist < 500.0f) {
            r += 0.20f * rim_k;
            g += 0.28f * rim_k;
            b += 0.46f * rim_k;
        } else {
            /* Neutral studio kicker rim for model sheets */
            r += 0.28f * rim_k;
            g += 0.28f * rim_k;
            b += 0.30f * rim_k;
        }
    } else {
        V3 rim = v3_norm(v3(0.45f, 0.35f, -0.82f));
        float rd = fmaxf(0.0f, v3_dot(n, rim)) * 0.14f;
        r += 0.12f * rd; g += 0.16f * rd; b += 0.26f * rd;
    }

    /* Point lights (warm sodium pools, window bounce, practicals + wet specular) */
    for (int i = 0; i < rig->nlights; i++) {
        const PointLight *L = &rig->lights[i];
        V3 to = v3_sub(L->pos, wp);
        float d2 = v3_dot(to, to);
        float r2 = L->range * L->range;
        if (d2 >= r2) continue;
        float dist = sqrtf(d2);
        float fall = 1.0f - dist / L->range;
        fall *= fall;
        V3 ldir = dist > 1e-5f ? v3_scale(to, 1.0f / dist) : v3(0, 1, 0);
        float nd = 0.12f + 0.88f * fmaxf(0.0f, v3_dot(n, ldir));
        float s = fall * nd * 1.42f;
        r += L->r * s; g += L->g * s; b += L->b * s;

        if (do_rim) {
            float back = fmaxf(0.0f, -v3_dot(V, ldir)) * 0.45f + 0.25f;
            float rk = fall * rim_k * back * 0.85f;
            r += L->r * rk; g += L->g * rk; b += L->b * rk;
        }
        if (do_spec) {
            V3 H = v3_norm(v3_add(ldir, V));
            float nh = fmaxf(0.0f, v3_dot(n, H));
            float nh2 = nh * nh;
            float nh4 = nh2 * nh2;
            float nh8 = nh4 * nh4;
            float sp = fall * (0.48f * nh8 + 1.28f * nh8 * nh8);
            r += L->r * sp; g += L->g * sp * 0.96f; b += L->b * sp * 0.90f;
        }
    }

    /* Directional spotlights (watchtower searchlight, guard flashlight, lamps) */
    for (int i = 0; i < rig->nspots; i++) {
        const SpotLight *S = &rig->spots[i];
        V3 to = v3_sub(wp, S->pos);
        float d2 = v3_dot(to, to);
        if (d2 >= S->range * S->range) continue;
        float dist = sqrtf(d2);
        V3 ray = dist > 1e-5f ? v3_scale(to, 1.0f / dist) : S->dir;
        float ca = v3_dot(ray, S->dir);
        if (ca <= S->cos_outer) continue;
        float cone = (ca - S->cos_outer) / fmaxf(1e-4f, S->cos_inner - S->cos_outer);
        cone = clampf(cone, 0.0f, 1.0f);
        cone *= cone;
        float fall = 1.0f - dist / S->range;
        fall *= fall;
        V3 ldir = v3_scale(ray, -1.0f);
        float nd = 0.16f + 0.84f * fmaxf(0.0f, v3_dot(n, ldir));
        float s = fall * cone * nd * 1.58f;
        r += S->r * s; g += S->g * s; b += S->b * s;

        if (do_spec) {
            V3 H = v3_norm(v3_add(ldir, V));
            float nh = fmaxf(0.0f, v3_dot(n, H));
            float nh2 = nh * nh;
            float nh4 = nh2 * nh2;
            float nh8 = nh4 * nh4;
            float sp = fall * cone * (0.55f * nh8 + 1.45f * nh8 * nh8);
            r += S->r * sp; g += S->g * sp; b += S->b * sp;
        }
    }

    *lr = r; *lg = g; *lb = b;
}

float light_exposure_at(const LightRig *rig, V3 wp) {
    if (!rig) return 0.5f;
    float r0, g0, b0, r1, g1, b1;
    light_at(rig, wp, v3(wp.x, wp.y + 1.0f, wp.z), v3(0.0f, 1.0f, 0.0f), RF_NONE, &r0, &g0, &b0);
    light_at(rig, wp, v3(wp.x, wp.y + 1.1f, wp.z), v3(0.0f, 0.3f, 0.95f), RF_NONE, &r1, &g1, &b1);
    float lum0 = r0 * 0.35f + g0 * 0.45f + b0 * 0.20f;
    float lum1 = r1 * 0.35f + g1 * 0.45f + b1 * 0.20f;
    float lum = fmaxf(lum0, lum1);
    return clampf((lum - 0.20f) * 0.85f, 0.08f, 1.0f);
}

/* ---------- view-space clipping & PS1.5 screen projection ------------- */
typedef struct {
    float x, y, z, u, v, lr, lg, lb;
} CVert;

static CVert cvert_lerp(CVert a, CVert b, float t) {
    return (CVert){
        lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t),
        lerpf(a.u, b.u, t), lerpf(a.v, b.v, t),
        lerpf(a.lr, b.lr, t), lerpf(a.lg, b.lg, t), lerpf(a.lb, b.lb, t)
    };
}

static inline float snap_coord(float v, SnapMode mode) {
    if (mode == SNAP_PS15) return lrintf(v * 2.0f) * 0.5f;
    if (mode == SNAP_PS1)  return lrintf(v * (2.0f / 3.0f)) * 1.5f;
    return v;
}

static bool sphere_visible(const FB *fb, const Camera *cam, V3 center, float radius) {
    if (radius <= 0.0f) return true;
    V3 d = v3_sub(center, cam->pos);
    float ccam = cosf(cam->yaw), scam = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float vx = ccam * d.x - scam * d.z;
    float vz0 = scam * d.x + ccam * d.z;
    float vy = cp * d.y - sp * vz0;
    float vz = sp * d.y + cp * vz0;
    if (vz + radius < NEAR_Z) return false;
    float focal = focal_of(fb);
    float hx = ((float)FB_W * 0.56f / focal);
    float hy = ((float)FB_H * 0.56f / focal);
    float lim_x = fmaxf(vz, 0.0f) * hx + radius * 1.35f;
    float lim_y = fmaxf(vz, 0.0f) * hy + radius * 1.35f;
    if (fabsf(vx) > lim_x || fabsf(vy) > lim_y) return false;
    return true;
}

static void render_verts(FB *fb, const Camera *cam,
                         const MVert *verts, const MTri *tris, int nt,
                         V3 pos, float yaw, float pitch, float scale,
                         const Tex *tex, float tr, float tg, float tb,
                         const LightRig *rig, BlendMode blend, uint32_t flags) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cmp = cosf(pitch), smp = sinf(pitch);
    float ccam = cosf(cam->yaw), scam = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float focal = focal_of(fb);

    int fog_r = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_r * 255.0f) : 24;
    int fog_g = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_g * 255.0f) : 36;
    int fog_b = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_b * 255.0f) : 58;
    float fog_dist = (rig && rig->fog_dist > 0.0f) ? rig->fog_dist : 48.0f;

    for (int t = 0; t < nt; t++) {
        const int idx[3] = { tris[t].a, tris[t].b, tris[t].c };
        CVert in[3];
        for (int i = 0; i < 3; i++) {
            const MVert *v = &verts[idx[i]];
            V3 lp = v3_scale(v->p, scale);
            float py_m = cmp * lp.y - smp * lp.z;
            float pz_m = smp * lp.y + cmp * lp.z;
            V3 rp = v3(cy * lp.x + sy * pz_m, py_m, -sy * lp.x + cy * pz_m);
            V3 wp = v3_add(rp, pos);

            float ny_m = cmp * v->n.y - smp * v->n.z;
            float nz_m = smp * v->n.y + cmp * v->n.z;
            V3 rn = v3(cy * v->n.x + sy * nz_m, ny_m, -sy * v->n.x + cy * nz_m);

            float vr = 1.0f, vg = 1.0f, vb = 1.0f;
            if (!(flags & RF_UNLIT)) {
                light_at(rig, cam->pos, wp, rn, flags, &vr, &vg, &vb);
            }
            vr *= tr; vg *= tg; vb *= tb;

            V3 d = v3_sub(wp, cam->pos);
            float vx = ccam * d.x - scam * d.z;
            float vz0 = scam * d.x + ccam * d.z;
            float vy = cp * d.y - sp * vz0;
            float vz = sp * d.y + cp * vz0;
            in[i] = (CVert){ vx, vy, vz, v->u, v->v, vr, vg, vb };
        }

        CVert poly[5];
        int np = 0;
        for (int i = 0; i < 3; i++) {
            CVert cur = in[i], prv = in[(i + 2) % 3];
            bool cur_in = cur.z >= NEAR_Z, prv_in = prv.z >= NEAR_Z;
            if (cur_in != prv_in) {
                float tt = (NEAR_Z - prv.z) / (cur.z - prv.z);
                poly[np++] = cvert_lerp(prv, cur, tt);
            }
            if (cur_in) poly[np++] = cur;
        }
        if (np < 3) {
            fb->stats.tris_submitted++;
            fb->stats.tris_culled++;
            continue;
        }

        for (int k = 1; k < np - 1; k++) {
            const CVert *tri3[3] = { &poly[0], &poly[k], &poly[k + 1] };
            bool all_left = true, all_right = true, all_top = true, all_bot = true;
            SVertEx sv[3];
            for (int i = 0; i < 3; i++) {
                float sx = (float)fb->w * 0.5f + tri3[i]->x * focal / tri3[i]->z;
                float sy_p = (float)fb->h * 0.5f - tri3[i]->y * focal / tri3[i]->z;
                sx = snap_coord(sx, fb->settings.snap_mode);
                sy_p = snap_coord(sy_p, fb->settings.snap_mode);
                if (sx >= 0.0f) all_left = false;
                if (sx < (float)fb->w) all_right = false;
                if (sy_p >= 0.0f) all_top = false;
                if (sy_p < (float)fb->h) all_bot = false;
                sv[i] = (SVertEx){ sx, sy_p, tri3[i]->z,
                                   tri3[i]->u, tri3[i]->v,
                                   tri3[i]->lr, tri3[i]->lg, tri3[i]->lb };
            }
            if (all_left || all_right || all_top || all_bot) {
                fb->stats.tris_submitted++;
                fb->stats.tris_culled++;
                continue;
            }
            float fogz = (sv[0].z + sv[1].z + sv[2].z) * (1.0f / 3.0f);
            float ft = clampf((fogz - 5.5f) / fog_dist, 0.0f, 1.0f);
            float fog = ft * ft * (3.0f - 2.0f * ft) * 0.88f;
            raster_tri_ex(fb, sv[0], sv[1], sv[2],
                          tex ? tex->px : NULL, tex ? tex->w : 0, tex ? tex->h : 0,
                          blend, flags, fog, fog_r, fog_g, fog_b);
        }
    }
}

void render_mesh_ex(FB *fb, const Camera *cam, const Mesh *m,
                    V3 pos, float yaw, float pitch, float scale,
                    const Tex *tex, float tr, float tg, float tb,
                    const LightRig *rig, BlendMode blend, uint32_t flags) {
    if (!m || m->nt <= 0) return;
    if (m->radius > 0.0f) {
        float cy = cosf(yaw), sy = sinf(yaw);
        V3 sc = v3_scale(m->center, scale);
        V3 wc = v3_add(pos, v3(cy * sc.x + sy * sc.z, sc.y, -sy * sc.x + cy * sc.z));
        if (!sphere_visible(fb, cam, wc, m->radius * scale)) {
            fb->stats.tris_submitted += m->nt;
            fb->stats.tris_culled += m->nt;
            return;
        }
    }
    render_verts(fb, cam, m->verts, m->tris, m->nt,
                 pos, yaw, pitch, scale, tex, tr, tg, tb, rig, blend, flags);
}

void render_mesh(FB *fb, const Camera *cam, const Mesh *m,
                 V3 pos, float yaw, const Tex *tex,
                 float tr, float tg, float tb, const LightRig *rig) {
    render_mesh_ex(fb, cam, m, pos, yaw, 0.0f, 1.0f, tex, tr, tg, tb,
                   rig, BLEND_OPAQUE, RF_NONE);
}

/* Real-time planar reflection across y = water_y with horizontal wave shimmer */
void render_mesh_mirror(FB *fb, const Camera *cam, const Mesh *m,
                        V3 pos, float yaw, float water_y, float ripple_t,
                        const Tex *tex, float tr, float tg, float tb,
                        const LightRig *rig, uint32_t flags) {
    if (!m || m->nt <= 0 || m->nv <= 0) return;
    if (m->radius > 0.0f) {
        float cy = cosf(yaw), sy = sinf(yaw);
        V3 wc = v3_add(pos, v3(cy * m->center.x + sy * m->center.z,
                               0.0f,
                               -sy * m->center.x + cy * m->center.z));
        wc.y = 2.0f * water_y - (pos.y + m->center.y);
        if (!sphere_visible(fb, cam, wc, m->radius * 1.15f)) {
            fb->stats.tris_submitted += m->nt;
            fb->stats.tris_culled += m->nt;
            return;
        }
    }

    float cy = cosf(yaw), sy = sinf(yaw);
    float ccam = cosf(cam->yaw), scam = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float focal = focal_of(fb);
    int fog_r = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_r * 255.0f) : 24;
    int fog_g = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_g * 255.0f) : 36;
    int fog_b = (rig && rig->fog_dist > 0.0f) ? (int)(rig->fog_b * 255.0f) : 58;
    float fog_dist = (rig && rig->fog_dist > 0.0f) ? rig->fog_dist : 48.0f;

    for (int t = 0; t < m->nt; t++) {
        const int idx[3] = { m->tris[t].a, m->tris[t].b, m->tris[t].c };
        CVert in[3];
        bool any_above = false;
        for (int i = 0; i < 3; i++) {
            const MVert *v = &m->verts[idx[i]];
            V3 rp = v3(cy * v->p.x + sy * v->p.z, v->p.y, -sy * v->p.x + cy * v->p.z);
            V3 wp = v3_add(rp, pos);
            if (wp.y > water_y + 0.02f) any_above = true;

            V3 rn = v3(cy * v->n.x + sy * v->n.z, v->n.y, -sy * v->n.x + cy * v->n.z);
            float vr = 1.0f, vg = 1.0f, vb = 1.0f;
            if (!(flags & RF_UNLIT)) light_at(rig, cam->pos, wp, rn, flags, &vr, &vg, &vb);
            vr *= tr; vg *= tg; vb *= tb;

            float h_above = fmaxf(0.0f, wp.y - water_y);
            wp.y = water_y - h_above;
            wp.x += sinf(wp.z * 1.7f + h_above * 0.9f + ripple_t * 3.1f) *
                    0.08f * clampf(h_above * 0.3f, 0.0f, 1.0f);

            V3 d = v3_sub(wp, cam->pos);
            float vx = ccam * d.x - scam * d.z;
            float vz0 = scam * d.x + ccam * d.z;
            float vy = cp * d.y - sp * vz0;
            float vz = sp * d.y + cp * vz0;
            in[i] = (CVert){ vx, vy, vz, v->u, v->v, vr, vg, vb };
        }
        if (!any_above) continue;

        CVert poly[5];
        int np = 0;
        for (int i = 0; i < 3; i++) {
            CVert cur = in[i], prv = in[(i + 2) % 3];
            bool cur_in = cur.z >= NEAR_Z, prv_in = prv.z >= NEAR_Z;
            if (cur_in != prv_in) {
                float tt = (NEAR_Z - prv.z) / (cur.z - prv.z);
                poly[np++] = cvert_lerp(prv, cur, tt);
            }
            if (cur_in) poly[np++] = cur;
        }
        if (np < 3) continue;

        for (int k = 1; k < np - 1; k++) {
            const CVert *tri3[3] = { &poly[0], &poly[k], &poly[k + 1] };
            SVertEx sv[3];
            for (int i = 0; i < 3; i++) {
                float sx = snap_coord((float)fb->w * 0.5f + tri3[i]->x * focal / tri3[i]->z,
                                      fb->settings.snap_mode);
                float sy_p = snap_coord((float)fb->h * 0.5f - tri3[i]->y * focal / tri3[i]->z,
                                        fb->settings.snap_mode);
                sv[i] = (SVertEx){ sx, sy_p, tri3[i]->z, tri3[i]->u, tri3[i]->v,
                                   tri3[i]->lr, tri3[i]->lg, tri3[i]->lb };
            }
            float fogz = (sv[0].z + sv[1].z + sv[2].z) * (1.0f / 3.0f);
            float ft = clampf((fogz - 4.0f) / fog_dist, 0.0f, 1.0f);
            float fog = ft * ft * (3.0f - 2.0f * ft) * 0.90f;
            raster_tri_ex(fb, sv[0], sv[1], sv[2],
                          tex ? tex->px : NULL, tex ? tex->w : 0, tex ? tex->h : 0,
                          BLEND_OPAQUE, flags | RF_MIRROR_Y,
                          fog, fog_r, fog_g, fog_b);
        }
    }
}

/* Animated UV-scrolling harbor water plane rendered via PS1 ABR + wet specular */
void render_water_plane(FB *fb, const Camera *cam, const Mesh *m,
                        V3 pos, const Tex *water_tex, float time_s,
                        float tr, float tg, float tb, const LightRig *rig,
                        BlendMode blend) {
    if (!m || m->nv <= 0 || m->nv > 1024) return;
    MVert tmp[1024];
    float du = time_s * 0.14f;
    float dv = time_s * 0.08f;
    for (int i = 0; i < m->nv; i++) {
        tmp[i] = m->verts[i];
        float wx = pos.x + tmp[i].p.x;
        float wz = pos.z + tmp[i].p.z;
        tmp[i].p.y += sinf(wx * 0.9f + time_s * 2.2f) * 0.032f +
                      cosf(wz * 0.7f - time_s * 1.8f) * 0.028f;
        tmp[i].u += du + sinf(wz * 0.5f + time_s * 1.5f) * 0.05f;
        tmp[i].v += dv;
    }
    render_verts(fb, cam, tmp, m->tris, m->nt,
                 pos, 0.0f, 0.0f, 1.0f, water_tex, tr, tg, tb,
                 rig, blend, RF_WET_SPEC);
}

/* ---------- 11-part hierarchical humanoid kinematics ------------------ */
static V3 rot_x_v(V3 p, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}
static V3 rot_y_v(V3 p, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}
static V3 rot_z_v(V3 p, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

static V3 xform_chain(V3 p, float ry, float rz, float rx) {
    if (ry != 0.0f) p = rot_y_v(p, ry);
    if (rz != 0.0f) p = rot_z_v(p, rz);
    if (rx != 0.0f) p = rot_x_v(p, rx);
    return p;
}

void render_humanoid(FB *fb, const Camera *cam, const Humanoid *h,
                     V3 pos, float yaw, const Pose *pose,
                     const Tex *texset, float tr, float tg, float tb,
                     const LightRig *rig) {
    if (!sphere_visible(fb, cam, v3(pos.x, pos.y + 0.95f, pos.z), 1.40f)) return;

    float crouch_drop = clampf(pose->crouch, 0.0f, 1.0f) * 0.26f;
    float hip_y = 0.82f - crouch_drop;
    float lean = pose->lean;
    float aim = clampf(pose->aim, 0.0f, 1.0f);
    float tyaw = pose->torso_yaw;

    V3 j_hips  = v3(0.0f, hip_y - 0.04f, 0.0f);
    V3 j_torso = v3(0.0f, hip_y + 0.13f, 0.0f);
    V3 j_head  = v3_add(j_torso, xform_chain(v3(0.0f, 0.56f, 0.01f), tyaw, 0.0f, lean));

    V3 j_sh_l  = v3_add(j_torso, xform_chain(v3(-0.224f, 0.49f, 0.01f), tyaw, 0.0f, lean));
    V3 j_sh_r  = v3_add(j_torso, xform_chain(v3( 0.224f, 0.49f, 0.01f), tyaw, 0.0f, lean));

    float uarm_l_rx = pose->arm_l + lean;
    float uarm_r_rx = pose->arm_r + lean;
    /* In two-handed Center-Axis-Relock stance (aim > 0), right elbow flares cleanly
     * so the gold NSA shoulder crest faces the viewer while both gloved hands
     * interlock tightly at sternum height with a level HK USP sightline! */
    float uarm_l_rz =  0.30f * aim;
    float uarm_r_rz = -0.18f * aim;
    float uarm_l_ry =  0.18f * aim;
    float uarm_r_ry = -0.22f * aim;

    V3 j_elb_l = v3_add(j_sh_l, xform_chain(v3(0, -0.27f, 0), uarm_l_ry, uarm_l_rz, uarm_l_rx));
    V3 j_elb_r = v3_add(j_sh_r, xform_chain(v3(0, -0.27f, 0), uarm_r_ry, uarm_r_rz, uarm_r_rx));

    float elb_l = (pose->elbow_l != 0.0f) ? pose->elbow_l : (0.28f + 0.25f * fabsf(pose->arm_l));
    float elb_r = (pose->elbow_r != 0.0f) ? pose->elbow_r : (0.48f + 0.20f * fabsf(pose->arm_r));
    float farm_l_rx = (aim > 0.0f) ? lerpf(uarm_l_rx - elb_l, -1.42f, aim) : (uarm_l_rx - elb_l);
    float farm_r_rx = (aim > 0.0f) ? lerpf(uarm_r_rx - elb_r, -1.48f, aim) : (uarm_r_rx - elb_r);
    float farm_l_rz = lerpf(uarm_l_rz,  0.48f, aim);
    float farm_r_rz = lerpf(uarm_r_rz, -0.22f, aim);
    float farm_l_ry =  0.22f * aim;
    float farm_r_ry = -0.28f * aim;

    float leg_stance = 0.052f * (1.0f - fminf(1.0f, fabsf(pose->leg_l) + fabsf(pose->leg_r)));
    float leg_l_rz = -leg_stance;
    float leg_r_rz =  leg_stance;

    V3 j_hip_l = v3(-0.105f, hip_y, 0.0f);
    V3 j_hip_r = v3( 0.105f, hip_y, 0.0f);
    V3 j_knee_l = v3_add(j_hip_l, xform_chain(v3(0, -0.39f, 0), 0.0f, leg_l_rz, pose->leg_l));
    V3 j_knee_r = v3_add(j_hip_r, xform_chain(v3(0, -0.39f, 0), 0.0f, leg_r_rz, pose->leg_r));

    float head_rx = lean * 0.45f + pose->head_pitch;
    float head_ry = tyaw * 0.5f + pose->head_yaw;

    const struct {
        V3 origin;
        float ry, rz, rx;
    } X[PART_COUNT] = {
        [PART_HIPS]      = { j_hips,   tyaw * 0.35f, 0.0f,      lean * 0.4f },
        [PART_TORSO]     = { j_torso,  tyaw,         0.0f,      lean },
        [PART_HEAD]      = { j_head,   head_ry,      0.0f,      head_rx },
        [PART_ARM_L]     = { j_sh_l,   uarm_l_ry,    uarm_l_rz, uarm_l_rx },
        [PART_ARM_R]     = { j_sh_r,   uarm_r_ry,    uarm_r_rz, uarm_r_rx },
        [PART_FOREARM_L] = { j_elb_l,  farm_l_ry,    farm_l_rz, farm_l_rx },
        [PART_FOREARM_R] = { j_elb_r,  farm_r_ry,    farm_r_rz, farm_r_rx },
        [PART_LEG_L]     = { j_hip_l,  0.0f,         leg_l_rz,  pose->leg_l },
        [PART_LEG_R]     = { j_hip_r,  0.0f,         leg_r_rz,  pose->leg_r },
        [PART_SHIN_L]    = { j_knee_l, 0.0f,         leg_l_rz,  pose->leg_l + pose->knee_l },
        [PART_SHIN_R]    = { j_knee_r, 0.0f,         leg_r_rz,  pose->leg_r + pose->knee_r },
    };

    MVert tmp[2048];
    for (int part = 0; part < PART_COUNT; part++) {
        const Mesh *m = &h->parts[part];
        if (m->nv <= 0 || m->nv > (int)(sizeof tmp / sizeof tmp[0])) continue;
        for (int i = 0; i < m->nv; i++) {
            tmp[i] = m->verts[i];
            tmp[i].p = v3_add(xform_chain(m->verts[i].p, X[part].ry, X[part].rz, X[part].rx),
                              X[part].origin);
            tmp[i].n = xform_chain(m->verts[i].n, X[part].ry, X[part].rz, X[part].rx);
        }
        const Tex *ptex = &texset[h->part_tex[part]];
        float ptr = tr, ptg = tg, ptb = tb;
        uint32_t pflags = RF_RIM_LIT;
        if (!h->is_guard && (part == PART_HEAD || part == PART_FOREARM_R))
            pflags |= RF_WET_SPEC;
        if (!h->is_guard && part == PART_HEAD) { ptr *= 1.08f; ptg *= 1.04f; ptb *= 1.04f; }
        render_verts(fb, cam, tmp, m->tris, m->nt,
                     pos, yaw, 0.0f, 1.0f, ptex, ptr, ptg, ptb,
                     rig, BLEND_OPAQUE, pflags);
    }

    /* Pinpoint crimson-red optic LED glint on the operator's ballistic goggles */
    if (!h->is_guard) {
        V3 led_local = v3_add(j_head, xform_chain(v3(-0.022f, 0.134f, 0.166f),
                                                  head_ry, 0.0f, head_rx));
        float cy = cosf(yaw), sy = sinf(yaw);
        V3 led_world = v3_add(pos, v3(cy * led_local.x + sy * led_local.z,
                                      led_local.y,
                                      -sy * led_local.x + cy * led_local.z));
        render_glow3d(fb, cam, led_world, 0.026f, 255, 34, 46);
        render_glow3d(fb, cam, led_world, 0.010f, 255, 210, 220);
    }
}

/* ---------- Towering Bipedal Regime Patrol Walker (Mecha Type-1) ------- */
void render_mecha(FB *fb, const Camera *cam, const MechaMesh *mm,
                  const Mecha *mc, const Tex *texset, const LightRig *rig) {
    if (!mm || !mc) return;
    if (!sphere_visible(fb, cam, v3(mc->pos.x, mc->pos.y + 1.85f, mc->pos.z), 2.8f))
        return;

    bool alive = (mc->coolant_hp > 0.01f);
    float cyc = mc->anim_t * (2.0f * PS15_PI / 1.80f);
    float stride = alive ? sinf(cyc) * 0.40f : 0.0f;
    float bob    = alive ? fabsf(cosf(cyc)) * 0.08f : -0.18f;
    float roll_y = alive ? sinf(cyc) * 0.06f : 0.0f;
    float scan   = (alive && mc->sensor_hp > 0.1f) ? sinf(mc->anim_t * 1.4f) * 0.35f : 0.0f;
    float lean   = alive ? (0.08f + mc->alert * 0.10f) : 0.38f;

    float hip_y = 1.86f + bob;
    V3 j_pelvis = v3(0.0f, hip_y, 0.0f);
    V3 j_torso  = v3(0.0f, hip_y + 0.24f, 0.0f);
    V3 j_head   = v3_add(j_torso, rot_x_v(v3(0.0f, 0.86f, 0.28f), lean));
    V3 j_sh_l   = v3_add(j_torso, rot_x_v(v3(-0.66f, 0.78f, 0.0f), lean));
    V3 j_sh_r   = v3_add(j_torso, rot_x_v(v3( 0.66f, 0.78f, 0.0f), lean));
    V3 j_hip_l  = v3(-0.38f, hip_y - 0.06f, 0.0f);
    V3 j_hip_r  = v3( 0.38f, hip_y - 0.06f, 0.0f);

    float leg_splay = 0.115f;
    float leg_l = -stride - 0.28f;
    float leg_r =  stride - 0.28f;
    float shin_l = 0.38f + fmaxf(0.0f, stride) * 0.45f;
    float shin_r = 0.38f + fmaxf(0.0f, -stride) * 0.45f;
    V3 j_knee_l = v3_add(j_hip_l, xform_chain(v3(0, -0.86f, 0), 0.0f, -leg_splay, leg_l));
    V3 j_knee_r = v3_add(j_hip_r, xform_chain(v3(0, -0.86f, 0), 0.0f,  leg_splay, leg_r));

    const struct {
        const Mesh *m;
        V3 org;
        float ry, rz, rx;
        int tex;
    } P[9] = {
        { &mm->pelvis, j_pelvis, 0.0f,   0.0f,       lean * 0.3f, TEX_METAL },
        { &mm->torso,  j_torso,  roll_y, 0.0f,       lean,        TEX_INSIGNIA },
        { &mm->head,   j_head,   scan,   0.0f,       lean * 0.6f, TEX_INSIGNIA },
        { &mm->arm_l,  j_sh_l,   0.0f,   0.08f,      lean + stride * 0.35f, TEX_INSIGNIA },
        { &mm->arm_r,  j_sh_r,   0.0f,  -0.06f,      lean - 0.28f - mc->alert * 0.35f, TEX_METAL },
        { &mm->leg_l,  j_hip_l,  0.0f,  -leg_splay,  leg_l,       TEX_INSIGNIA },
        { &mm->leg_r,  j_hip_r,  0.0f,   leg_splay,  leg_r,       TEX_INSIGNIA },
        { &mm->shin_l, j_knee_l, 0.0f,  -leg_splay,  leg_l + shin_l, TEX_METAL },
        { &mm->shin_r, j_knee_r, 0.0f,   leg_splay,  leg_r + shin_r, TEX_METAL },
    };

    MVert tmp[2048];
    for (int k = 0; k < 9; k++) {
        const Mesh *m = P[k].m;
        if (m->nv <= 0 || m->nv > (int)(sizeof tmp / sizeof tmp[0])) continue;
        for (int i = 0; i < m->nv; i++) {
            tmp[i] = m->verts[i];
            tmp[i].p = v3_add(xform_chain(m->verts[i].p, P[k].ry, P[k].rz, P[k].rx),
                              P[k].org);
            tmp[i].n = xform_chain(m->verts[i].n, P[k].ry, P[k].rz, P[k].rx);
        }
        render_verts(fb, cam, tmp, m->tris, m->nt,
                     mc->pos, mc->yaw, 0.0f, 1.0f, &texset[P[k].tex],
                     1.0f, 1.0f, 1.02f, rig, BLEND_OPAQUE, RF_RIM_LIT | RF_WET_SPEC);
    }

    if (alive && mc->sensor_hp > 0.15f) {
        V3 eye_loc = v3_add(j_head, xform_chain(v3(0.0f, 0.02f, 0.34f), scan, 0.0f, lean * 0.6f));
        float cy = cosf(mc->yaw), sy = sinf(mc->yaw);
        V3 eye_w = v3_add(mc->pos, v3(cy * eye_loc.x + sy * eye_loc.z,
                                      eye_loc.y,
                                      -sy * eye_loc.x + cy * eye_loc.z));
        int er = (int)(175.0f * mc->sensor_hp + 55.0f * mc->alert);
        render_glow3d(fb, cam, eye_w, 0.15f, er, 20, 24);
    }
}

bool render_project(const FB *fb, const Camera *cam, V3 wp,
                    int *sx, int *sy, float *out_z) {
    V3 d = v3_sub(wp, cam->pos);
    float vx  = cosf(cam->yaw) * d.x - sinf(cam->yaw) * d.z;
    float vz0 = sinf(cam->yaw) * d.x + cosf(cam->yaw) * d.z;
    float vy  = cosf(cam->pitch) * d.y - sinf(cam->pitch) * vz0;
    float vz  = sinf(cam->pitch) * d.y + cosf(cam->pitch) * vz0;
    if (vz < 0.35f) return false;
    float focal = focal_of(fb);
    int px = (int)lrintf((float)fb->w * 0.5f + vx * focal / vz);
    int py = (int)lrintf((float)fb->h * 0.5f - vy * focal / vz);
    if (sx) *sx = px;
    if (sy) *sy = py;
    if (out_z) *out_z = vz;
    return (px >= -40 && px < fb->w + 40 && py >= -40 && py < fb->h + 40);
}

/* ---------- first-person tactical pistol & gloved hands viewmodel ----- */
void render_viewmodel_ex(FB *fb, const Camera *cam, const Viewmodel *vm,
                         const Tex *texset, const LightRig *rig,
                         float bob_phase, float bob_amp, float pitch_sway,
                         bool crouching, bool grappling, float recoil) {
    V3 fwd = v3(sinf(cam->yaw) * cosf(cam->pitch), sinf(cam->pitch),
                cosf(cam->yaw) * cosf(cam->pitch));
    V3 probe = v3_add(cam->pos, v3_scale(fwd, 0.45f));
    float lr, lg, lb;
    light_at(rig, cam->pos, probe, v3(0, 0.8f, 0.6f), RF_NONE, &lr, &lg, &lb);
    lr = fmaxf(lr, 0.34f); lg = fmaxf(lg, 0.36f); lb = fmaxf(lb, 0.46f);

    float rec = clampf(recoil, 0.0f, 1.0f);
    float kick = sinf(rec * PS15_PI);
    float bx = sinf(bob_phase) * 0.014f * bob_amp;
    float by = -fabsf(cosf(bob_phase)) * 0.012f * bob_amp
               + (crouching ? -0.018f : 0.0f)
               + (grappling ? -0.08f : 0.0f)
               + kick * 0.014f;
    float bz = -kick * 0.028f;
    float tilt = clampf(pitch_sway * 0.08f, -0.12f, 0.12f)
                 + (crouching ? 0.08f : 0.0f)
                 - kick * 0.18f;

    RenderStats saved_stats = fb->stats;
    fb_clear_depth(fb);
    fb->stats = saved_stats;
    float flash = (rec > 0.65f) ? (rec - 0.65f) * 3.2f : 0.0f;
    PointLight vlights[1] = {
        { v3(-0.24f, 0.38f, 0.25f), 1.6f,
          lr * 0.95f + flash * 1.4f, lg * 0.90f + flash * 0.95f, lb * 0.85f + flash * 0.45f }
    };
    LightRig vrig = {
        vlights, 1,
        lr * 0.52f, lg * 0.54f, lb * 0.60f,
        NULL, 0,
        0, 0, 0, 100.0f
    };

    Camera vcam = { v3(0, 0, 0), 0.0f, 0.0f };
    V3 ofs = v3(bx, by, bz);
    V3 slide_ofs = v3(bx, by - kick * 0.004f, bz - kick * 0.056f);
    uint32_t fl = RF_NO_FOG | RF_RIM_LIT;
    render_mesh_ex(fb, &vcam, &vm->sleeve_l, ofs, -0.05f * (crouching ? 1 : 0),
                   tilt, 1.0f, &texset[TEX_CAMO], 1.0f, 1.0f, 1.0f, &vrig, BLEND_OPAQUE, fl);
    render_mesh_ex(fb, &vcam, &vm->sleeve_r, ofs, 0.0f,
                   tilt, 1.0f, &texset[TEX_CAMO], 1.0f, 1.0f, 1.0f, &vrig, BLEND_OPAQUE, fl);
    render_mesh_ex(fb, &vcam, &vm->glove_l, ofs, 0.0f,
                   tilt, 1.0f, &texset[TEX_WEAPON], 1.0f, 1.0f, 1.0f, &vrig, BLEND_OPAQUE, fl);
    render_mesh_ex(fb, &vcam, &vm->glove_r, ofs, 0.0f,
                   tilt, 1.0f, &texset[TEX_WEAPON], 1.0f, 1.0f, 1.0f, &vrig, BLEND_OPAQUE, fl);
    render_mesh_ex(fb, &vcam, &vm->pistol_frame, ofs, 0.0f,
                   tilt, 1.0f, &texset[TEX_WEAPON], 1.0f, 1.0f, 1.0f, &vrig, BLEND_OPAQUE, fl);
    render_mesh_ex(fb, &vcam, &vm->pistol_slide, slide_ofs, 0.0f,
                   tilt, 1.0f, &texset[TEX_WEAPON], 1.18f, 1.18f, 1.24f, &vrig, BLEND_OPAQUE, fl | RF_WET_SPEC);

    if (rec > 0.68f) {
        V3 mpos = v3(0.060f + bx, -0.075f + by + kick * 0.08f, 0.78f + bz);
        render_glow3d(fb, &vcam, mpos, 0.055f, 185, 130, 62);
    }
}

void render_viewmodel(FB *fb, const Camera *cam, const Viewmodel *vm,
                      const Tex *texset, const LightRig *rig,
                      float bob_phase, float bob_amp, float pitch_sway,
                      bool crouching, bool grappling) {
    render_viewmodel_ex(fb, cam, vm, texset, rig, bob_phase, bob_amp, pitch_sway,
                        crouching, grappling, 0.0f);
}

/* ---------- grounded contact shadows, 3D lines, glows & particles ----- */
void render_blob_shadow(FB *fb, const Camera *cam, const Mesh *quad,
                        const Tex *shadow_tex, V3 pos, float radius) {
    render_mesh_ex(fb, cam, quad, v3(pos.x, pos.y + 0.03f, pos.z),
                   0.0f, 0.0f, radius * 2.0f, shadow_tex,
                   0.65f, 0.65f, 0.62f, NULL,
                   BLEND_SUB, RF_UNLIT | RF_DOUBLE_SIDED);
}

void render_line3d(FB *fb, const Camera *cam, V3 a, V3 b,
                   uint32_t col, BlendMode blend) {
    float ccam = cosf(cam->yaw), scam = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float focal = focal_of(fb);
    V3 pts[2] = { a, b };
    V3 vp[2];
    for (int i = 0; i < 2; i++) {
        V3 d = v3_sub(pts[i], cam->pos);
        float vx = ccam * d.x - scam * d.z;
        float vz0 = scam * d.x + ccam * d.z;
        float vy = cp * d.y - sp * vz0;
        float vz = sp * d.y + cp * vz0;
        vp[i] = v3(vx, vy, vz);
    }
    if (vp[0].z < NEAR_Z && vp[1].z < NEAR_Z) return;
    if (vp[0].z < NEAR_Z) {
        float t = (NEAR_Z - vp[0].z) / (vp[1].z - vp[0].z);
        vp[0] = v3(lerpf(vp[0].x, vp[1].x, t), lerpf(vp[0].y, vp[1].y, t), NEAR_Z);
    } else if (vp[1].z < NEAR_Z) {
        float t = (NEAR_Z - vp[1].z) / (vp[0].z - vp[1].z);
        vp[1] = v3(lerpf(vp[1].x, vp[0].x, t), lerpf(vp[1].y, vp[0].y, t), NEAR_Z);
    }
    float sx0 = (float)fb->w * 0.5f + vp[0].x * focal / vp[0].z;
    float sy0 = (float)fb->h * 0.5f - vp[0].y * focal / vp[0].z;
    float sx1 = (float)fb->w * 0.5f + vp[1].x * focal / vp[1].z;
    float sy1 = (float)fb->h * 0.5f - vp[1].y * focal / vp[1].z;
    raster_line(fb, sx0, sy0, vp[0].z, sx1, sy1, vp[1].z, col, blend);
}

void render_glow3d(FB *fb, const Camera *cam, V3 pos, float world_radius,
                   int r, int g, int b) {
    V3 d = v3_sub(pos, cam->pos);
    float vx = cosf(cam->yaw) * d.x - sinf(cam->yaw) * d.z;
    float vz0 = sinf(cam->yaw) * d.x + cosf(cam->yaw) * d.z;
    float vy = cosf(cam->pitch) * d.y - sinf(cam->pitch) * vz0;
    float vz = sinf(cam->pitch) * d.y + cosf(cam->pitch) * vz0;
    if (vz < 0.45f) return;
    float focal = focal_of(fb);
    int gx = (int)lrintf((float)fb->w * 0.5f + vx * focal / vz);
    int gy = (int)lrintf((float)fb->h * 0.5f - vy * focal / vz);
    int rad = (int)lrintf(world_radius * focal / vz);
    if (rad < 2) rad = 2;
    if (rad > 120) rad = 120;
    if (gx + rad < 0 || gx - rad >= fb->w || gy + rad < 0 || gy - rad >= fb->h) return;
    raster_glow_z(fb, gx, gy, rad, vz, r, g, b);
}

void particles_init(ParticlePool *pp, uint64_t seed) {
    if (!pp) return;
    memset(pp, 0, sizeof *pp);
    pp->rng.s = seed ? seed : 0x51A1C35ULL;
}

void particles_spawn_burst(ParticlePool *pp, V3 pos, V3 dir_bias, int count,
                           float speed, float gravity, float life,
                           uint32_t col, bool glow) {
    if (!pp) return;
    for (int k = 0; k < count; k++) {
        int slot = -1;
        float lowest = 1e9f;
        for (int i = 0; i < PS15_MAX_PARTICLES; i++) {
            if (pp->items[i].life <= 0.0f) { slot = i; break; }
            if (pp->items[i].life < lowest) { lowest = pp->items[i].life; slot = i; }
        }
        if (slot < 0) break;
        float rx = rng_f01(&pp->rng) * 2.0f - 1.0f;
        float ry = rng_f01(&pp->rng) * 2.0f - 0.5f;
        float rz = rng_f01(&pp->rng) * 2.0f - 1.0f;
        V3 dir = v3_norm(v3_add(v3(rx, ry, rz), dir_bias));
        float sp = speed * (0.45f + 0.75f * rng_f01(&pp->rng));
        float lf = life  * (0.65f + 0.55f * rng_f01(&pp->rng));
        pp->items[slot] = (Particle){
            pos, v3_scale(dir, sp), lf, lf, gravity, col, glow
        };
    }
}

void particles_update(ParticlePool *pp, float dt) {
    if (!pp) return;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) {
        Particle *p = &pp->items[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->vel.y -= p->gravity * dt;
        p->vel = v3_scale(p->vel, fmaxf(0.0f, 1.0f - 1.8f * dt));
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        if (p->pos.y < 0.03f && p->vel.y < 0.0f) {
            p->pos.y = 0.03f;
            p->vel.y = -p->vel.y * 0.35f;
        }
    }
}

void particles_render(FB *fb, const Camera *cam, const ParticlePool *pp) {
    if (!pp) return;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) {
        const Particle *p = &pp->items[i];
        if (p->life <= 0.0f) continue;
        float frac = clampf(p->life / fmaxf(p->max_life, 0.01f), 0.0f, 1.0f);
        int r = (int)(((p->col >> 16) & 255u) * frac);
        int g = (int)(((p->col >> 8)  & 255u) * frac);
        int b = (int)(( p->col        & 255u) * frac);
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        V3 tail = v3_sub(p->pos, v3_scale(p->vel, 0.045f));
        render_line3d(fb, cam, p->pos, tail, c, p->glow ? BLEND_ADD : BLEND_HALF);
        if (p->glow && frac > 0.35f)
            render_glow3d(fb, cam, p->pos, 0.06f, r / 2, g / 2, b / 2);
    }
}

/* Deterministic 3D night rain streaks wrapping around the camera volume */
void render_rain(FB *fb, const Camera *cam, float time_s, int count) {
    Rng rng = { 0x51A19A11ULL };
    const float BOX = 24.0f, HALF = 12.0f, H = 14.0f;
    float base_x = floorf(cam->pos.x / BOX) * BOX;
    float base_z = floorf(cam->pos.z / BOX) * BOX;

    for (int i = 0; i < count; i++) {
        float rx = rng_f01(&rng) * BOX - HALF;
        float ry0 = rng_f01(&rng) * H;
        float rz = rng_f01(&rng) * BOX - HALF;
        float spd = 14.0f + rng_f01(&rng) * 5.0f;

        float wx = base_x + rx;
        float wz = base_z + rz;
        if (wx - cam->pos.x >  HALF) wx -= BOX;
        if (wx - cam->pos.x < -HALF) wx += BOX;
        if (wz - cam->pos.z >  HALF) wz -= BOX;
        if (wz - cam->pos.z < -HALF) wz += BOX;

        float wy = H - fmodf(ry0 + time_s * spd, H);
        V3 p0 = v3(wx, wy, wz);
        V3 p1 = v3(wx - 0.04f, wy - 0.44f, wz - 0.02f);
        render_line3d(fb, cam, p0, p1, 0x526b8cu, BLEND_QUARTER);
    }
}
