/* CASM-PS15 mesh builders: world-tiled subdivided architecture, cones, quads,
 * sculpted 11-part PS1.5 tactical espionage operator & guard, and first-person
 * tactical pistol viewmodel. */
#include "engine.h"

void mesh_compute_bounds(Mesh *m) {
    if (!m || m->nv <= 0) {
        if (m) { m->center = v3(0, 0, 0); m->radius = 0; }
        return;
    }
    V3 mn = m->verts[0].p, mx = m->verts[0].p;
    for (int i = 1; i < m->nv; i++) {
        V3 p = m->verts[i].p;
        if (p.x < mn.x) mn.x = p.x;
        if (p.y < mn.y) mn.y = p.y;
        if (p.z < mn.z) mn.z = p.z;
        if (p.x > mx.x) mx.x = p.x;
        if (p.y > mx.y) mx.y = p.y;
        if (p.z > mx.z) mx.z = p.z;
    }
    m->center = v3_scale(v3_add(mn, mx), 0.5f);
    float r2 = 0.0f;
    for (int i = 0; i < m->nv; i++) {
        V3 d = v3_sub(m->verts[i].p, m->center);
        float l2 = v3_dot(d, d);
        if (l2 > r2) r2 = l2;
    }
    m->radius = sqrtf(r2);
}

static Mesh mesh_alloc(int nv, int nt) {
    Mesh m;
    m.verts = calloc((size_t)(nv > 0 ? nv : 1), sizeof(MVert));
    m.tris  = calloc((size_t)(nt > 0 ? nt : 1), sizeof(MTri));
    m.nv = nv; m.nt = nt;
    m.center = v3(0, 0, 0); m.radius = 0;
    return m;
}

void mesh_free(Mesh *m) {
    if (!m) return;
    free(m->verts); free(m->tris);
    m->verts = NULL; m->tris = NULL;
    m->nv = m->nt = 0;
}

/* PS2-grade vertex normal smoothing across curved polygon boundaries
 * while preserving sharp mechanical edges above crease_angle_rad */
void mesh_smooth_normals(Mesh *m, float crease_angle_rad) {
    if (!m || m->nv <= 0 || m->nt <= 0) return;
    float cos_crease = cosf(crease_angle_rad);

    V3 *tri_n = malloc(sizeof(V3) * (size_t)m->nt);
    float *tri_w = malloc(sizeof(float) * (size_t)m->nt);
    if (!tri_n || !tri_w) {
        free(tri_n); free(tri_w);
        return;
    }

    for (int t = 0; t < m->nt; t++) {
        V3 p0 = m->verts[m->tris[t].a].p;
        V3 p1 = m->verts[m->tris[t].b].p;
        V3 p2 = m->verts[m->tris[t].c].p;
        V3 cp = v3_cross(v3_sub(p1, p0), v3_sub(p2, p0));
        float len = v3_len(cp);
        tri_w[t] = len;
        tri_n[t] = len > 1e-6f ? v3_scale(cp, 1.0f / len) : v3(0, 1, 0);
    }

    V3 *new_n = malloc(sizeof(V3) * (size_t)m->nv);
    if (!new_n) {
        free(tri_n); free(tri_w);
        return;
    }

    for (int i = 0; i < m->nv; i++) {
        V3 pi = m->verts[i].p;
        V3 orig_n = m->verts[i].n;
        V3 accum = v3(0, 0, 0);
        float total_w = 0.0f;

        for (int t = 0; t < m->nt; t++) {
            int ia = m->tris[t].a, ib = m->tris[t].b, ic = m->tris[t].c;
            V3 pa = m->verts[ia].p, pb = m->verts[ib].p, pc = m->verts[ic].p;
            V3 da = v3_sub(pi, pa), db = v3_sub(pi, pb), dc = v3_sub(pi, pc);
            bool shares = (v3_dot(da, da) < 1e-6f) ||
                          (v3_dot(db, db) < 1e-6f) ||
                          (v3_dot(dc, dc) < 1e-6f);
            if (!shares) continue;

            float dot = v3_dot(orig_n, tri_n[t]);
            if (dot >= cos_crease) {
                float w = tri_w[t];
                accum = v3_add(accum, v3_scale(tri_n[t], w));
                total_w += w;
            }
        }

        if (total_w > 1e-6f && v3_dot(accum, accum) > 1e-6f) {
            new_n[i] = v3_norm(accum);
        } else {
            new_n[i] = orig_n;
        }
    }

    for (int i = 0; i < m->nv; i++) {
        m->verts[i].n = new_n[i];
    }

    free(tri_n);
    free(tri_w);
    free(new_n);
}

/* ---------- growable low-poly sculpting builder ----------------------- */
typedef struct {
    MVert *v; int nv, cap_v;
    MTri  *t; int nt, cap_t;
} MBuilder;

static void mb_init(MBuilder *b) {
    b->cap_v = 64; b->cap_t = 32;
    b->nv = 0; b->nt = 0;
    b->v = malloc(sizeof(MVert) * (size_t)b->cap_v);
    b->t = malloc(sizeof(MTri) * (size_t)b->cap_t);
}

static int mb_vert(MBuilder *b, V3 p, float u, float v, V3 n) {
    if (b->nv >= b->cap_v) {
        b->cap_v *= 2;
        b->v = realloc(b->v, sizeof(MVert) * (size_t)b->cap_v);
    }
    b->v[b->nv] = (MVert){ p, u, v, n };
    return b->nv++;
}

static void mb_tri(MBuilder *b, int i0, int i1, int i2) {
    if (b->nt >= b->cap_t) {
        b->cap_t *= 2;
        b->t = realloc(b->t, sizeof(MTri) * (size_t)b->cap_t);
    }
    b->t[b->nt++] = (MTri){ i0, i1, i2 };
}

/* Add a flat-shaded CCW quad (p0 bottom-left, p1 bottom-right, p2 top-right, p3 top-left) */
static void mb_quad(MBuilder *b, V3 p0, V3 p1, V3 p2, V3 p3,
                    float u0, float v0, float u1, float v1) {
    V3 n = v3_norm(v3_cross(v3_sub(p1, p0), v3_sub(p2, p0)));
    int base = mb_vert(b, p0, u0, v0, n);
    mb_vert(b, p1, u1, v0, n);
    mb_vert(b, p2, u1, v1, n);
    mb_vert(b, p3, u0, v1, n);
    mb_tri(b, base, base + 1, base + 2);
    mb_tri(b, base, base + 2, base + 3);
}

/* Sculpted tapered 6-face prism (frustum box) with per-face UV rect */
typedef struct { float u0, v0, u1, v1; } UVRect;

static void mb_frustum(MBuilder *b, V3 c0, float sx0, float sz0,
                       V3 c1, float sx1, float sz1,
                       UVRect front_uv, UVRect side_uv, UVRect back_uv, UVRect cap_uv) {
    float hx0 = sx0 * 0.5f, hz0 = sz0 * 0.5f;
    float hx1 = sx1 * 0.5f, hz1 = sz1 * 0.5f;
    V3 b00 = v3(c0.x - hx0, c0.y, c0.z + hz0); /* +z left  */
    V3 b10 = v3(c0.x + hx0, c0.y, c0.z + hz0); /* +z right */
    V3 b11 = v3(c0.x + hx0, c0.y, c0.z - hz0); /* -z right */
    V3 b01 = v3(c0.x - hx0, c0.y, c0.z - hz0); /* -z left  */

    V3 t00 = v3(c1.x - hx1, c1.y, c1.z + hz1);
    V3 t10 = v3(c1.x + hx1, c1.y, c1.z + hz1);
    V3 t11 = v3(c1.x + hx1, c1.y, c1.z - hz1);
    V3 t01 = v3(c1.x - hx1, c1.y, c1.z - hz1);

    /* +Z front, -Z back, +X right, -X left, +Y top, -Y bottom */
    mb_quad(b, b00, b10, t10, t00, front_uv.u0, front_uv.v0, front_uv.u1, front_uv.v1);
    mb_quad(b, b11, b01, t01, t11, back_uv.u0,  back_uv.v0,  back_uv.u1,  back_uv.v1);
    mb_quad(b, b10, b11, t11, t10, side_uv.u0,  side_uv.v0,  side_uv.u1,  side_uv.v1);
    mb_quad(b, b01, b00, t00, t01, side_uv.u0,  side_uv.v0,  side_uv.u1,  side_uv.v1);
    mb_quad(b, t00, t10, t11, t01, cap_uv.u0,   cap_uv.v0,   cap_uv.u1,   cap_uv.v1);
    mb_quad(b, b01, b11, b10, b00, cap_uv.u0,   cap_uv.v0,   cap_uv.u1,   cap_uv.v1);
}

/* Oriented 3D box (right-handed basis ax_r x ax_u = ax_f) for angled weapons & gear */
static V3 obb_pt(V3 c, V3 r, V3 u, V3 f, float xr, float yu, float zf) {
    return v3(c.x + r.x * xr + u.x * yu + f.x * zf,
              c.y + r.y * xr + u.y * yu + f.y * zf,
              c.z + r.z * xr + u.z * yu + f.z * zf);
}

static void mb_obb(MBuilder *b, V3 center, V3 ax_r, V3 ax_u, V3 ax_f,
                   float sx, float sy, float sz,
                   UVRect front_uv, UVRect side_uv, UVRect top_uv, UVRect bot_uv) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    V3 f00 = obb_pt(center, ax_r, ax_u, ax_f, -hx, -hy,  hz);
    V3 f10 = obb_pt(center, ax_r, ax_u, ax_f,  hx, -hy,  hz);
    V3 f11 = obb_pt(center, ax_r, ax_u, ax_f,  hx,  hy,  hz);
    V3 f01 = obb_pt(center, ax_r, ax_u, ax_f, -hx,  hy,  hz);

    V3 k00 = obb_pt(center, ax_r, ax_u, ax_f, -hx, -hy, -hz);
    V3 k10 = obb_pt(center, ax_r, ax_u, ax_f,  hx, -hy, -hz);
    V3 k11 = obb_pt(center, ax_r, ax_u, ax_f,  hx,  hy, -hz);
    V3 k01 = obb_pt(center, ax_r, ax_u, ax_f, -hx,  hy, -hz);

    mb_quad(b, f00, f10, f11, f01, front_uv.u0, front_uv.v0, front_uv.u1, front_uv.v1);
    mb_quad(b, k10, k00, k01, k11, side_uv.u0,  side_uv.v0,  side_uv.u1,  side_uv.v1);
    mb_quad(b, f10, k10, k11, f11, side_uv.u0,  side_uv.v0,  side_uv.u1,  side_uv.v1);
    mb_quad(b, k00, f00, f01, k01, side_uv.u0,  side_uv.v0,  side_uv.u1,  side_uv.v1);
    mb_quad(b, f01, f11, k11, k01, top_uv.u0,   top_uv.v0,   top_uv.u1,   top_uv.v1);
    mb_quad(b, k00, k10, f10, f00, bot_uv.u0,   bot_uv.v0,   bot_uv.u1,   bot_uv.v1);
}

