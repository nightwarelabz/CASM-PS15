/* CASM-PS15 renderer: world transform, per-vertex sodium/moon lighting,
 * near-plane clipping, vertex snapping, projection — feeding the
 * software rasterizer. */
#include "engine.h"

static void light_at(const LightRig *rig, V3 wp, V3 n,
                     float *lr, float *lg, float *lb) {
    V3 moon = v3_norm(v3(-0.35f, 0.8f, -0.45f));
    float md = fmaxf(0.0f, v3_dot(n, moon)) * 0.35f;
    float r = rig->amb_r + 0.45f * md;
    float g = rig->amb_g + 0.55f * md;
    float b = rig->amb_b + 0.95f * md;
    for (int i = 0; i < rig->nlights; i++) {
        const PointLight *L = &rig->lights[i];
        V3 to = v3_sub(L->pos, wp);
        float dist = v3_len(to);
        if (dist > L->range) continue;
        float fall = 1.0f - dist / L->range;
        fall *= fall;
        float nd = fmaxf(0.15f, v3_dot(n, v3_norm(to)));
        float s = fall * nd * 1.25f;
        r += L->r * s; g += L->g * s; b += L->b * s;
    }
    *lr = r; *lg = g; *lb = b;
}

typedef struct { float x, y, z, u, v, light, lr, lg, lb; } CVert;

static CVert cvert_lerp(CVert a, CVert b, float t) {
    CVert o;
    o.x = lerpf(a.x, b.x, t); o.y = lerpf(a.y, b.y, t); o.z = lerpf(a.z, b.z, t);
    o.u = lerpf(a.u, b.u, t); o.v = lerpf(a.v, b.v, t);
    o.light = lerpf(a.light, b.light, t);
    o.lr = lerpf(a.lr, b.lr, t); o.lg = lerpf(a.lg, b.lg, t); o.lb = lerpf(a.lb, b.lb, t);
    return o;
}

static void render_verts(FB *fb, const Camera *cam,
                         const MVert *verts, const MTri *tris, int nt,
                         V3 pos, float yaw, const Tex *tex,
                         float tr, float tg, float tb, const LightRig *rig) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float ccam = cosf(cam->yaw), scam = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    const float F = 215.0f, NEAR = 0.6f;
    for (int t = 0; t < nt; t++) {
        const int idx[3] = { tris[t].a, tris[t].b, tris[t].c };
        CVert in[3];
        for (int i = 0; i < 3; i++) {
            const MVert *v = &verts[idx[i]];
            V3 rp = v3(cy * v->p.x + sy * v->p.z, v->p.y, -sy * v->p.x + cy * v->p.z);
            V3 wp = v3_add(rp, pos);
            V3 rn = v3(cy * v->n.x + sy * v->n.z, v->n.y, -sy * v->n.x + cy * v->n.z);
            float vr, vg, vb;
            light_at(rig, wp, rn, &vr, &vg, &vb);
            V3 d = v3_sub(wp, cam->pos);
            float vx = ccam * d.x - scam * d.z;
            float vz = scam * d.x + ccam * d.z;
            float py = cp * d.y - sp * vz;
            float pz = sp * d.y + cp * vz;
            in[i] = (CVert){ vx, py, pz, v->u, v->v, (vr + vg + vb) / 3.0f, vr, vg, vb };
        }
        /* clip polygon against the near plane (Sutherland–Hodgman) */
        CVert poly[5];
        int np = 0;
        for (int i = 0; i < 3; i++) {
            CVert cur = in[i], prv = in[(i + 2) % 3];
            bool cur_in = cur.z >= NEAR, prv_in = prv.z >= NEAR;
            if (cur_in != prv_in) {
                float tt = (NEAR - prv.z) / (cur.z - prv.z);
                poly[np++] = cvert_lerp(prv, cur, tt);
            }
            if (cur_in) poly[np++] = cur;
        }
        if (np < 3) continue;
        float lr = (poly[0].lr + poly[1].lr + poly[2].lr) / 3;
        float lg = (poly[0].lg + poly[1].lg + poly[2].lg) / 3;
        float lb = (poly[0].lb + poly[1].lb + poly[2].lb) / 3;
        float avg = (lr + lg + lb) / 3.0f;
        if (avg < 1e-4f) avg = 1e-4f;
        for (int k = 1; k < np - 1; k++) {
            const CVert *tri3[3] = { &poly[0], &poly[k], &poly[k + 1] };
            SVert sv[3];
            for (int i = 0; i < 3; i++) {
                int px = (int)lrintf(FB_W * 0.5f + tri3[i]->x * F / tri3[i]->z);
                int py = (int)lrintf(FB_H * 0.5f - tri3[i]->y * F / tri3[i]->z);
                sv[i] = (SVert){ (float)px, (float)py, tri3[i]->z,
                                 tri3[i]->u, tri3[i]->v, tri3[i]->light };
            }
            float fogz = (sv[0].z + sv[1].z + sv[2].z) / 3.0f;
            raster_tri(fb, sv[0], sv[1], sv[2],
                       tex ? tex->px : NULL, tex ? tex->w : 0, tex ? tex->h : 0,
                       tr * lr / avg, tg * lg / avg, tb * lb / avg, fogz);
        }
    }
}

void render_mesh(FB *fb, const Camera *cam, const Mesh *m,
                 V3 pos, float yaw, const Tex *tex,
                 float tr, float tg, float tb, const LightRig *rig) {
    render_verts(fb, cam, m->verts, m->tris, m->nt, pos, yaw, tex, tr, tg, tb, rig);
}

static void rot_x(V3 *p, float ang) {
    float c = cosf(ang), s = sinf(ang);
    float y = p->y, z = p->z;
    p->y = c * y - s * z;
    p->z = s * y + c * z;
}

void render_humanoid(FB *fb, const Camera *cam, const Humanoid *h,
                     V3 pos, float yaw, const Pose *pose,
                     const Tex *texset, float tr, float tg, float tb,
                     const LightRig *rig) {
    const struct { V3 joint; float ang; } J[PART_COUNT] = {
        { v3(0, 0.84f, 0), 0 },
        { v3(0, 1.00f, 0), pose->lean },
        { v3(0, 1.58f, 0), pose->lean },
        { v3(-0.28f, 1.52f, 0), pose->arm_l + pose->lean },
        { v3( 0.28f, 1.52f, 0), pose->arm_r + pose->lean },
        { v3(-0.11f, 0.80f, 0), pose->leg_l },
        { v3( 0.11f, 0.80f, 0), pose->leg_r },
    };
    for (int part = 0; part < PART_COUNT; part++) {
        const Mesh *m = &h->parts[part];
        MVert tmp[24];
        for (int i = 0; i < m->nv; i++) {
            tmp[i] = m->verts[i];
            V3 p = tmp[i].p;
            if (part == PART_ARM_L || part == PART_ARM_R) {
                p.y -= 0.60f;
                rot_x(&p, J[part].ang);
            } else if (part == PART_LEG_L || part == PART_LEG_R) {
                p.y -= 0.76f;
                rot_x(&p, J[part].ang);
            } else if (J[part].ang != 0) {
                rot_x(&p, J[part].ang);
            }
            tmp[i].p = v3_add(p, J[part].joint);
        }
        const Tex *part_tex = (part == PART_HEAD) ? &texset[TEX_FACE]
                                                  : &texset[TEX_CAMO];
        render_verts(fb, cam, tmp, m->tris, m->nt, pos, yaw, part_tex, tr, tg, tb, rig);
    }
}
