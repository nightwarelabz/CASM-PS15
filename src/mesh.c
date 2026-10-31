/* Mesh builders: boxes, ground planes, and the part-built humanoid. */
#include "engine.h"

static Mesh mesh_alloc(int nv, int nt) {
    Mesh m;
    m.verts = malloc(sizeof(MVert) * (size_t)nv);
    m.tris = malloc(sizeof(MTri) * (size_t)nt);
    m.nv = nv; m.nt = nt;
    return m;
}
void mesh_free(Mesh *m) { free(m->verts); free(m->tris); }

/* unit-ish box centred at origin base (y from 0..sy), full UV per face */
Mesh mesh_box(float sx, float sy, float sz) {
    Mesh m = mesh_alloc(24, 12);
    float hx = sx / 2, hz = sz / 2;
    /* faces: +z, -z, +x, -x, +y(top), -y — 4 verts each */
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
        m.tris[f * 2] = (MTri){ f * 4, f * 4 + 1, f * 4 + 2 };
        m.tris[f * 2 + 1] = (MTri){ f * 4, f * 4 + 2, f * 4 + 3 };
    }
    return m;
}

Mesh mesh_plane(float sx, float sz, float tile) {
    Mesh m = mesh_alloc(4, 2);
    float hx = sx / 2, hz = sz / 2;
    const float P[4][3] = { {-hx,0,-hz}, {hx,0,-hz}, {hx,0,hz}, {-hx,0,hz} };
    const float UV[4][2] = { {0,0}, {tile,0}, {tile,tile}, {0,tile} };
    for (int i = 0; i < 4; i++) {
        m.verts[i].p = v3(P[i][0], P[i][1], P[i][2]);
        m.verts[i].u = UV[i][0]; m.verts[i].v = UV[i][1];
        m.verts[i].n = v3(0, 1, 0);
    }
    m.tris[0] = (MTri){ 0, 2, 1 };
    m.tris[1] = (MTri){ 0, 3, 2 };
    return m;
}

/* Humanoid ~1.8 m. Parts are boxes; pivots are the joint points the
 * animator rotates around (shoulders, hips, neck). */
Humanoid humanoid_new(void) {
    Humanoid h;
    h.parts[PART_HIPS]  = mesh_box(0.36f, 0.22f, 0.22f);
    h.parts[PART_TORSO] = mesh_box(0.42f, 0.58f, 0.26f);
    h.parts[PART_HEAD]  = mesh_box(0.24f, 0.28f, 0.26f);
    h.parts[PART_ARM_L] = mesh_box(0.11f, 0.62f, 0.11f);
    h.parts[PART_ARM_R] = mesh_box(0.11f, 0.62f, 0.11f);
    h.parts[PART_LEG_L] = mesh_box(0.14f, 0.78f, 0.14f);
    h.parts[PART_LEG_R] = mesh_box(0.14f, 0.78f, 0.14f);
    /* pivot = where the part hangs from, in part-local space */
    h.pivot[PART_HIPS]  = v3(0, 0, 0);
    h.pivot[PART_TORSO] = v3(0, 0, 0);
    h.pivot[PART_HEAD]  = v3(0, 0, 0);
    h.pivot[PART_ARM_L] = v3(0, 0.60f, 0);   /* top of the arm box */
    h.pivot[PART_ARM_R] = v3(0, 0.60f, 0);
    h.pivot[PART_LEG_L] = v3(0, 0.76f, 0);   /* top of the leg box */
    h.pivot[PART_LEG_R] = v3(0, 0.76f, 0);
    return h;
}
void humanoid_free(Humanoid *h) {
    for (int i = 0; i < PART_COUNT; i++) mesh_free(&h->parts[i]);
}