/* Octagonal sculpted ring between y0 and y1 (great for cranium, suppressor, scopes) */
static void mb_oct_ring(MBuilder *b, V3 c0, float rx0, float rz0,
                        V3 c1, float rx1, float rz1,
                        UVRect front_uv, UVRect other_uv, bool cap_top, bool cap_bot) {
    /* 8 angles starting at -22.5 deg so segment 0 faces straight +Z! */
    V3 p0[8], p1[8];
    for (int i = 0; i < 8; i++) {
        float ang = (-0.3926991f) + (float)i * (PS15_PI * 0.25f);
        /* i=0..1 spans x: -sin(22.5)..+sin(22.5) at z = +cos(22.5) (front +Z!) */
        float sa = sinf(ang), ca = cosf(ang);
        p0[i] = v3(c0.x + sa * rx0, c0.y, c0.z + ca * rz0);
        p1[i] = v3(c1.x + sa * rx1, c1.y, c1.z + ca * rz1);
    }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        /* Front 3 facets (7, 0, 1) map smoothly across front_uv (visor/brow/mask) */
        if (i == 7 || i == 0 || i == 1) {
            int slot = (i == 7) ? 0 : (i == 0 ? 1 : 2);
            /* Note: p0[7] is +X front-right, p0[0] is -X? Wait:
             * ang(0) = -22.5 deg -> sin < 0 (-X), cos > 0 (+Z)
             * ang(1) = +22.5 deg -> sin > 0 (+X), cos > 0 (+Z)
             * So segment i=0 goes (-X,+Z) -> (+X,+Z): straight front!
             * Segment i=7 goes (-X_cheek) -> (-X,+Z): left cheek!
             * Segment i=1 goes (+X,+Z) -> (+X_cheek): right cheek! */
            float u_a = lerpf(front_uv.u0, front_uv.u1, (float)slot / 3.0f);
            float u_b = lerpf(front_uv.u0, front_uv.u1, (float)(slot + 1) / 3.0f);
            mb_quad(b, p0[i], p0[j], p1[j], p1[i], u_a, front_uv.v0, u_b, front_uv.v1);
        } else {
            mb_quad(b, p0[i], p0[j], p1[j], p1[i],
                    other_uv.u0, other_uv.v0, other_uv.u1, other_uv.v1);
        }
    }
    if (cap_top) {
        V3 n = v3(0, 1, 0);
        int c = mb_vert(b, v3(c1.x, c1.y, c1.z),
                        0.5f * (other_uv.u0 + other_uv.u1),
                        0.5f * (other_uv.v0 + other_uv.v1), n);
        int ring[8];
        for (int i = 0; i < 8; i++)
            ring[i] = mb_vert(b, p1[i], other_uv.u0, other_uv.v0, n);
        for (int i = 0; i < 8; i++)
            mb_tri(b, c, ring[i], ring[(i + 1) & 7]);
    }
    if (cap_bot) {
        V3 n = v3(0, -1, 0);
        int c = mb_vert(b, v3(c0.x, c0.y, c0.z),
                        0.5f * (other_uv.u0 + other_uv.u1),
                        0.5f * (other_uv.v0 + other_uv.v1), n);
        int ring[8];
        for (int i = 0; i < 8; i++)
            ring[i] = mb_vert(b, p0[i], other_uv.u0, other_uv.v0, n);
        for (int i = 0; i < 8; i++)
            mb_tri(b, c, ring[(i + 1) & 7], ring[i]);
    }
}

static Mesh mb_finish(MBuilder *b) {
    Mesh m;
    m.nv = b->nv; m.nt = b->nt;
    m.verts = b->v; m.tris = b->t;
    mesh_compute_bounds(&m);
    return m;
}

/* ---------- standard architectural & primitive mesh builders ---------- */
Mesh mesh_box(float sx, float sy, float sz) {
    Mesh m = mesh_alloc(24, 12);
    float hx = sx * 0.5f, hz = sz * 0.5f;
    const float F[6][4][3] = {
        { {-hx,0, hz}, { hx,0, hz}, { hx,sy, hz}, {-hx,sy, hz} },
        { { hx,0,-hz}, {-hx,0,-hz}, {-hx,sy,-hz}, { hx,sy,-hz} },
        { { hx,0, hz}, { hx,0,-hz}, { hx,sy,-hz}, { hx,sy, hz} },
        { {-hx,0,-hz}, {-hx,0, hz}, {-hx,sy, hz}, {-hx,sy,-hz} },
        { {-hx,sy, hz}, { hx,sy, hz}, { hx,sy,-hz}, {-hx,sy,-hz} },
        { {-hx,0,-hz}, { hx,0,-hz}, { hx,0, hz}, {-hx,0, hz} },
    };
    const float N[6][3] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    const float UV[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    for (int f = 0; f < 6; f++) {
        for (int i = 0; i < 4; i++) {
            MVert *v = &m.verts[f * 4 + i];
            v->p = v3(F[f][i][0], F[f][i][1], F[f][i][2]);
            v->u = UV[i][0]; v->v = UV[i][1];
            v->n = v3(N[f][0], N[f][1], N[f][2]);
        }
        m.tris[f * 2]     = (MTri){ f * 4, f * 4 + 1, f * 4 + 2 };
        m.tris[f * 2 + 1] = (MTri){ f * 4, f * 4 + 2, f * 4 + 3 };
    }
    mesh_compute_bounds(&m);
    return m;
}

/* Subdivided, world-space UV-tiled box for crisp texels & Gouraud light pools */
Mesh mesh_box_tiled(float sx, float sy, float sz, float tile_m, float subdiv_m) {
    if (tile_m <= 0.01f) tile_m = 2.0f;
    if (subdiv_m <= 0.05f) subdiv_m = 2.0f;
    MBuilder b;
    mb_init(&b);
    float hx = sx * 0.5f, hz = sz * 0.5f;

    struct {
        V3 org, axis_u, axis_v, norm;
        float len_u, len_v;
    } faces[6] = {
        { v3(-hx, 0,  hz), v3(1,0,0),  v3(0,1,0),  v3(0,0,1),  sx, sy }, /* +Z */
        { v3( hx, 0, -hz), v3(-1,0,0), v3(0,1,0),  v3(0,0,-1), sx, sy }, /* -Z */
        { v3( hx, 0,  hz), v3(0,0,-1), v3(0,1,0),  v3(1,0,0),  sz, sy }, /* +X */
        { v3(-hx, 0, -hz), v3(0,0,1),  v3(0,1,0),  v3(-1,0,0), sz, sy }, /* -X */
        { v3(-hx, sy, hz), v3(1,0,0),  v3(0,0,-1), v3(0,1,0),  sx, sz }, /* +Y */
        { v3(-hx, 0, -hz), v3(1,0,0),  v3(0,0,1),  v3(0,-1,0), sx, sz }, /* -Y */
    };

    for (int f = 0; f < 6; f++) {
        int nu = (int)ceilf(faces[f].len_u / subdiv_m); if (nu < 1) nu = 1;
        int nv = (int)ceilf(faces[f].len_v / subdiv_m); if (nv < 1) nv = 1;
        int base = b.nv;
        for (int iy = 0; iy <= nv; iy++) {
            float fv = (float)iy / (float)nv;
            float dist_v = fv * faces[f].len_v;
            for (int ix = 0; ix <= nu; ix++) {
                float fu = (float)ix / (float)nu;
                float dist_u = fu * faces[f].len_u;
                V3 p = v3_add(faces[f].org,
                              v3_add(v3_scale(faces[f].axis_u, dist_u),
                                     v3_scale(faces[f].axis_v, dist_v)));
                mb_vert(&b, p, dist_u / tile_m, dist_v / tile_m, faces[f].norm);
            }
        }
        int stride = nu + 1;
        for (int iy = 0; iy < nv; iy++) {
            for (int ix = 0; ix < nu; ix++) {
                int i0 = base + iy * stride + ix;
                int i1 = i0 + 1;
                int i2 = i0 + stride + 1;
                int i3 = i0 + stride;
                mb_tri(&b, i0, i1, i2);
                mb_tri(&b, i0, i2, i3);
            }
        }
    }
    return mb_finish(&b);
}

Mesh mesh_plane_subdiv(float sx, float sz, float tile, int divs) {
    if (divs < 1) divs = 1;
    int stride = divs + 1;
    Mesh m = mesh_alloc(stride * stride, divs * divs * 2);
    float hx = sx * 0.5f, hz = sz * 0.5f;
    for (int iz = 0; iz <= divs; iz++) {
        float tz = (float)iz / (float)divs;
        for (int ix = 0; ix <= divs; ix++) {
            float tx = (float)ix / (float)divs;
            MVert *v = &m.verts[iz * stride + ix];
            v->p = v3(-hx + tx * sx, 0.0f, -hz + tz * sz);
            v->u = tx * tile;
            v->v = tz * tile;
            v->n = v3(0, 1, 0);
        }
    }
    int ti = 0;
    for (int iz = 0; iz < divs; iz++) {
        for (int ix = 0; ix < divs; ix++) {
            int i0 = iz * stride + ix;
            int i1 = i0 + 1;
            int i2 = i0 + stride + 1;
            int i3 = i0 + stride;
            /* CCW upward (+Y): (i0 -> i2 -> i1) and (i0 -> i3 -> i2) */
            m.tris[ti++] = (MTri){ i0, i2, i1 };
            m.tris[ti++] = (MTri){ i0, i3, i2 };
        }
    }
    mesh_compute_bounds(&m);
    return m;
}

Mesh mesh_plane(float sx, float sz, float tile) {
    /* Default tessellated ground grid so PS1.5 vertex lights paint warm pools */
    int divs = (sx > 20.0f || sz > 20.0f) ? 32 : 4;
    return mesh_plane_subdiv(sx, sz, tile, divs);
}

Mesh mesh_quad_xz(float sx, float sz) {
    return mesh_plane_subdiv(sx, sz, 1.0f, 1);
}

/* Open/capped frustum cone from y=0 (r_bot) to y=h (r_top) for light beams */
Mesh mesh_cone(float r_top, float r_bot, float h, int segs, bool caps) {
    if (segs < 4) segs = 4;
    MBuilder b;
    mb_init(&b);
    for (int i = 0; i < segs; i++) {
        float a0 = ((float)i / (float)segs) * 2.0f * PS15_PI;
        float a1 = ((float)(i + 1) / (float)segs) * 2.0f * PS15_PI;
        V3 b0 = v3(sinf(a0) * r_bot, 0.0f, cosf(a0) * r_bot);
        V3 b1 = v3(sinf(a1) * r_bot, 0.0f, cosf(a1) * r_bot);
        V3 t1 = v3(sinf(a1) * r_top, h,    cosf(a1) * r_top);
        V3 t0 = v3(sinf(a0) * r_top, h,    cosf(a0) * r_top);
        mb_quad(&b, b0, b1, t1, t0,
                (float)i / (float)segs, 0.0f, (float)(i + 1) / (float)segs, 1.0f);
        if (caps && r_top > 1e-4f) {
            V3 n = v3(0, 1, 0);
            int c = mb_vert(&b, v3(0, h, 0), 0.5f, 0.5f, n);
            int v0 = mb_vert(&b, t0, 0, 0, n);
            int v1 = mb_vert(&b, t1, 1, 0, n);
            mb_tri(&b, c, v0, v1);
        }
    }
    return mb_finish(&b);
}

static V3 m_rot_x(V3 p, float a) {
    float c = cosf(a), s = sinf(a);
    return v3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}
static V3 m_rot_z(V3 p, float a) {
    float c = cosf(a), s = sinf(a);
    return v3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}
/* Exact inverse of render_humanoid's xform_chain(v, 0, rz, rx) */
static V3 inv_limb(V3 w, float rz, float rx) {
    return m_rot_z(m_rot_x(w, -rx), -rz);
}

/* ---------- sculpted 11-part PS1.5 espionage operator & patrol guard -- */
static Mesh build_operator_head(bool guard) {
    MBuilder b;
    mb_init(&b);
    if (!guard) {
        UVRect camo  = { 0.0f, 0.78f, 0.36f, 0.98f };
        UVRect lower = { 0.20f, 0.08f, 0.80f, 0.37f }; /* balaclava jaw/mask */
        UVRect mid   = { 0.20f, 0.38f, 0.80f, 0.77f }; /* visor + pixel-art brow */
        UVRect crown = { 0.20f, 0.77f, 0.80f, 0.98f }; /* camo hood dome */
        UVRect mask_l = { 0.20f, 0.08f, 0.50f, 0.37f };
        UVRect mask_r = { 0.50f, 0.08f, 0.80f, 0.37f };

        /* Neck & lower balaclava jaw ring */
        mb_oct_ring(&b, v3(0, 0.00f, -0.006f), 0.076f, 0.080f,
                        v3(0, 0.106f, 0.004f), 0.112f, 0.118f,
                        lower, camo, false, true);
        /* Sculpted V-ridged balaclava nose-bridge & chin facets on +Z front */
        V3 ml0 = v3(-0.088f, 0.010f, 0.070f), mc0 = v3(0.0f, 0.004f, 0.118f), mr0 = v3(0.088f, 0.010f, 0.070f);
        V3 ml1 = v3(-0.108f, 0.110f, 0.094f), mc1 = v3(0.0f, 0.110f, 0.150f), mr1 = v3(0.108f, 0.110f, 0.094f);
        mb_quad(&b, ml0, mc0, mc1, ml1, mask_l.u0, mask_l.v0, mask_l.u1, mask_l.v1);
        mb_quad(&b, mc0, mr0, mr1, mc1, mask_r.u0, mask_r.v0, mask_r.u1, mask_r.v1);

        /* Mid-cranium: exposed warm pixel-art forehead brow & goggle socket */
        mb_oct_ring(&b, v3(0, 0.104f, 0.006f), 0.116f, 0.126f,
                        v3(0, 0.220f, 0.012f), 0.124f, 0.138f,
                        mid, camo, false, false);

        /* Sculpted 3-tier faceted pixel-camo hood cranium + menacing V-brow scowl lip */
        mb_oct_ring(&b, v3(0, 0.214f, 0.014f), 0.128f, 0.144f,
                        v3(0, 0.258f, 0.004f), 0.118f, 0.130f,
                        crown, camo, false, false);
        mb_oct_ring(&b, v3(0, 0.258f, 0.004f), 0.118f, 0.130f,
                        v3(0, 0.292f, -0.006f), 0.092f, 0.102f,
                        crown, camo, false, false);
        mb_oct_ring(&b, v3(0, 0.292f, -0.006f), 0.092f, 0.102f,
                        v3(0, 0.312f, -0.012f), 0.056f, 0.064f,
                        crown, camo, true, false);
        /* Overhanging V-angled front hood brow ridge casting subtle scowl over forehead */
        V3 hl0 = v3(-0.116f, 0.200f, 0.106f), hc0 = v3(0.0f, 0.188f, 0.148f), hr0 = v3(0.116f, 0.200f, 0.106f);
        V3 hl1 = v3(-0.118f, 0.234f, 0.108f), hc1 = v3(0.0f, 0.228f, 0.150f), hr1 = v3(0.118f, 0.234f, 0.108f);
        mb_quad(&b, hl0, hc0, hc1, hl1, camo.u0, camo.v0, camo.u1, camo.v1);
        mb_quad(&b, hc0, hr0, hr1, hc1, camo.u0, camo.v0, camo.u1, camo.v1);

        /* 3D Extruded V-Curved Ballistic Goggles (smoked-ruby mosaic + pinpoint red LED) */
        UVRect lens_l = { 0.23f, 0.40f, 0.50f, 0.59f };
        UVRect lens_r = { 0.50f, 0.40f, 0.77f, 0.59f };
        UVRect frame  = { 0.23f, 0.39f, 0.31f, 0.42f };
        UVRect mic_uv = { 0.06f, 0.06f, 0.13f, 0.13f };
        V3 gl0 = v3(-0.118f, 0.106f, 0.098f), gc0 = v3(0.0f, 0.096f, 0.156f), gr0 = v3(0.118f, 0.106f, 0.098f);
        V3 gl1 = v3(-0.122f, 0.166f, 0.108f), gc1 = v3(0.0f, 0.158f, 0.164f), gr1 = v3(0.122f, 0.166f, 0.108f);
        V3 bl0 = v3(-0.118f, 0.106f, 0.056f), br0 = v3(0.118f, 0.106f, 0.056f);
        V3 bl1 = v3(-0.122f, 0.166f, 0.056f), br1 = v3(0.122f, 0.166f, 0.056f);
        mb_quad(&b, gl0, gc0, gc1, gl1, lens_l.u0, lens_l.v0, lens_l.u1, lens_l.v1);
        mb_quad(&b, gc0, gr0, gr1, gc1, lens_r.u0, lens_r.v0, lens_r.u1, lens_r.v1);
        mb_quad(&b, gl1, gr1, br1, bl1, frame.u0, frame.v0, frame.u1, frame.v1);
        mb_quad(&b, bl0, gl0, gl1, bl1, frame.u0, frame.v0, frame.u1, frame.v1);
        mb_quad(&b, gr0, br0, br1, gr1, frame.u0, frame.v0, frame.u1, frame.v1);
        /* Matte polymer upper goggle brow bar */
        mb_frustum(&b, v3(0.0f, 0.160f, 0.126f), 0.242f, 0.058f,
                       v3(0.0f, 0.168f, 0.126f), 0.238f, 0.056f,
                       frame, frame, frame, frame);

        /* Tactical Comms Headset: beveled ear-cups + yoke + curved gunmetal boom mic */
        mb_frustum(&b, v3(-0.132f, 0.096f, -0.004f), 0.048f, 0.088f,
                       v3(-0.136f, 0.198f, -0.004f), 0.044f, 0.082f,
                       frame, frame, frame, frame);
        mb_frustum(&b, v3( 0.132f, 0.096f, -0.004f), 0.048f, 0.088f,
                       v3( 0.136f, 0.198f, -0.004f), 0.044f, 0.082f,
                       frame, frame, frame, frame);
        mb_frustum(&b, v3(0.0f, 0.292f, -0.006f), 0.22f, 0.032f,
                       v3(0.0f, 0.312f, -0.006f), 0.18f, 0.030f,
                       frame, frame, frame, frame);
        /* Headset headband linking ear-cups across cranium rear (operator-rear-reference.png) */
        mb_frustum(&b, v3(0.0f, 0.144f, -0.126f), 0.232f, 0.028f,
                       v3(0.0f, 0.166f, -0.126f), 0.228f, 0.026f,
                       frame, frame, frame, frame);
        /* Curved gunmetal boom-mic wire + dark foam wind-tip capsule */
        V3 bm_f = v3_norm(v3(0.104f, -0.056f, 0.146f));
        V3 bm_r = v3_norm(v3_cross(v3(0, 1, 0), bm_f));
        V3 bm_u = v3_cross(bm_f, bm_r);
        mb_obb(&b, v3(-0.088f, 0.082f, 0.078f), bm_r, bm_u, bm_f,
               0.010f, 0.010f, 0.176f, mic_uv, mic_uv, mic_uv, frame);
        mb_obb(&b, v3(-0.038f, 0.056f, 0.148f), bm_r, bm_u, bm_f,
               0.018f, 0.018f, 0.034f, frame, frame, frame, frame);
    } else {
        /* Regime Trooper Head: exact replica of enemy-soldier.jpg & enemy-soldier-rear.jpg
         * Rounded ballistic helmet dome, brow rim, neck guard, goggle strap,
         * dark dual ballistic goggles, and protruding titanium respirator faceplate
         * with three distinct vertical breathing vent slits (|||) + beveled cheek plates */
        UVRect mask_plate = { 0.10f, 0.04f, 0.40f, 0.26f }; /* 3-slit titanium respirator */
        UVRect mask_l     = { 0.10f, 0.04f, 0.25f, 0.26f };
        UVRect mask_r     = { 0.25f, 0.04f, 0.40f, 0.26f };
        UVRect g_lens     = { 0.06f, 0.28f, 0.44f, 0.42f }; /* dark tinted ballistic goggles */
        UVRect g_frame    = { 0.04f, 0.40f, 0.16f, 0.44f };
        UVRect helmet     = { 0.04f, 0.36f, 0.24f, 0.46f }; /* dark ballistic helmet finish */

        /* Neck ring & lower balaclava foundation */
        mb_oct_ring(&b, v3(0, 0.00f, 0.0f), 0.076f, 0.080f,
                        v3(0, 0.10f, 0.004f), 0.108f, 0.114f,
                        helmet, helmet, false, true);
        mb_oct_ring(&b, v3(0, 0.10f, 0.004f), 0.112f, 0.118f,
                        v3(0, 0.17f, 0.006f), 0.122f, 0.128f,
                        helmet, helmet, false, false);

        /* 3-tier rounded ballistic helmet dome (enemy-soldier.jpg) */
        mb_oct_ring(&b, v3(0, 0.170f, 0.002f), 0.128f, 0.138f,
                        v3(0, 0.235f, -0.004f), 0.124f, 0.132f,
                        helmet, helmet, false, false);
        mb_oct_ring(&b, v3(0, 0.235f, -0.004f), 0.124f, 0.132f,
                        v3(0, 0.285f, -0.010f), 0.098f, 0.106f,
                        helmet, helmet, false, false);
        mb_oct_ring(&b, v3(0, 0.285f, -0.010f), 0.098f, 0.106f,
                        v3(0, 0.316f, -0.014f), 0.056f, 0.064f,
                        helmet, helmet, true, false);

        /* Horizontal overhanging front helmet brow rim / visor peak */
        mb_frustum(&b, v3(0.0f, 0.166f, 0.088f), 0.248f, 0.082f,
                       v3(0.0f, 0.182f, 0.088f), 0.254f, 0.086f,
                       helmet, helmet, helmet, helmet);
        /* Flared rear neck guard */
        mb_frustum(&b, v3(0.0f, 0.156f, -0.116f), 0.224f, 0.046f,
                       v3(0.0f, 0.174f, -0.122f), 0.238f, 0.052f,
                       helmet, helmet, helmet, helmet);

        /* Elastic goggle retention strap wrapping around helmet sides & rear */
        mb_frustum(&b, v3(-0.124f, 0.132f, 0.00f), 0.012f, 0.225f,
                       v3(-0.124f, 0.154f, 0.00f), 0.012f, 0.220f,
                       g_frame, g_frame, g_frame, g_frame);
        mb_frustum(&b, v3( 0.124f, 0.132f, 0.00f), 0.012f, 0.225f,
                       v3( 0.124f, 0.154f, 0.00f), 0.012f, 0.220f,
                       g_frame, g_frame, g_frame, g_frame);
        mb_frustum(&b, v3(0.0f, 0.132f, -0.124f), 0.238f, 0.012f,
                       v3(0.0f, 0.154f, -0.124f), 0.232f, 0.012f,
                       g_frame, g_frame, g_frame, g_frame);

        /* 3D Extruded Ballistic Goggles (dark tinted dual lenses + center bridge) */
        V3 gl0 = v3(-0.114f, 0.114f, 0.098f), gc0 = v3(0.0f, 0.110f, 0.134f), gr0 = v3(0.114f, 0.114f, 0.098f);
        V3 gl1 = v3(-0.116f, 0.164f, 0.104f), gc1 = v3(0.0f, 0.160f, 0.140f), gr1 = v3(0.116f, 0.164f, 0.104f);
        V3 bl0 = v3(-0.114f, 0.114f, 0.065f), br0 = v3(0.114f, 0.114f, 0.065f);
        V3 bl1 = v3(-0.116f, 0.164f, 0.065f), br1 = v3(0.116f, 0.164f, 0.065f);
        mb_quad(&b, gl0, gc0, gc1, gl1, g_lens.u0, g_lens.v0, g_lens.u1, g_lens.v1);
        mb_quad(&b, gc0, gr0, gr1, gc1, g_lens.u0, g_lens.v0, g_lens.u1, g_lens.v1);
        mb_quad(&b, gl1, gr1, br1, bl1, g_frame.u0, g_frame.v0, g_frame.u1, g_frame.v1);
        mb_quad(&b, bl0, gl0, gl1, bl1, g_frame.u0, g_frame.v0, g_frame.u1, g_frame.v1);
        mb_quad(&b, gr0, br0, br1, gr1, g_frame.u0, g_frame.v0, g_frame.u1, g_frame.v1);

        /* Sculpted Titanium Respirator Mask: forward-angled plate with 3 vertical slits */
        V3 r0 = v3(-0.066f, 0.012f, 0.112f), rc0 = v3(0.0f, 0.010f, 0.142f), r1 = v3(0.066f, 0.012f, 0.112f);
        V3 t0 = v3(-0.062f, 0.112f, 0.124f), tc0 = v3(0.0f, 0.112f, 0.154f), t1 = v3(0.062f, 0.112f, 0.124f);
        mb_quad(&b, r0, rc0, tc0, t0, mask_l.u0, mask_l.v0, mask_l.u1, mask_l.v1);
        mb_quad(&b, rc0, r1, t1, tc0, mask_r.u0, mask_r.v0, mask_r.u1, mask_r.v1);
        /* Beveled titanium cheek plates & chin */
        V3 ck_l = v3(-0.106f, 0.040f, 0.082f), ck_r = v3(0.106f, 0.040f, 0.082f);
        V3 ck_t_l = v3(-0.110f, 0.112f, 0.092f), ck_t_r = v3(0.110f, 0.112f, 0.092f);
        mb_quad(&b, ck_l, r0, t0, ck_t_l, mask_plate.u0, mask_plate.v0, mask_plate.u1, mask_plate.v1);
        mb_quad(&b, r1, ck_r, ck_t_r, t1, mask_plate.u0, mask_plate.v0, mask_plate.u1, mask_plate.v1);
        mb_quad(&b, ck_l, rc0, r0, ck_l, mask_plate.u0, mask_plate.v0, mask_plate.u1, mask_plate.v1);
        mb_quad(&b, rc0, ck_r, r1, rc0, mask_plate.u0, mask_plate.v0, mask_plate.u1, mask_plate.v1);
    }
    return mb_finish(&b);
}

static Mesh build_torso(bool guard) {
    MBuilder b;
    mb_init(&b);
    UVRect vest = guard ? (UVRect){ 0.52f, 0.52f, 0.78f, 0.78f }
                        : (UVRect){ 0.02f, 0.02f, 0.48f, 0.48f };
    UVRect camo = guard ? vest : (UVRect){ 0.50f, 0.02f, 0.68f, 0.48f };
    UVRect gun_dark  = { 0.04f, 0.52f, 0.46f, 0.72f };
    UVRect gun_steel = { 0.04f, 0.76f, 0.46f, 0.96f };

    /* Athletic V-taper torso + layered MOLLE plate carrier */
    mb_frustum(&b, v3(0, 0.00f, 0), 0.34f, 0.21f,
                   v3(0, 0.34f, 0), 0.42f, 0.25f,
                   vest, camo, vest, camo);
    mb_frustum(&b, v3(0, 0.34f, 0), 0.42f, 0.25f,
                   v3(0, 0.56f, 0), 0.46f, 0.24f,
                   vest, camo, vest, camo);
    /* Raised 3D MOLLE plate-carrier shoulder straps over clavicles */
    mb_frustum(&b, v3(-0.135f, 0.38f, 0.01f), 0.076f, 0.265f,
                   v3(-0.125f, 0.575f, 0.01f), 0.072f, 0.250f,
                   vest, vest, vest, vest);
    mb_frustum(&b, v3( 0.135f, 0.38f, 0.01f), 0.076f, 0.265f,
                   v3( 0.125f, 0.575f, 0.01f), 0.072f, 0.250f,
                   vest, vest, vest, vest);
    /* Raised tactical jacket / greatcoat collar */
    mb_frustum(&b, v3(0, 0.54f, 0.0f), guard ? 0.25f : 0.23f, 0.22f,
                   v3(0, guard ? 0.64f : 0.62f, 0.0f), guard ? 0.27f : 0.21f, 0.22f,
                   camo, camo, camo, camo);

    if (!guard) {
        /* Upper chest admin pouch + 4 sculpted individual 3D front mag pouches */
        mb_frustum(&b, v3(0.0f, 0.31f, 0.134f), 0.18f, 0.038f,
                       v3(0.0f, 0.43f, 0.134f), 0.17f, 0.036f,
                       vest, camo, camo, camo);
        for (int p = 0; p < 4; p++) {
            float px = -0.108f + (float)p * 0.072f;
            mb_frustum(&b, v3(px, 0.07f, 0.136f), 0.062f, 0.052f,
                           v3(px, 0.25f, 0.138f), 0.064f, 0.054f,
                           vest, gun_dark, camo, camo);
        }
        /* Center-back MOLLE plate-carrier utility/hydration pack + grab loop (operator-rear-reference.png) */
        mb_frustum(&b, v3( 0.024f, 0.13f, -0.150f), 0.185f, 0.070f,
                       v3( 0.024f, 0.47f, -0.148f), 0.180f, 0.068f,
                       vest, gun_dark, gun_dark, vest);
        mb_frustum(&b, v3( 0.024f, 0.47f, -0.148f), 0.084f, 0.022f,
                       v3( 0.024f, 0.505f, -0.148f), 0.078f, 0.020f,
                       gun_dark, gun_dark, gun_dark, gun_dark);

        /* Left-back Tactical Comms Radio Pack + Twin Unequal Antenna Whips (operator-model-sheet & rear) */
        mb_frustum(&b, v3(-0.126f, 0.12f, -0.156f), 0.096f, 0.076f,
                       v3(-0.134f, 0.58f, -0.156f), 0.092f, 0.074f,
                       gun_dark, gun_dark, gun_dark, vest);
        /* Shorter inner secondary dark gunmetal whip antenna */
        mb_oct_ring(&b, v3(-0.108f, 0.58f, -0.152f), 0.010f, 0.010f,
                        v3(-0.118f, 0.82f, -0.152f), 0.007f, 0.007f,
                        gun_dark, gun_dark, true, false);
        /* Tall segmented main whip antenna rising cleanly behind left shoulder */
        mb_oct_ring(&b, v3(-0.138f, 0.58f, -0.156f), 0.014f, 0.014f,
                        v3(-0.152f, 0.74f, -0.156f), 0.012f, 0.012f,
                        gun_dark, gun_dark, false, false);
        mb_oct_ring(&b, v3(-0.152f, 0.74f, -0.156f), 0.015f, 0.015f,
                        v3(-0.156f, 0.78f, -0.156f), 0.015f, 0.015f,
                        gun_steel, gun_steel, false, false);
        mb_oct_ring(&b, v3(-0.156f, 0.78f, -0.156f), 0.011f, 0.011f,
                        v3(-0.170f, 1.15f, -0.156f), 0.006f, 0.006f,
                        gun_dark, gun_dark, true, false);
    } else {
        /* Regime Trooper Torso: exact replica of enemy-soldier.jpg & enemy-soldier-rear.jpg
         * High flared greatcoat collar, double-breasted button studs along right flap,
         * shoulder harness webbing with metal adjustment buckles front & back,
         * and prominent 3-cell magazine chest rig with pointed flaps and pull-tabs! */
        UVRect coat_btn  = { 0.84f, 0.53f, 0.96f, 0.65f }; /* silver button stud in TEX_VEST */
        UVRect harness_b = { 0.84f, 0.68f, 0.96f, 0.80f }; /* metal harness buckle */
        UVRect pouch_tab = { 0.52f, 0.52f, 0.78f, 0.78f };

        /* High flared stand-up greatcoat collar with front opening */
        mb_frustum(&b, v3(0, 0.54f, 0.0f), 0.26f, 0.24f,
                       v3(0, 0.67f, 0.0f), 0.30f, 0.26f,
                       vest, vest, vest, vest);

        /* Double-breasted front overlap lapel + silver button studs */
        mb_frustum(&b, v3(0.045f, 0.10f, 0.126f), 0.11f, 0.018f,
                       v3(0.045f, 0.54f, 0.122f), 0.11f, 0.018f,
                       vest, vest, vest, vest);
        for (int s = 0; s < 4; s++) {
            float sy = 0.16f + (float)s * 0.11f;
            mb_frustum(&b, v3(0.068f, sy - 0.016f, 0.134f), 0.032f, 0.010f,
                           v3(0.068f, sy + 0.016f, 0.134f), 0.032f, 0.010f,
                           coat_btn, coat_btn, coat_btn, coat_btn);
        }

        /* Tactical shoulder harness webbing straps running over clavicles & down back */
        mb_frustum(&b, v3(-0.135f, 0.32f, 0.01f), 0.064f, 0.268f,
                       v3(-0.125f, 0.57f, 0.01f), 0.060f, 0.252f,
                       gun_dark, gun_dark, gun_dark, gun_dark);
        mb_frustum(&b, v3( 0.135f, 0.32f, 0.01f), 0.064f, 0.268f,
                       v3( 0.125f, 0.57f, 0.01f), 0.060f, 0.252f,
                       gun_dark, gun_dark, gun_dark, gun_dark);
        /* Metallic rectangular harness adjustment buckles (front & rear) */
        mb_frustum(&b, v3(-0.135f, 0.38f,  0.132f), 0.054f, 0.016f,
                       v3(-0.135f, 0.44f,  0.132f), 0.054f, 0.016f,
                       harness_b, harness_b, harness_b, harness_b);
        mb_frustum(&b, v3( 0.135f, 0.38f,  0.132f), 0.054f, 0.016f,
                       v3( 0.135f, 0.44f,  0.132f), 0.054f, 0.016f,
                       harness_b, harness_b, harness_b, harness_b);
        mb_frustum(&b, v3(-0.135f, 0.38f, -0.132f), 0.054f, 0.016f,
                       v3(-0.135f, 0.44f, -0.132f), 0.054f, 0.016f,
                       harness_b, harness_b, harness_b, harness_b);
        mb_frustum(&b, v3( 0.135f, 0.38f, -0.132f), 0.054f, 0.016f,
                       v3( 0.135f, 0.44f, -0.132f), 0.054f, 0.016f,
                       harness_b, harness_b, harness_b, harness_b);

        /* 3-cell magazine chest rig with pointed flaps and pull-tabs (enemy-soldier.jpg) */
        for (int p = -1; p <= 1; p++) {
            float px = (float)p * 0.092f;
            /* Boxy magazine pouch body */
            mb_frustum(&b, v3(px, 0.12f, 0.140f), 0.076f, 0.058f,
                           v3(px, 0.28f, 0.142f), 0.078f, 0.060f,
                           gun_dark, gun_dark, vest, vest);
            /* Pointed pouch flap closure */
            mb_frustum(&b, v3(px, 0.25f, 0.168f), 0.078f, 0.016f,
                           v3(px, 0.29f, 0.168f), 0.078f, 0.016f,
                           gun_dark, gun_dark, gun_dark, gun_dark);
            /* Flap pull-tab */
            mb_frustum(&b, v3(px, 0.21f, 0.170f), 0.024f, 0.012f,
                           v3(px, 0.25f, 0.170f), 0.024f, 0.012f,
                           pouch_tab, pouch_tab, pouch_tab, pouch_tab);
        }
    }
    return mb_finish(&b);
}

static Mesh build_hips(bool guard) {
    MBuilder b;
    mb_init(&b);
    UVRect belt = guard ? (UVRect){ 0.52f, 0.52f, 0.78f, 0.78f }
                        : (UVRect){ 0.02f, 0.02f, 0.48f, 0.20f };
    UVRect pouch = { 0.04f, 0.52f, 0.46f, 0.72f };
    UVRect stud  = { 0.84f, 0.53f, 0.96f, 0.65f };

    /* Waist battle belt */
    mb_frustum(&b, v3(0, 0.00f, 0), 0.33f, 0.20f,
                   v3(0, 0.20f, 0), 0.35f, 0.22f,
                   belt, belt, belt, belt);

    if (!guard) {
        /* Front utility pouches (operator-model-sheet.jpg) */
        mb_frustum(&b, v3(-0.095f, 0.04f,  0.118f), 0.072f, 0.046f,
                       v3(-0.095f, 0.19f,  0.120f), 0.074f, 0.048f,
                       belt, pouch, pouch, belt);
        mb_frustum(&b, v3( 0.095f, 0.04f,  0.118f), 0.072f, 0.046f,
                       v3( 0.095f, 0.19f,  0.120f), 0.074f, 0.048f,
                       belt, pouch, pouch, belt);
        /* Lateral hip pouches on left and right */
        mb_frustum(&b, v3(-0.175f, 0.04f, 0.0f), 0.046f, 0.088f,
                       v3(-0.175f, 0.19f, 0.0f), 0.048f, 0.090f,
                       belt, pouch, pouch, belt);
        mb_frustum(&b, v3( 0.175f, 0.04f, 0.0f), 0.046f, 0.088f,
                       v3( 0.175f, 0.19f, 0.0f), 0.048f, 0.090f,
                       belt, pouch, pouch, belt);
        /* 4 distinct utility pouches across rear waistband (operator-rear-reference.png) */
        for (int p = 0; p < 4; p++) {
            float px = -0.115f + (float)p * 0.077f;
            mb_frustum(&b, v3(px, 0.04f, -0.122f), 0.068f, 0.048f,
                           v3(px, 0.19f, -0.122f), 0.070f, 0.050f,
                           belt, pouch, pouch, belt);
        }
    } else {
        /* Regime Trooper Flared Split Greatcoat Skirt (enemy-soldier.jpg & rear) */
        /* Left flared skirt draping over left thigh */
        mb_frustum(&b, v3(-0.118f, -0.27f, -0.01f), 0.218f, 0.272f,
                       v3(-0.095f,  0.02f,  0.00f), 0.175f, 0.220f,
                       belt, belt, belt, belt);
        /* Right flared skirt draping over right thigh */
        mb_frustum(&b, v3( 0.118f, -0.27f, -0.01f), 0.218f, 0.272f,
                       v3( 0.095f,  0.02f,  0.00f), 0.175f, 0.220f,
                       belt, belt, belt, belt);
        /* Silver button studs along lower coat front hem (enemy-soldier.jpg) */
        mb_frustum(&b, v3(0.065f, -0.12f, 0.138f), 0.028f, 0.010f,
                       v3(0.065f, -0.08f, 0.138f), 0.028f, 0.010f,
                       stud, stud, stud, stud);
        mb_frustum(&b, v3(0.065f, -0.23f, 0.142f), 0.028f, 0.010f,
                       v3(0.065f, -0.19f, 0.142f), 0.028f, 0.010f,
                       stud, stud, stud, stud);
    }
    return mb_finish(&b);
}

/* Upper arm hangs from shoulder pivot (0,0,0) down to y = -0.29 */
static Mesh build_upper_arm(bool is_right, bool guard) {
    MBuilder b;
    mb_init(&b);
    if (guard) {
        UVRect pad = { 0.04f, 0.36f, 0.24f, 0.46f };
        if (!is_right) {
            UVRect red_plain = { 0.04f, 0.56f, 0.24f, 0.94f };
            UVRect bars      = { 0.18f, 0.54f, 0.82f, 0.96f };
            UVRect sleeve    = { 0.04f, 0.36f, 0.24f, 0.46f };
            mb_frustum(&b, v3(0, -0.29f, 0), 0.118f, 0.120f,
                           v3(0, -0.18f, 0), 0.128f, 0.130f,
                           sleeve, sleeve, sleeve, sleeve);
            /* Left arm crimson pauldron + three white vertical bars (III) */
            mb_frustum(&b, v3(-0.006f, -0.18f, 0), 0.138f, 0.140f,
                           v3(-0.006f, -0.06f, 0), 0.148f, 0.150f,
                           red_plain, bars, red_plain, red_plain);
            mb_frustum(&b, v3(-0.014f, -0.06f, 0), 0.162f, 0.164f,
                           v3(-0.006f,  0.055f, 0), 0.168f, 0.170f,
                           red_plain, red_plain, red_plain, red_plain);
            /* Sculpted hard protective elbow pad (enemy-soldier.jpg & rear) */
            mb_frustum(&b, v3(-0.018f, -0.28f, -0.045f), 0.082f, 0.052f,
                           v3(-0.018f, -0.19f, -0.045f), 0.086f, 0.056f,
                           pad, pad, pad, pad);
            return mb_finish(&b);
        }
        UVRect uni = { 0.55f, 0.55f, 0.95f, 0.75f };
        mb_frustum(&b, v3(0, -0.29f, 0), 0.118f, 0.120f,
                       v3(0,  0.04f, 0), 0.146f, 0.150f,
                       uni, uni, uni, uni);
        /* Right arm sculpted hard protective elbow pad (enemy-soldier.jpg & rear) */
        mb_frustum(&b, v3(0.018f, -0.28f, -0.045f), 0.082f, 0.052f,
                       v3(0.018f, -0.19f, -0.045f), 0.086f, 0.056f,
                       pad, pad, pad, pad);
        return mb_finish(&b);
    }
    if (!is_right) {
        UVRect camo = { 0.04f, 0.04f, 0.56f, 0.92f };
        mb_frustum(&b, v3(0, -0.29f, 0), 0.126f, 0.130f,
                       v3(0,  0.05f, 0), 0.162f, 0.166f,
                       camo, camo, camo, camo);
        return mb_finish(&b);
    }

    /* Right upper arm (TEX_PATCH): muscular sleeve + upright 3D NSA shoulder crest */
    UVRect camo_lo = { 0.00f, 0.04f, 0.15f, 0.46f };
    UVRect camo_hi = { 0.85f, 0.42f, 1.00f, 0.96f };
    UVRect patch   = { 0.84f, 0.89f, 0.16f, 0.09f };

    mb_frustum(&b, v3(0, -0.29f, 0), 0.126f, 0.130f,
                   v3(0, -0.17f, 0), 0.142f, 0.146f,
                   camo_lo, camo_lo, camo_lo, camo_lo);
    mb_frustum(&b, v3(0, -0.17f, 0), 0.142f, 0.146f,
                   v3(0,  0.05f, 0), 0.164f, 0.168f,
                   camo_hi, camo_hi, camo_hi, camo_hi);

    /* Exact inverse of canonical AIM uarm_r (rz=-0.16, rx=-0.68) so NSA crest sits upright! */
    V3 wn = v3_norm(v3(0.84f, 0.06f, 0.54f));
    V3 wr = v3_norm(v3_cross(v3(0.0f, 1.0f, 0.0f), wn));
    V3 wu = v3_cross(wn, wr);
    V3 n_out = inv_limb(wn, -0.16f, -0.68f);
    V3 r_ax  = inv_limb(wr, -0.16f, -0.68f);
    V3 u_ax  = inv_limb(wu, -0.16f, -0.68f);
    mb_obb(&b, v3(0.106f, -0.078f, 0.030f), r_ax, u_ax, n_out,
           0.134f, 0.164f, 0.016f, patch, camo_hi, camo_hi, camo_hi);
    return mb_finish(&b);
}

/* Forearm + sculpted gloves + operator HK USP or guard tactical assault rifle */
static Mesh build_forearm(bool with_item, bool guard) {
    MBuilder b;
    mb_init(&b);
    UVRect camo  = guard ? (UVRect){ 0.55f, 0.55f, 0.95f, 0.75f }
                         : (UVRect){ 0.50f, 0.02f, 0.74f, 0.48f };
    UVRect glove = guard ? (UVRect){ 0.04f, 0.52f, 0.46f, 0.72f }
                         : (UVRect){ 0.04f, 0.04f, 0.46f, 0.46f };
    UVRect dark_g = { 0.04f, 0.04f, 0.22f, 0.18f };
    UVRect steel = guard ? (UVRect){ 0.04f, 0.78f, 0.46f, 0.98f }
                         : (UVRect){ 0.02f, 0.52f, 0.98f, 0.98f };
    UVRect bore  = { 0.42f, 0.75f, 0.58f, 0.92f };

    /* Forearm sleeve */
    mb_frustum(&b, v3(0, -0.195f, 0), 0.102f, 0.106f,
                   v3(0,  0.025f, 0), 0.128f, 0.130f,
                   camo, camo, camo, camo);

    if (!guard && !with_item) {
        /* Operator LEFT support hand (Weaver grip) */
        V3 wf = v3_norm(v3(0.68f, 0.12f, 0.72f));
        V3 wu = v3_norm(v3(-0.06f, 0.98f, 0.16f));
        V3 wr = v3_norm(v3_cross(wu, wf));
        wu = v3_cross(wf, wr);
        V3 lr = inv_limb(wr, 0.48f, -1.42f);
        V3 lu = inv_limb(wu, 0.48f, -1.42f);
        V3 lf = inv_limb(wf, 0.48f, -1.42f);
        V3 palm = v3(0.012f, -0.248f, 0.018f);

        mb_obb(&b, palm, lr, lu, lf, 0.074f, 0.088f, 0.078f, glove, glove, glove, dark_g);
        for (int f = 0; f < 4; f++) {
            float fy = 0.024f - (float)f * 0.019f;
            float fz = 0.052f - fabsf((float)f - 1.5f) * 0.003f;
            mb_obb(&b, obb_pt(palm, lr, lu, lf, -0.018f, fy, fz),
                   lr, lu, lf, 0.068f, 0.016f, 0.034f,
                   glove, dark_g, glove, dark_g);
        }
        mb_obb(&b, obb_pt(palm, lr, lu, lf, 0.012f, 0.054f, 0.030f),
               lr, lu, lf, 0.030f, 0.022f, 0.062f, glove, glove, glove, dark_g);
        return mb_finish(&b);
    }

    if (guard && !with_item) {
        /* Regime Trooper LEFT support hand: gripping the assault rifle handguard */
        V3 wf_char = v3_norm(v3(0.28f, -0.48f, 0.83f));
        V3 ru_char = v3_norm(v3(-0.24f, 0.82f, 0.52f));
        V3 rr_char = v3_cross(ru_char, wf_char);
        V3 wf = m_rot_x(wf_char, 1.33f);
        V3 rr = m_rot_x(rr_char, 1.33f);
        V3 ru = v3_cross(wf, rr);
        V3 palm = v3(0.042f, -0.245f, 0.038f);
        mb_obb(&b, palm, rr, ru, wf, 0.068f, 0.078f, 0.072f, glove, glove, glove, dark_g);
        for (int f = 0; f < 3; f++) {
            float fz = 0.018f - (float)f * 0.018f;
            mb_obb(&b, obb_pt(palm, rr, ru, wf, 0.016f, 0.024f, fz),
                   rr, ru, wf, 0.046f, 0.028f, 0.016f, glove, dark_g, glove, dark_g);
        }
        return mb_finish(&b);
    }

    /* Right shooting glove */
    mb_frustum(&b, v3(0, -0.280f, 0.006f), 0.086f, 0.094f,
                   v3(0, -0.195f, 0.002f), 0.098f, 0.102f,
                   glove, glove, glove, glove);

    if (with_item && !guard) {
        /* Operator RIGHT shooting hand + two-tone HK USP */
        V3 wf = v3_norm(v3(0.68f, 0.12f, 0.72f));
        V3 wu = v3_norm(v3(-0.06f, 0.98f, 0.16f));
        V3 wr = v3_norm(v3_cross(wu, wf));
        wu = v3_cross(wf, wr);

        V3 ax_r = inv_limb(wr, -0.22f, -1.48f);
        V3 ax_u = inv_limb(wu, -0.22f, -1.48f);
        V3 ax_f = inv_limb(wf, -0.22f, -1.48f);
        V3 grip = v3(0.006f, -0.246f, 0.018f);

        for (int f = 0; f < 3; f++) {
            float fy = 0.020f - (float)f * 0.021f;
            mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.014f, fy, 0.026f),
                   ax_r, ax_u, ax_f, 0.044f, 0.016f, 0.036f,
                   glove, dark_g, glove, dark_g);
        }
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, -0.022f, 0.046f, 0.024f),
               ax_r, ax_u, ax_f, 0.020f, 0.020f, 0.056f, glove, glove, glove, dark_g);

        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.006f, -0.004f),
               ax_r, ax_u, ax_f, 0.033f, 0.094f, 0.050f,
               glove, glove, glove, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.046f,  0.062f),
               ax_r, ax_u, ax_f, 0.035f, 0.024f, 0.188f,
               glove, glove, glove, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.022f,  0.044f),
               ax_r, ax_u, ax_f, 0.014f, 0.024f, 0.056f,
               dark_g, dark_g, dark_g, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.075f,  0.064f),
               ax_r, ax_u, ax_f, 0.039f, 0.035f, 0.194f,
               steel, steel, steel, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.076f,  0.165f),
               ax_r, ax_u, ax_f, 0.018f, 0.018f, 0.010f,
               bore, steel, steel, steel);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.058f,  0.162f),
               ax_r, ax_u, ax_f, 0.011f, 0.011f, 0.008f,
               steel, steel, steel, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.097f, -0.022f),
               ax_r, ax_u, ax_f, 0.026f, 0.009f, 0.014f,
               dark_g, dark_g, dark_g, dark_g);
        mb_obb(&b, obb_pt(grip, ax_r, ax_u, ax_f, 0.0f, 0.097f,  0.148f),
               ax_r, ax_u, ax_f, 0.008f, 0.009f, 0.012f,
               dark_g, dark_g, dark_g, dark_g);
    } else if (with_item && guard) {
        /* Regime Trooper RIGHT shooting hand + Tactical Assault Rifle (enemy-soldier.jpg)
         * Canonical low-ready patrol carry: receiver at waist, buttstock tucked under
         * right armpit, barrel pointing forward-downward across thighs at ~30 degrees. */
        V3 rf_char = v3_norm(v3(0.28f, -0.48f, 0.83f));
        V3 ru_char = v3_norm(v3(-0.24f, 0.82f, 0.52f));
        V3 rr_char = v3_cross(ru_char, rf_char);

        /* Invert right forearm pitch (-1.40 rad) */
        V3 rf = m_rot_x(rf_char, 1.40f);
        V3 ru = m_rot_x(ru_char, 1.40f);
        V3 rr = m_rot_x(rr_char, 1.40f);
        V3 rc = v3(-0.015f, -0.265f, 0.035f);

        /* Main boxy receiver & magwell */
        mb_obb(&b, rc, rr, ru, rf, 0.044f, 0.078f, 0.32f, glove, glove, glove, steel);
        /* Pistol grip */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, -0.075f, -0.06f),
               rr, ru, rf, 0.034f, 0.10f, 0.046f, glove, glove, glove, dark_g);
        /* Trigger guard */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, -0.042f, -0.01f),
               rr, ru, rf, 0.014f, 0.028f, 0.048f, dark_g, dark_g, dark_g, dark_g);
        /* Curved 30-round box magazine extending forward/down */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, -0.105f, 0.07f),
               rr, ru, rf, 0.030f, 0.145f, 0.066f, steel, steel, steel, dark_g);
        /* Top Picatinny carry handle / optical rail */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.054f, 0.04f),
               rr, ru, rf, 0.028f, 0.032f, 0.28f, dark_g, dark_g, dark_g, steel);
        /* Front sight post */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.062f, 0.20f),
               rr, ru, rf, 0.012f, 0.022f, 0.016f, dark_g, dark_g, dark_g, dark_g);
        /* Tactical ribbed handguard forward of receiver */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.008f, 0.26f),
               rr, ru, rf, 0.042f, 0.054f, 0.20f, glove, glove, glove, dark_g);
        /* Extended steel barrel */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.010f, 0.43f),
               rr, ru, rf, 0.018f, 0.018f, 0.18f, steel, steel, steel, steel);
        /* Cylindrical birdcage flash hider compensator */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.010f, 0.53f),
               rr, ru, rf, 0.026f, 0.026f, 0.052f, steel, dark_g, steel, bore);
        /* Tapered skeleton buttstock extending rearward under arm */
        mb_obb(&b, obb_pt(rc, rr, ru, rf, 0.0f, 0.006f, -0.24f),
               rr, ru, rf, 0.034f, 0.082f, 0.20f, dark_g, dark_g, dark_g, dark_g);
    }
    return mb_finish(&b);
}

/* Upper thigh hanging from hip socket (0,0,0) down to y = -0.40 */
static Mesh build_thigh(bool is_right, bool guard) {
    MBuilder b;
    mb_init(&b);
    UVRect camo = { 0.0f, 0.0f, 0.45f, 0.55f };
    UVRect dark = { 0.10f, 0.05f, 0.28f, 0.20f };
    UVRect knee = { 0.04f, 0.36f, 0.24f, 0.46f };
    mb_frustum(&b, v3(0, -0.40f, 0), 0.132f, 0.138f,
                   v3(0,  0.03f, 0), 0.168f, 0.178f,
                   camo, camo, camo, camo);
    if (!guard && is_right) {
        /* Right thigh drop-leg holster + twin horizontal leg straps (operator-model-sheet & rear) */
        mb_frustum(&b, v3(0.082f, -0.26f, 0.01f), 0.052f, 0.108f,
                       v3(0.088f, -0.06f, 0.01f), 0.056f, 0.114f,
                       dark, dark, dark, dark);
        mb_frustum(&b, v3(0.005f, -0.15f, 0.0f), 0.164f, 0.168f,
                       v3(0.005f, -0.12f, 0.0f), 0.166f, 0.170f,
                       dark, dark, dark, dark);
        mb_frustum(&b, v3(0.004f, -0.24f, 0.0f), 0.154f, 0.158f,
                       v3(0.004f, -0.21f, 0.0f), 0.156f, 0.160f,
                       dark, dark, dark, dark);
    } else if (!guard && !is_right) {
        /* Left thigh cargo/utility drop pouch + twin horizontal leg straps (operator-model-sheet & rear) */
        mb_frustum(&b, v3(-0.082f, -0.26f, 0.01f), 0.052f, 0.108f,
                       v3(-0.088f, -0.06f, 0.01f), 0.056f, 0.114f,
                       dark, dark, dark, dark);
        mb_frustum(&b, v3(-0.005f, -0.15f, 0.0f), 0.164f, 0.168f,
                       v3(-0.005f, -0.12f, 0.0f), 0.166f, 0.170f,
                       dark, dark, dark, dark);
        mb_frustum(&b, v3(-0.004f, -0.24f, 0.0f), 0.154f, 0.158f,
                       v3(-0.004f, -0.21f, 0.0f), 0.156f, 0.160f,
                       dark, dark, dark, dark);
    } else if (guard) {
        /* Regime Trooper hexagonal knee pad upper shell (enemy-soldier.jpg & rear) */
        mb_frustum(&b, v3(0.0f, -0.395f, 0.054f), 0.096f, 0.050f,
                       v3(0.0f, -0.340f, 0.056f), 0.092f, 0.052f,
                       knee, knee, knee, knee);
    }
    return mb_finish(&b);
}

/* Lower shin + sculpted knee pad + laced combat boot (operator) or 3-strap buckle boot (guard) */
static Mesh build_shin_boot(bool guard) {
    MBuilder b;
    mb_init(&b);
    UVRect camo = { 0.0f, 0.25f, 0.42f, 0.72f };
    UVRect boot = { 0.05f, 0.02f, 0.25f, 0.18f };
    UVRect lace = { 0.25f, 0.52f, 0.48f, 0.72f };
    UVRect buckle_strap = { 0.68f, 0.84f, 0.98f, 0.96f }; /* horizontal buckle strap in TEX_VEST */
    UVRect pad  = { 0.04f, 0.36f, 0.24f, 0.46f };

    /* Tapered trousers */
    mb_frustum(&b, v3(0, -0.29f, 0.0f), 0.108f, 0.114f,
                   v3(0,  0.02f, 0.005f), 0.136f, 0.144f,
                   camo, camo, camo, camo);

    if (!guard) {
        /* Operator: Sculpted 3D hard-shell octagonal tactical knee pad (operator-model-sheet.jpg) */
        mb_frustum(&b, v3(0, -0.065f, 0.058f), 0.094f, 0.054f,
                       v3(0,  0.038f, 0.062f), 0.098f, 0.056f,
                       pad, pad, pad, pad);
        /* Laced combat boot shaft + front laces + lugged sole + angled toe cap */
        mb_frustum(&b, v3(0, -0.37f, 0.012f), 0.112f, 0.142f,
                       v3(0, -0.28f, 0.004f), 0.106f, 0.128f,
                       lace, boot, boot, boot);
        mb_frustum(&b, v3(0, -0.42f, 0.044f), 0.120f, 0.245f,
                       v3(0, -0.37f, 0.028f), 0.114f, 0.220f,
                       boot, boot, boot, boot);
    } else {
        /* Regime Trooper: Sculpted 3D hard hexagonal knee pad (enemy-soldier.jpg & rear) */
        mb_frustum(&b, v3(0, -0.070f, 0.058f), 0.098f, 0.054f,
                       v3(0,  0.035f, 0.062f), 0.096f, 0.054f,
                       pad, pad, pad, pad);
        /* Tall combat boot shaft */
        mb_frustum(&b, v3(0, -0.37f, 0.012f), 0.114f, 0.144f,
                       v3(0, -0.28f, 0.004f), 0.108f, 0.130f,
                       boot, boot, boot, boot);
        /* THREE horizontal buckle straps wrapping up the shin (enemy-soldier.jpg & rear) */
        for (int k = 0; k < 3; k++) {
            float by = -0.35f + (float)k * 0.055f;
            mb_frustum(&b, v3(0.002f, by - 0.012f, 0.008f), 0.118f, 0.148f,
                           v3(0.002f, by + 0.012f, 0.008f), 0.118f, 0.148f,
                           buckle_strap, buckle_strap, buckle_strap, buckle_strap);
        }
        /* Thick lugged combat outsole & angled toe cap */
        mb_frustum(&b, v3(0, -0.42f, 0.044f), 0.122f, 0.248f,
                       v3(0, -0.37f, 0.028f), 0.116f, 0.222f,
                       boot, boot, boot, boot);
    }
    return mb_finish(&b);
}

static Humanoid build_humanoid_common(bool guard) {
    Humanoid h;
    memset(&h, 0, sizeof h);
    h.is_guard = guard;

    h.parts[PART_HIPS]      = build_hips(guard);
    h.parts[PART_TORSO]     = build_torso(guard);
    h.parts[PART_HEAD]      = build_operator_head(guard);
    h.parts[PART_ARM_L]     = build_upper_arm(false, guard);
    h.parts[PART_ARM_R]     = build_upper_arm(true, guard);
    h.parts[PART_FOREARM_L] = build_forearm(false, guard);
    h.parts[PART_FOREARM_R] = build_forearm(true, guard);
    h.parts[PART_LEG_L]     = build_thigh(false, guard);
    h.parts[PART_LEG_R]     = build_thigh(true, guard);
    h.parts[PART_SHIN_L]    = build_shin_boot(guard);
    h.parts[PART_SHIN_R]    = build_shin_boot(guard);

    /* PS2-grade smooth normal shading for organic and cylindrical surfaces */
    mesh_smooth_normals(&h.parts[PART_HEAD], 0.92f);
    mesh_smooth_normals(&h.parts[PART_TORSO], 0.82f);
    mesh_smooth_normals(&h.parts[PART_ARM_L], 0.92f);
    mesh_smooth_normals(&h.parts[PART_ARM_R], 0.92f);
    mesh_smooth_normals(&h.parts[PART_FOREARM_L], 0.92f);
    mesh_smooth_normals(&h.parts[PART_FOREARM_R], 0.92f);
    mesh_smooth_normals(&h.parts[PART_LEG_L], 0.92f);
    mesh_smooth_normals(&h.parts[PART_LEG_R], 0.92f);
    mesh_smooth_normals(&h.parts[PART_SHIN_L], 0.92f);
    mesh_smooth_normals(&h.parts[PART_SHIN_R], 0.92f);

    h.part_tex[PART_HIPS]      = TEX_VEST;
    h.part_tex[PART_TORSO]     = TEX_VEST;
    h.part_tex[PART_HEAD]      = guard ? TEX_INSIGNIA : TEX_FACE;
    h.part_tex[PART_ARM_L]     = guard ? TEX_INSIGNIA : TEX_CAMO;
    h.part_tex[PART_ARM_R]     = guard ? TEX_VEST     : TEX_PATCH;
    h.part_tex[PART_FOREARM_L] = guard ? TEX_VEST     : TEX_WEAPON;
    h.part_tex[PART_FOREARM_R] = guard ? TEX_VEST     : TEX_WEAPON;
    h.part_tex[PART_LEG_L]     = guard ? TEX_VEST     : TEX_CAMO;
    h.part_tex[PART_LEG_R]     = guard ? TEX_VEST     : TEX_CAMO;
    h.part_tex[PART_SHIN_L]    = guard ? TEX_VEST     : TEX_CAMO;
    h.part_tex[PART_SHIN_R]    = guard ? TEX_VEST     : TEX_CAMO;

    return h;
}

Humanoid humanoid_new(void)       { return build_humanoid_common(false); }
Humanoid humanoid_guard_new(void) { return build_humanoid_common(true); }

void humanoid_free(Humanoid *h) {
    for (int i = 0; i < PART_COUNT; i++) mesh_free(&h->parts[i]);
}

/* 3-Prong Curved Steel Grappling Claw (grapple-hook.jpg) */
Mesh mesh_grapple_claw(void) {
    MBuilder b;
    mb_init(&b);
    UVRect st = { 0.1f, 0.1f, 0.9f, 0.9f };
    mb_oct_ring(&b, v3(0, -0.14f, 0), 0.042f, 0.042f,
                    v3(0,  0.12f, 0), 0.036f, 0.036f, st, st, true, true);
    for (int k = 0; k < 3; k++) {
        float ang = (float)k * (2.0f * PS15_PI / 3.0f);
        V3 rad = v3(cosf(ang), 0.0f, sinf(ang));
        V3 tan = v3(-sinf(ang), 0.0f, cosf(ang));
        V3 f1  = v3_norm(v3_add(v3_scale(rad, 0.85f), v3(0, -0.52f, 0)));
        V3 u1  = v3_cross(f1, tan);
        mb_obb(&b, v3_add(v3(0, 0.06f, 0), v3_scale(rad, 0.09f)),
               tan, u1, f1, 0.024f, 0.028f, 0.18f, st, st, st, st);
        V3 f2  = v3_norm(v3_add(v3_scale(rad, 0.32f), v3(0, -0.94f, 0)));
        V3 u2  = v3_cross(f2, tan);
        mb_obb(&b, v3_add(v3(0, -0.04f, 0), v3_scale(rad, 0.17f)),
               tan, u2, f2, 0.018f, 0.022f, 0.15f, st, st, st, st);
    }
    return mb_finish(&b);
}

/* Towering Bipedal Regime Patrol Walker (~3.55m, exact replica of enemy-mecha.jpg & rear) */
MechaMesh mecha_mesh_new(void) {
    MechaMesh mm;
    UVRect plate  = { 0.54f, 0.04f, 0.96f, 0.33f }; /* weathered ceramic white armor with chipped paint */
    UVRect eye    = { 0.58f, 0.34f, 0.92f, 0.46f }; /* crimson optic sensor slit */
    UVRect bars   = { 0.18f, 0.54f, 0.82f, 0.96f }; /* 3 vertical white bars on crimson (III) */
    UVRect red_p  = { 0.04f, 0.56f, 0.20f, 0.94f }; /* plain crimson Regime identification */
    UVRect dark   = { 0.04f, 0.36f, 0.24f, 0.46f }; /* dark chassis steel & machinery */
    UVRect steel  = { 0.08f, 0.08f, 0.92f, 0.92f }; /* industrial gunmetal steel */
    UVRect muzzle = { 0.40f, 0.70f, 0.60f, 0.90f }; /* hollow dark muzzle */

    {   /* Pelvis: heavy hip chassis + exposed waist column + twin auxiliary pistons */
        MBuilder b; mb_init(&b);
        /* Central hydraulic waist column cylinder */
        mb_oct_ring(&b, v3(0, -0.04f, 0), 0.16f, 0.16f,
                        v3(0,  0.34f, 0), 0.14f, 0.14f,
                        steel, steel, false, false);
        /* Twin vertical pneumatic ram pistons flanking waist */
        mb_oct_ring(&b, v3(-0.16f, -0.02f, 0.02f), 0.038f, 0.038f,
                        v3(-0.16f,  0.30f, 0.02f), 0.034f, 0.034f,
                        steel, steel, false, false);
        mb_oct_ring(&b, v3( 0.16f, -0.02f, 0.02f), 0.038f, 0.038f,
                        v3( 0.16f,  0.30f, 0.02f), 0.034f, 0.034f,
                        steel, steel, false, false);
        /* Heavy armored groin chassis */
        mb_frustum(&b, v3(0, -0.22f, -0.02f), 0.54f, 0.44f,
                       v3(0,  0.04f,  0.00f), 0.48f, 0.38f,
                       plate, dark, plate, dark);
        /* Outward-canted rotary hip knuckle assemblies with red armor caps */
        mb_frustum(&b, v3(-0.36f, -0.08f, 0.0f), 0.18f, 0.36f,
                       v3(-0.24f, -0.08f, 0.0f), 0.18f, 0.36f,
                       dark, red_p, dark, dark);
        mb_frustum(&b, v3( 0.24f, -0.08f, 0.0f), 0.18f, 0.36f,
                       v3( 0.36f, -0.08f, 0.0f), 0.18f, 0.36f,
                       dark, red_p, dark, dark);
        mm.pelvis = mb_finish(&b);
    }
    {   /* Torso: V-prow white ceramic chest armor + shoulder arches + dorsal core */
        MBuilder b; mb_init(&b);
        /* Main armored core box */
        mb_frustum(&b, v3(0, 0.04f, 0.0f), 0.62f, 0.54f,
                       v3(0, 0.88f, 0.0f), 0.74f, 0.58f,
                       plate, dark, plate, plate);

        /* V-prow forward-angled ceramic glacis armor plates (enemy-mecha.jpg) */
        V3 prow_bot = v3( 0.0f,  0.08f, 0.32f);
        V3 prow_mid = v3( 0.0f,  0.46f, 0.56f);
        V3 prow_top = v3( 0.0f,  0.88f, 0.44f);
        V3 prow_l   = v3(-0.52f, 0.46f, 0.24f);
        V3 prow_r   = v3( 0.52f, 0.46f, 0.24f);
        V3 prow_tl  = v3(-0.46f, 0.94f, 0.18f);
        V3 prow_tr  = v3( 0.46f, 0.94f, 0.18f);
        V3 prow_bl  = v3(-0.44f, 0.10f, 0.18f);
        V3 prow_br  = v3( 0.44f, 0.10f, 0.18f);

        /* Upper V-prow facets */
        mb_quad(&b, prow_mid, prow_top, prow_tl, prow_l,  plate.u0, plate.v0, plate.u1, plate.v1);
        mb_quad(&b, prow_mid, prow_r,   prow_tr, prow_top, plate.u0, plate.v0, plate.u1, plate.v1);
        /* Lower V-prow facets */
        mb_quad(&b, prow_bot, prow_mid, prow_l,  prow_bl, plate.u0, plate.v0, plate.u1, plate.v1);
        mb_quad(&b, prow_bot, prow_br,  prow_r,  prow_mid, plate.u0, plate.v0, plate.u1, plate.v1);

        /* Heavy beveled shoulder breastplate cowlings flanking the head */
        mb_frustum(&b, v3(-0.46f, 0.52f, 0.10f), 0.28f, 0.44f,
                       v3(-0.44f, 0.96f, 0.06f), 0.26f, 0.40f,
                       plate, dark, plate, plate);
        mb_frustum(&b, v3( 0.46f, 0.52f, 0.10f), 0.28f, 0.44f,
                       v3( 0.44f, 0.96f, 0.06f), 0.26f, 0.40f,
                       plate, dark, plate, plate);

        /* Dorsal Core (enemy-mecha-rear.jpg): recessed radiator + twin coolant cylinders */
        mb_frustum(&b, v3(0, 0.20f, -0.32f), 0.34f, 0.10f,
                       v3(0, 0.80f, -0.30f), 0.32f, 0.10f,
                       dark, dark, dark, dark);
        mb_oct_ring(&b, v3(-0.18f, 0.20f, -0.36f), 0.082f, 0.082f,
                        v3(-0.18f, 0.82f, -0.34f), 0.080f, 0.080f,
                        dark, red_p, true, false);
        mb_oct_ring(&b, v3( 0.18f, 0.20f, -0.36f), 0.082f, 0.082f,
                        v3( 0.18f, 0.82f, -0.34f), 0.080f, 0.080f,
                        dark, red_p, true, false);

        /* Faceted ceramic carapace backplate (enemy-mecha-rear.jpg) */
        V3 back_top = v3( 0.0f,  0.88f, -0.28f);
        V3 back_mid = v3( 0.0f,  0.50f, -0.32f);
        V3 back_bot = v3( 0.0f,  0.18f, -0.28f);
        V3 back_tl  = v3(-0.46f, 0.92f, -0.20f);
        V3 back_tr  = v3( 0.46f, 0.92f, -0.20f);
        V3 back_l   = v3(-0.48f, 0.50f, -0.22f);
        V3 back_r   = v3( 0.48f, 0.50f, -0.22f);
        V3 back_bl  = v3(-0.42f, 0.16f, -0.20f);
        V3 back_br  = v3( 0.42f, 0.16f, -0.20f);
        mb_quad(&b, back_mid, back_l,   back_tl, back_top, plate.u0, plate.v0, plate.u1, plate.v1);
        mb_quad(&b, back_mid, back_top, back_tr, back_r,   plate.u0, plate.v0, plate.u1, plate.v1);
        mb_quad(&b, back_bot, back_bl,  back_l,  back_mid, plate.u0, plate.v0, plate.u1, plate.v1);
        mb_quad(&b, back_bot, back_mid, back_r,  back_br,  plate.u0, plate.v0, plate.u1, plate.v1);

        /* Ribbed heavy industrial power conduits looping under shoulders (enemy-mecha.jpg & rear) */
        mb_oct_ring(&b, v3(-0.44f, 0.54f, 0.08f), 0.038f, 0.038f,
                        v3(-0.48f, 0.40f, 0.04f), 0.038f, 0.038f,
                        dark, dark, false, false);
        mb_oct_ring(&b, v3(-0.48f, 0.40f, 0.04f), 0.038f, 0.038f,
                        v3(-0.44f, 0.28f, -0.04f), 0.038f, 0.038f,
                        dark, dark, false, false);
        mb_oct_ring(&b, v3( 0.44f, 0.54f, 0.08f), 0.038f, 0.038f,
                        v3( 0.48f, 0.40f, 0.04f), 0.038f, 0.038f,
                        dark, dark, false, false);
        mb_oct_ring(&b, v3( 0.48f, 0.40f, 0.04f), 0.038f, 0.038f,
                        v3( 0.44f, 0.28f, -0.04f), 0.038f, 0.038f,
                        dark, dark, false, false);
        /* Rear dorsal power cables looping from spine to waist */
        mb_oct_ring(&b, v3(-0.16f, 0.24f, -0.28f), 0.036f, 0.036f,
                        v3(-0.14f, 0.06f, -0.20f), 0.036f, 0.036f,
                        dark, dark, false, false);
        mb_oct_ring(&b, v3( 0.16f, 0.24f, -0.28f), 0.036f, 0.036f,
                        v3( 0.14f, 0.06f, -0.20f), 0.036f, 0.036f,
                        dark, dark, false, false);
        mm.torso = mb_finish(&b);
    }
    {   /* Low-slung angular head nacelle with horizontal glowing crimson eye slit */
        MBuilder b; mb_init(&b);
        /* 45-degree sloped ceramic forehead armor plate */
        mb_frustum(&b, v3(0, 0.02f,  0.12f), 0.26f, 0.38f,
                       v3(0, 0.22f, -0.04f), 0.22f, 0.32f,
                       plate, plate, plate, plate);
        /* Inset sensor visor cavity + crimson optic sensor slit */
        mb_frustum(&b, v3(0, -0.10f, 0.02f), 0.28f, 0.44f,
                       v3(0,  0.04f, 0.16f), 0.26f, 0.48f,
                       eye, dark, dark, plate);
        /* Chiseled metal jaw armor block */
        mb_frustum(&b, v3(0, -0.18f, 0.04f), 0.22f, 0.30f,
                       v3(0, -0.08f, 0.08f), 0.24f, 0.34f,
                       dark, dark, dark, dark);
        mm.head = mb_finish(&b);
    }
    {   /* Left Arm: Crimson Three-Bars Pauldron + hydraulic ram + 3-claw hand */
        MBuilder b; mb_init(&b);
        /* Crimson pauldron with 3 vertical white bars (enemy-mecha.jpg & rear) */
        mb_frustum(&b, v3(-0.08f, -0.18f, 0.00f), 0.48f, 0.58f,
                       v3( 0.02f,  0.26f, 0.00f), 0.44f, 0.52f,
                       red_p, bars, red_p, red_p);
        /* Upper arm hydraulic ram cylinder & elbow swivel */
        mb_oct_ring(&b, v3(-0.05f, -0.68f, 0.03f), 0.085f, 0.085f,
                        v3(-0.04f, -0.16f, 0.00f), 0.095f, 0.095f,
                        steel, steel, false, false);
        mb_frustum(&b, v3(-0.05f, -0.74f, 0.04f), 0.22f, 0.24f,
                       v3(-0.05f, -0.66f, 0.04f), 0.22f, 0.24f,
                       dark, dark, dark, dark);
        /* Armored forearm chassis */
        mb_frustum(&b, v3(-0.06f, -1.24f, 0.14f), 0.25f, 0.28f,
                       v3(-0.05f, -0.72f, 0.04f), 0.24f, 0.26f,
                       plate, dark, plate, dark);
        /* Mechanical wrist collar */
        mb_frustum(&b, v3(-0.06f, -1.32f, 0.16f), 0.18f, 0.22f,
                       v3(-0.06f, -1.24f, 0.14f), 0.22f, 0.26f,
                       dark, dark, dark, dark);

        /* Articulated 3-claw hand (enemy-mecha.jpg & rear) */
        /* Upper Outer Claw */
        mb_frustum(&b, v3(-0.16f, -1.48f, 0.22f), 0.065f, 0.085f,
                       v3(-0.12f, -1.30f, 0.16f), 0.080f, 0.100f,
                       dark, steel, dark, steel);
        mb_frustum(&b, v3(-0.18f, -1.62f, 0.26f), 0.045f, 0.060f,
                       v3(-0.16f, -1.48f, 0.22f), 0.065f, 0.085f,
                       steel, steel, steel, steel);
        /* Upper Inner Claw */
        mb_frustum(&b, v3(-0.05f, -1.50f, 0.24f), 0.065f, 0.085f,
                       v3(-0.04f, -1.30f, 0.18f), 0.080f, 0.100f,
                       dark, steel, dark, steel);
        mb_frustum(&b, v3(-0.05f, -1.64f, 0.28f), 0.045f, 0.060f,
                       v3(-0.05f, -1.50f, 0.24f), 0.065f, 0.085f,
                       steel, steel, steel, steel);
        /* Opposing Lower Thumb Claw */
        mb_frustum(&b, v3( 0.06f, -1.45f, 0.16f), 0.065f, 0.085f,
                       v3( 0.04f, -1.30f, 0.12f), 0.080f, 0.100f,
                       dark, steel, dark, steel);
        mb_frustum(&b, v3( 0.08f, -1.58f, 0.20f), 0.045f, 0.060f,
                       v3( 0.06f, -1.45f, 0.16f), 0.065f, 0.085f,
                       steel, steel, steel, steel);
        mm.arm_l = mb_finish(&b);
    }
    {   /* Right Arm: Weathered pauldron + heavy autocannon with crimson bands */
        MBuilder b; mb_init(&b);
        mb_frustum(&b, v3( 0.06f, -0.16f, 0.00f), 0.44f, 0.54f,
                       v3(-0.02f,  0.24f, 0.00f), 0.40f, 0.48f,
                       plate, plate, plate, plate);
        mb_oct_ring(&b, v3(0.05f, -0.66f, 0.04f), 0.085f, 0.085f,
                        v3(0.04f, -0.14f, 0.00f), 0.095f, 0.095f,
                        steel, steel, false, false);
        mb_frustum(&b, v3(0.05f, -0.72f, 0.05f), 0.22f, 0.24f,
                       v3(0.05f, -0.64f, 0.05f), 0.22f, 0.24f,
                       dark, dark, dark, dark);
        /* Heavy rectangular autocannon receiver */
        mb_frustum(&b, v3(0.06f, -1.36f, 0.42f), 0.28f, 0.36f,
                       v3(0.06f, -0.70f, 0.10f), 0.30f, 0.34f,
                       steel, steel, steel, dark);
        /* Crimson identification bands on autocannon (enemy-mecha.jpg & rear) */
        mb_frustum(&b, v3(0.06f, -0.96f, 0.22f), 0.29f, 0.37f,
                       v3(0.06f, -0.86f, 0.18f), 0.29f, 0.37f,
                       red_p, red_p, red_p, red_p);
        mb_frustum(&b, v3(0.06f, -1.26f, 0.36f), 0.29f, 0.37f,
                       v3(0.06f, -1.16f, 0.32f), 0.29f, 0.37f,
                       red_p, red_p, red_p, red_p);
        /* Extended heavy rectangular barrel */
        mb_frustum(&b, v3(0.06f, -1.82f, 0.66f), 0.16f, 0.20f,
                       v3(0.06f, -1.34f, 0.40f), 0.19f, 0.23f,
                       steel, steel, steel, steel);
        /* Rectangular muzzle brake crown with hollow square bore */
        mb_frustum(&b, v3(0.06f, -1.90f, 0.70f), 0.17f, 0.21f,
                       v3(0.06f, -1.82f, 0.66f), 0.17f, 0.21f,
                       steel, steel, steel, muzzle);
        mm.arm_r = mb_finish(&b);
    }
    for (int side = 0; side < 2; side++) {
        MBuilder b; mb_init(&b);
        /* Cantilevered rotary hip knuckle */
        mb_frustum(&b, v3(0, -0.16f, 0), 0.34f, 0.42f,
                       v3(0,  0.08f, 0), 0.32f, 0.40f,
                       red_p, dark, red_p, dark);
        /* Upper thigh chassis beam */
        mb_frustum(&b, v3(0, -0.86f, 0.04f), 0.28f, 0.34f,
                       v3(0, -0.14f, 0.00f), 0.34f, 0.42f,
                       dark, dark, dark, dark);
        /* Forward-canted weathered white ceramic armor plate on thigh face */
        mb_frustum(&b, v3(0, -0.82f, 0.18f), 0.32f, 0.12f,
                       v3(0, -0.16f, 0.14f), 0.36f, 0.12f,
                       plate, plate, plate, plate);
        /* Dual-bolt rotary knee knuckles at lower end */
        mb_frustum(&b, v3(0, -0.92f, 0.04f), 0.28f, 0.32f,
                       v3(0, -0.84f, 0.04f), 0.28f, 0.32f,
                       steel, steel, steel, steel);
        if (side == 0) mm.leg_l = mb_finish(&b); else mm.leg_r = mb_finish(&b);

        mb_init(&b);
        /* Knee knuckle pivot hub with circular bolt heads */
        mb_oct_ring(&b, v3(-0.12f, 0.0f, 0.0f), 0.12f, 0.12f,
                        v3( 0.12f, 0.0f, 0.0f), 0.12f, 0.12f,
                        steel, steel, true, true);
        /* Twin hydraulic ram struts connecting knee to lower calf */
        mb_oct_ring(&b, v3(-0.06f, -0.74f, -0.08f), 0.034f, 0.034f,
                        v3(-0.06f, -0.10f,  0.02f), 0.038f, 0.038f,
                        steel, steel, false, false);
        mb_oct_ring(&b, v3( 0.06f, -0.74f, -0.08f), 0.034f, 0.034f,
                        v3( 0.06f, -0.10f,  0.02f), 0.038f, 0.038f,
                        steel, steel, false, false);
        /* Main reverse-joint calf chassis */
        mb_frustum(&b, v3(0, -0.84f, -0.06f), 0.24f, 0.30f,
                       v3(0,  0.02f,  0.04f), 0.26f, 0.32f,
                       dark, steel, dark, steel);
        /* Crimson red identification band on outer calf armor (enemy-mecha.jpg & rear) */
        mb_frustum(&b, v3(0, -0.56f, -0.02f), 0.25f, 0.31f,
                       v3(0, -0.42f,  0.00f), 0.25f, 0.31f,
                       red_p, red_p, red_p, red_p);
        /* Lower ankle knuckle with circular bolt hubs */
        mb_oct_ring(&b, v3(-0.11f, -0.88f, -0.04f), 0.09f, 0.09f,
                        v3( 0.11f, -0.88f, -0.04f), 0.09f, 0.09f,
                        steel, steel, true, true);

        /* 3-point stabilizing foot (enemy-mecha.jpg & enemy-mecha-rear.jpg) */
        /* Left front talon toe */
        mb_frustum(&b, v3(-0.11f, -1.02f, 0.28f), 0.15f, 0.44f,
                       v3(-0.09f, -0.86f, 0.12f), 0.13f, 0.26f,
                       steel, dark, steel, steel);
        /* Right front talon toe */
        mb_frustum(&b, v3( 0.11f, -1.02f, 0.28f), 0.15f, 0.44f,
                       v3( 0.09f, -0.86f, 0.12f), 0.13f, 0.26f,
                       steel, dark, steel, steel);
        /* Rear stabilizing heel spur talon (tripod stability) */
        mb_frustum(&b, v3( 0.00f, -1.02f, -0.26f), 0.15f, 0.30f,
                       v3( 0.00f, -0.86f, -0.12f), 0.13f, 0.20f,
                       steel, dark, steel, steel);
        if (side == 0) mm.shin_l = mb_finish(&b); else mm.shin_r = mb_finish(&b);
    }
    /* PS2-grade normal smoothing on curved mecha cowlings & hydraulic joints */
    mesh_smooth_normals(&mm.head, 0.92f);
    mesh_smooth_normals(&mm.torso, 0.82f);
    mesh_smooth_normals(&mm.arm_l, 0.92f);
    mesh_smooth_normals(&mm.arm_r, 0.92f);
    mesh_smooth_normals(&mm.leg_l, 0.92f);
    mesh_smooth_normals(&mm.leg_r, 0.92f);
    mesh_smooth_normals(&mm.shin_l, 0.92f);
    mesh_smooth_normals(&mm.shin_r, 0.92f);
    return mm;
}

void mecha_mesh_free(MechaMesh *m) {
    if (!m) return;
    mesh_free(&m->pelvis); mesh_free(&m->torso); mesh_free(&m->head);
    mesh_free(&m->arm_l);  mesh_free(&m->arm_r);
    mesh_free(&m->leg_l);  mesh_free(&m->leg_r);
    mesh_free(&m->shin_l); mesh_free(&m->shin_r);
}

/* ---------- first-person tactical pistol + gloved hands viewmodel ----- */
Viewmodel viewmodel_new(void) {
    Viewmodel vm;
    UVRect camo   = { 0.0f, 0.0f, 0.42f, 0.85f };
    UVRect glove  = { 0.02f, 0.02f, 0.48f, 0.48f };
    UVRect steel  = { 0.02f, 0.52f, 0.98f, 0.98f };
    UVRect baffle = { 0.76f, 0.02f, 0.98f, 0.48f };

    /* Right & left angled camo forearms entering from bottom-right / bottom-left */
    {
        MBuilder b; mb_init(&b);
        V3 f = v3_norm(v3(-0.08f, 0.16f, 0.25f));
        V3 r = v3_norm(v3_cross(v3(0, 1, 0), f));
        V3 u = v3_cross(f, r);
        mb_obb(&b, v3(0.125f, -0.255f, 0.27f), r, u, f,
               0.086f, 0.082f, 0.28f, camo, camo, camo, camo);
        vm.sleeve_r = mb_finish(&b);
    }
    {
        MBuilder b; mb_init(&b);
        V3 f = v3_norm(v3(0.20f, 0.17f, 0.24f));
        V3 r = v3_norm(v3_cross(v3(0, 1, 0), f));
        V3 u = v3_cross(f, r);
        mb_obb(&b, v3(-0.075f, -0.265f, 0.29f), r, u, f,
               0.084f, 0.080f, 0.30f, camo, camo, camo, camo);
        vm.sleeve_l = mb_finish(&b);
    }
    /* Sculpted right shooting glove & left support glove */
    {
        MBuilder b; mb_init(&b);
        mb_frustum(&b, v3(0.068f, -0.185f, 0.385f), 0.078f, 0.092f,
                       v3(0.062f, -0.120f, 0.435f), 0.072f, 0.084f,
                       glove, glove, glove, glove);
        /* Thumb locking high along left side of frame */
        mb_frustum(&b, v3(0.036f, -0.128f, 0.435f), 0.024f, 0.056f,
                       v3(0.038f, -0.105f, 0.455f), 0.022f, 0.052f,
                       glove, glove, glove, glove);
        vm.glove_r = mb_finish(&b);
    }
    {
        MBuilder b; mb_init(&b);
        mb_frustum(&b, v3(0.034f, -0.192f, 0.405f), 0.076f, 0.090f,
                       v3(0.042f, -0.130f, 0.455f), 0.070f, 0.082f,
                       glove, glove, glove, glove);
        vm.glove_l = mb_finish(&b);
    }
    /* Two-tone tactical pistol frame (matte polymer grip, rail & trigger guard) */
    {
        MBuilder b; mb_init(&b);
        mb_frustum(&b, v3(0.060f, -0.205f, 0.405f), 0.034f, 0.052f,
                       v3(0.060f, -0.100f, 0.435f), 0.036f, 0.055f,
                       glove, glove, glove, glove);
        mb_frustum(&b, v3(0.060f, -0.118f, 0.515f), 0.035f, 0.215f,
                       v3(0.060f, -0.094f, 0.515f), 0.036f, 0.215f,
                       glove, glove, glove, glove);
        /* Threaded tactical espionage suppressor on muzzle */
        mb_oct_ring(&b, v3(0.060f, -0.080f, 0.630f), 0.017f, 0.017f,
                        v3(0.060f, -0.080f, 0.765f), 0.017f, 0.017f,
                        baffle, baffle, true, true);
        vm.pistol_frame = mb_finish(&b);
    }
    /* Brushed stainless steel slide + front/rear iron sights (blows back on fire!) */
    {
        MBuilder b; mb_init(&b);
        mb_frustum(&b, v3(0.060f, -0.094f, 0.520f), 0.040f, 0.224f,
                       v3(0.060f, -0.065f, 0.520f), 0.037f, 0.222f,
                       steel, steel, steel, steel);
        mb_frustum(&b, v3(0.048f, -0.067f, 0.416f), 0.008f, 0.016f,
                       v3(0.048f, -0.058f, 0.420f), 0.007f, 0.014f,
                       glove, glove, glove, glove);
        mb_frustum(&b, v3(0.072f, -0.067f, 0.416f), 0.008f, 0.016f,
                       v3(0.072f, -0.058f, 0.420f), 0.007f, 0.014f,
                       glove, glove, glove, glove);
        mb_frustum(&b, v3(0.060f, -0.065f, 0.614f), 0.007f, 0.014f,
                       v3(0.060f, -0.055f, 0.618f), 0.006f, 0.012f,
                       steel, steel, steel, steel);
        vm.pistol_slide = mb_finish(&b);
    }
    return vm;
}

void viewmodel_free(Viewmodel *vm) {
    if (!vm) return;
    mesh_free(&vm->sleeve_l); mesh_free(&vm->sleeve_r);
    mesh_free(&vm->glove_l);  mesh_free(&vm->glove_r);
    mesh_free(&vm->pistol_frame);
    mesh_free(&vm->pistol_slide);
}
