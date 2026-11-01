/* Greyport night espionage dockyard, flooded harbor canal & warehouse street.
 * Features real-time planar mirrored water reflections, animated ABR water
 * ripples, shootable sodium streetlamps, classified intel terminals, and
 * volumetric searchlights. Shared by main.c, play.c, and tools/vistest.c. */
#include "greyport.h"

static void add_aabb(GreyportScene *sc, float x0, float y0, float z0,
                     float x1, float y1, float z1) {
    if (sc->nworld < GREYPORT_MAX_AABBS)
        sc->world[sc->nworld++] = (AABB){ v3(x0, y0, z0), v3(x1, y1, z1) };
}

/* Vertical hanging Regime Three-Bars banner mapped to top half of TEX_INSIGNIA */
static Mesh make_banner_mesh(float sx, float sy, float sz) {
    Mesh m = mesh_box(sx, sy, sz);
    for (int i = 0; i < m.nv; i++) {
        m.verts[i].u = 0.14f + m.verts[i].u * 0.72f;
        m.verts[i].v = 0.54f + m.verts[i].v * 0.44f;
    }
    return m;
}

static void render_catenary(FB *fb, const Camera *cam, V3 a, V3 b, float sag) {
    const int N = 8;
    V3 prev = a;
    for (int i = 1; i <= N; i++) {
        float t = (float)i / (float)N;
        V3 p = v3(lerpf(a.x, b.x, t),
                  lerpf(a.y, b.y, t) - sag * 4.0f * t * (1.0f - t),
                  lerpf(a.z, b.z, t));
        render_line3d(fb, cam, prev, p, 0x1e2638u, BLEND_OPAQUE);
        prev = p;
    }
}

void greyport_init(GreyportScene *sc) {
    memset(sc, 0, sizeof *sc);
    texgen_all(sc->tex);
    sc->have_brick_file = (tex_load_ppm("assets/brick.ppm", &sc->brick_file) == 0) ||
                          (tex_load_ppm("engine/assets/brick.ppm", &sc->brick_file) == 0);

    /* Main quay concrete pier (x:[-5.2 .. +38.8], z:[-8 .. +68]) + west barge pier */
    sc->ground       = mesh_plane_subdiv(44.0f, 76.0f, 22.0f, 38);
    sc->barge_deck   = mesh_plane_subdiv(8.0f,  22.0f,  6.0f, 12);
    sc->seawall_e    = mesh_box_tiled(0.55f, 1.15f, 26.0f, 2.2f, 2.0f);
    sc->seawall_w    = mesh_box_tiled(0.55f, 1.15f, 20.0f, 2.2f, 2.0f);
    sc->canal_rail   = mesh_box_tiled(0.12f, 0.86f, 15.0f, 1.8f, 1.8f);
    /* Harbor canal water surface (22x22 = 484 verts <= 1024 cap in render_water_plane) */
    sc->canal_water  = mesh_plane_subdiv(34.0f, 68.0f, 16.0f, 22);

    /* Raised curbed sidewalks flanking the wet asphalt street canyon */
    sc->sidewalk_w   = mesh_box_tiled(2.0f, 0.16f, 32.0f, 1.2f, 2.5f);
    sc->sidewalk_e   = mesh_box_tiled(2.0f, 0.16f, 32.0f, 1.2f, 2.5f);

    /* Elevated harbor viaduct spanning across the canal (DeBrief left-side bridge!) */
    sc->viaduct      = mesh_box_tiled(6.4f, 0.36f, 2.2f, 2.0f, 1.8f);
    sc->viaduct_rail = mesh_box_tiled(6.4f, 0.90f, 2.2f, 1.8f, 1.8f);

    /* Warehouse 1: cold-war dark masonry depot centered at (-4, 0, 22) -> x:[-9..1], z:[18..26] */
    sc->ware1        = mesh_box_tiled(10.0f, 5.0f, 8.0f, 2.5f, 2.0f);
    sc->ware1_plinth = mesh_box_tiled(10.2f, 0.55f, 8.2f, 2.5f, 2.5f);
    sc->ware1_belt   = mesh_box_tiled(10.22f, 0.22f, 8.22f, 2.5f, 0.4f);
    sc->pilaster     = mesh_box_tiled(0.52f, 5.02f, 0.52f, 0.5f, 2.0f);
    sc->ware1_roof   = mesh_box_tiled(10.4f, 0.32f, 8.4f, 2.5f, 2.5f);
    sc->ware1_door   = mesh_box_tiled(2.6f, 2.7f, 0.16f, 2.6f, 1.4f);
    sc->ware1_win    = mesh_box_tiled(6.4f, 1.3f, 0.12f, 1.6f, 1.6f);
    sc->ware1_rail   = mesh_box_tiled(10.0f, 0.85f, 8.0f, 2.0f, 2.5f);
    sc->bollard      = mesh_box(0.18f, 0.88f, 0.18f);

    /* Warehouse 2: corrugated steel cargo hall at (10, 0, 27.5) -> x:[4..16], z:[23..32] */
    sc->ware2        = mesh_box_tiled(12.0f, 7.0f, 9.0f, 3.0f, 2.2f);
    sc->ware2_base   = mesh_box_tiled(12.15f, 1.5f, 9.15f, 3.0f, 2.4f);
    sc->ware2_roof   = mesh_box_tiled(12.45f, 0.38f, 9.45f, 3.0f, 3.0f);
    sc->ware2_win    = mesh_box_tiled(8.0f, 1.5f, 0.14f, 2.0f, 2.0f);

    /* Overhead steel catwalk spanning street canyon */
    sc->catwalk      = mesh_box_tiled(17.0f, 0.28f, 2.2f, 4.0f, 1.6f);
    sc->catwalk_rail = mesh_box_tiled(17.0f, 0.90f, 2.2f, 4.0f, 1.6f);

    /* Fortified Watchtower at (9.0, 0, 19.5) — framed in upper-right sky behind left shoulder */
    sc->tower_shaft  = mesh_box_tiled(2.0f, 3.4f, 2.0f, 2.0f, 2.0f);
    sc->tower_cab    = mesh_box_tiled(3.6f, 0.55f, 3.6f, 2.0f, 1.4f);
    sc->tower_win    = mesh_box_tiled(2.6f, 1.25f, 2.6f, 2.6f, 1.3f);
    sc->tower_roof   = mesh_cone(0.40f, 2.85f, 1.10f, 4, true);
    sc->tower_rail   = mesh_box_tiled(3.55f, 0.95f, 3.55f, 1.6f, 1.6f);

    /* Dockyard shipping containers & file-loaded crate.obj */
    sc->container_a  = mesh_box_tiled(2.5f, 2.5f, 5.2f, 2.5f, 2.0f);
    sc->container_b  = mesh_box_tiled(2.4f, 2.4f, 4.6f, 2.4f, 2.0f);
    sc->container_c  = mesh_box_tiled(2.5f, 2.5f, 5.2f, 2.5f, 2.0f);
    sc->crate        = mesh_load_obj("assets/crate.obj");
    if (sc->crate.nv <= 0) sc->crate = mesh_load_obj("engine/assets/crate.obj");
    sc->have_crate   = (sc->crate.nv > 0);
    if (!sc->have_crate) sc->crate = mesh_box(1.2f, 1.2f, 1.2f);

    /* Cantilevered sodium streetlamps with dark hooded heads and recessed bulbs */
    sc->pole         = mesh_box(0.18f, 4.5f, 0.18f);
    sc->lamp_arm     = mesh_box(0.95f, 0.12f, 0.14f);
    sc->lamp_head    = mesh_box(0.48f, 0.22f, 0.32f);
    sc->lamp_bulb    = mesh_box(0.24f, 0.06f, 0.20f);

    /* Classified Intelligence Data Terminals */
    sc->term_base    = mesh_box_tiled(0.62f, 1.08f, 0.48f, 1.0f, 1.0f);
    sc->term_screen  = mesh_box(0.50f, 0.40f, 0.10f);

    /* Distant spy-city industrial skyline silhouettes */
    sc->skyline_a    = mesh_box_tiled(14.0f, 21.0f, 8.0f, 3.5f, 4.5f);
    sc->skyline_b    = mesh_box_tiled(11.0f, 25.0f, 8.0f, 3.5f, 4.5f);
    sc->skyline_c    = mesh_box_tiled(16.0f, 16.0f, 7.0f, 3.5f, 4.5f);

    /* Volumetric light cones, drop-shadow quad, wet reflection quad & claw */
    sc->banner_a     = make_banner_mesh(0.95f, 2.35f, 0.08f);
    sc->banner_b     = make_banner_mesh(0.08f, 2.65f, 1.05f);
    sc->crane_mast   = mesh_box_tiled(1.3f, 15.5f, 1.3f, 2.0f, 3.0f);
    sc->crane_jib    = mesh_box_tiled(8.5f, 0.85f, 0.95f, 2.0f, 2.5f);
    sc->beam_cone    = mesh_cone(3.4f, 0.20f, 14.5f, 12, false);
    sc->guard_cone   = mesh_cone(0.08f, 1.35f, 6.2f, 8, false);
    sc->shadow_quad  = mesh_quad_xz(1.0f, 1.0f);
    sc->puddle_quad  = mesh_quad_xz(1.4f, 9.6f);
    sc->claw         = mesh_grapple_claw();

    /* Physics AABBs matching visual meshes 1:1 */
    add_aabb(sc, -16.0f,  0.0f, 18.0f,  -6.0f,  5.0f, 26.0f);  /* ware1 */
    add_aabb(sc,  10.5f,  0.0f, 24.0f,  20.5f,  7.0f, 33.0f);  /* ware2 */
    add_aabb(sc,   8.0f,  0.0f, 18.5f,  10.0f,  3.4f, 20.5f);  /* watchtower shaft */
    add_aabb(sc,   7.2f,  3.4f, 17.7f,  10.8f,  3.95f, 21.3f); /* watchtower deck */
    add_aabb(sc,   1.0f,  0.0f, 16.2f,   2.2f,  1.2f, 17.4f);  /* crate */
    add_aabb(sc,  -6.0f,  5.06f, 24.4f,  9.5f,  5.34f, 26.6f); /* elevated catwalk spanning canyon */
    add_aabb(sc,  -4.5f,  0.0f, 13.5f,  -1.8f,  5.0f, 18.5f);  /* stacked containers on quay */
    add_aabb(sc,  11.8f,  0.0f, 14.8f,  14.2f,  2.4f, 19.2f);  /* container B */
    add_aabb(sc,  -5.45f, 0.0f,  2.0f,  -5.05f, 0.88f, 17.8f);  /* quay canal guardrail */
    add_aabb(sc, -12.2f,  4.75f, 18.9f,  -5.8f,  5.12f, 21.1f); /* west harbor viaduct */

    /* Grapple anchors (watchtower, streetlamps, warehouse roofs, catwalks, viaduct) */
    sc->anchors[0] = v3( 9.0f,  4.20f, 19.5f);
    sc->anchors[1] = v3( 7.0f,  4.6f,  24.5f);
    sc->anchors[2] = v3(-8.0f,  4.6f,  28.0f);
    sc->anchors[3] = v3( 0.8f,  5.3f,  25.5f);
    sc->anchors[4] = v3( 8.2f,  7.3f,  25.0f);
    sc->anchors[5] = v3( 1.75f, 5.4f,  25.5f);
    sc->anchors[6] = v3(-8.8f,  5.2f,  20.0f);
    sc->nanchors = 7;

    /* Shootable sodium lights (0: watchtower searchlight, 1: east streetlamp, 2: quay canal lamp) */
    sc->lamp_pos[0] = v3( 9.0f,  4.20f, 19.0f);
    sc->lamp_pos[1] = v3( 6.7f,  4.26f, 24.5f);
    sc->lamp_pos[2] = v3(-4.8f,  4.32f, 13.6f);

    /* Classified Intel Terminals (0: Warehouse 1 bay, 1: Overhead catwalk) */
    sc->term_pos[0] = v3(-6.9f, 0.0f, 17.3f);
    sc->term_pos[1] = v3( 1.75f, 5.34f, 25.5f);

    greyport_update_lights(sc, NULL, 0, 0.0f);
}

void greyport_free(GreyportScene *sc) {
    if (!sc) return;
    if (sc->have_brick_file) free(sc->brick_file.px);
    texgen_free(sc->tex);
    mesh_free(&sc->ground); mesh_free(&sc->barge_deck);
    mesh_free(&sc->seawall_e); mesh_free(&sc->seawall_w);
    mesh_free(&sc->canal_rail); mesh_free(&sc->canal_water);
    mesh_free(&sc->sidewalk_w); mesh_free(&sc->sidewalk_e);
    mesh_free(&sc->viaduct); mesh_free(&sc->viaduct_rail);
    mesh_free(&sc->ware1); mesh_free(&sc->ware1_plinth);
    mesh_free(&sc->ware1_belt); mesh_free(&sc->pilaster);
    mesh_free(&sc->ware1_roof); mesh_free(&sc->ware1_door);
    mesh_free(&sc->ware1_win); mesh_free(&sc->ware1_rail);
    mesh_free(&sc->bollard);
    mesh_free(&sc->ware2); mesh_free(&sc->ware2_base);
    mesh_free(&sc->ware2_roof); mesh_free(&sc->ware2_win);
    mesh_free(&sc->catwalk); mesh_free(&sc->catwalk_rail);
    mesh_free(&sc->tower_shaft); mesh_free(&sc->tower_cab);
    mesh_free(&sc->tower_win); mesh_free(&sc->tower_roof);
    mesh_free(&sc->tower_rail);
    mesh_free(&sc->container_a); mesh_free(&sc->container_b); mesh_free(&sc->container_c);
    mesh_free(&sc->crate);
    mesh_free(&sc->pole); mesh_free(&sc->lamp_arm);
    mesh_free(&sc->lamp_head); mesh_free(&sc->lamp_bulb);
    mesh_free(&sc->term_base); mesh_free(&sc->term_screen);
    mesh_free(&sc->skyline_a); mesh_free(&sc->skyline_b); mesh_free(&sc->skyline_c);
    mesh_free(&sc->banner_a); mesh_free(&sc->banner_b);
    mesh_free(&sc->crane_mast); mesh_free(&sc->crane_jib);
    mesh_free(&sc->beam_cone); mesh_free(&sc->guard_cone);
    mesh_free(&sc->shadow_quad); mesh_free(&sc->puddle_quad);
    mesh_free(&sc->claw);
}

void greyport_update_lights(GreyportScene *sc, const Guard *guards, int nguards, float time_s) {
    int nl = 0;
    if (!sc->lamp_out[0])
        sc->lights[nl++] = (PointLight){ v3( 9.0f,  5.2f, 19.5f), 24.0f, 1.25f, 0.95f, 0.55f };
    if (!sc->lamp_out[1])
        sc->lights[nl++] = (PointLight){ v3( 7.0f,  4.5f, 24.5f), 16.5f, 1.18f, 0.76f, 0.28f };
    if (!sc->lamp_out[2])
        sc->lights[nl++] = (PointLight){ v3(-4.8f,  4.5f, 13.6f), 14.0f, 1.05f, 0.68f, 0.24f };

    sc->lights[nl++] = (PointLight){ v3(-8.0f,  4.5f, 28.0f), 15.0f, 1.05f, 0.62f, 0.20f };
    sc->lights[nl++] = (PointLight){ v3(-7.5f,  3.6f, 17.6f),  8.5f, 0.62f, 0.44f, 0.20f };

    /* Subtle emerald/cyan glow from Intel Terminal 0 */
    sc->lights[nl++] = (PointLight){
        v3(sc->term_pos[0].x, 1.3f, sc->term_pos[0].z - 0.3f), 4.2f,
        sc->term_hacked[0] ? 0.12f : 0.18f,
        sc->term_hacked[0] ? 0.58f : 0.44f,
        sc->term_hacked[0] ? 0.30f : 0.48f
    };

    if (sc->muzzle_flash > 0.01f && nl < GREYPORT_MAX_LIGHTS) {
        float mf = sc->muzzle_flash;
        sc->lights[nl++] = (PointLight){ sc->muzzle_pos, 7.5f, 1.35f * mf, 0.92f * mf, 0.42f * mf };
    }

    int ns = 0;
    if (!sc->lamp_out[1]) {
        sc->spots[ns++] = (SpotLight){
            v3(7.0f, 4.5f, 24.5f), v3(0.0f, -1.0f, 0.0f),
            12.0f, 0.78f, 0.38f, 1.25f, 0.78f, 0.30f
        };
    }
    if (!sc->lamp_out[2]) {
        sc->spots[ns++] = (SpotLight){
            v3(-4.8f, 4.5f, 13.6f), v3(-0.22f, -0.97f, 0.0f),
            11.5f, 0.78f, 0.38f, 1.00f, 0.64f, 0.22f
        };
    }
    if (!sc->lamp_out[0]) {
        float sweep = sinf(time_s * 0.55f) * 0.22f;
        V3 sdir = v3_norm(v3(-0.62f + sweep, -0.38f, -0.68f));
        sc->spots[ns++] = (SpotLight){
            v3(9.0f, 4.8f, 19.0f), sdir,
            32.0f, 0.90f, 0.74f, 1.25f, 1.15f, 0.92f
        };
    }

    for (int i = 0; i < nguards && ns < GREYPORT_MAX_SPOTS; i++) {
        const Guard *g = &guards[i];
        if (g->stunned > 0.0f) continue;
        float al = clampf(g->alert, 0.0f, 1.0f);
        V3 fdir = v3_norm(v3(sinf(g->yaw), -0.16f, cosf(g->yaw)));
        V3 fpos = v3(g->pos.x + sinf(g->yaw) * 0.35f,
                     g->pos.y + 1.15f,
                     g->pos.z + cosf(g->yaw) * 0.35f);
        sc->spots[ns++] = (SpotLight){
            fpos, fdir, 14.5f, 0.91f, 0.72f,
            lerpf(0.85f, 1.25f, al),
            lerpf(0.88f, 0.45f, al),
            lerpf(0.95f, 0.25f, al)
        };
    }

    sc->rig = (LightRig){
        sc->lights, nl,
        0.18f, 0.22f, 0.32f,
        sc->spots, ns,
        0.11f, 0.16f, 0.26f, 48.0f
    };
}

void greyport_render_opaque(FB *fb, const GreyportScene *sc, const Camera *cam, float time_s) {
    fb_sky_cam(fb, cam);
    fb_clear_depth(fb);

    const LightRig *rig = &sc->rig;
    /* Always render cold-war oxblood/umber masonry from procedural generator for fidelity */
    const Tex *brick = &sc->tex[TEX_BRICK];
    const float wy = GREYPORT_WATER_Y;

    /* ---- PASS 1: Real-time planar mirrored reflections in harbor canal ----
     * Drawn into fb->px with mutual 1/z sorting, then zb is fast-cleared so
     * all solid quay piers & seawalls naturally mask the reflection to ONLY
     * appear inside the open harbor canal! */
    render_mesh_mirror(fb, cam, &sc->skyline_a, v3(-18.0f, 0.0f, 56.0f), 0.0f,
                       wy, time_s, &sc->tex[TEX_WINDOW], 0.62f, 0.68f, 0.88f, rig, RF_NONE);
    render_mesh_mirror(fb, cam, &sc->skyline_b, v3(  3.0f, 0.0f, 60.0f), 0.0f,
                       wy, time_s, &sc->tex[TEX_WINDOW], 0.62f, 0.68f, 0.88f, rig, RF_NONE);
    render_mesh_mirror(fb, cam, &sc->ware1, v3(-11.0f, 0.0f, 22.0f), 0.0f,
                       wy, time_s, brick, 0.68f, 0.68f, 0.78f, rig, RF_NONE);
    render_mesh_mirror(fb, cam, &sc->ware1_win, v3(-11.4f, 3.15f, 17.96f), 0.0f,
                       wy, time_s, &sc->tex[TEX_WINDOW], 0.90f, 0.78f, 0.58f, rig, RF_UNLIT);
    render_mesh_mirror(fb, cam, &sc->container_a, v3(-3.2f, 0.0f, 16.2f), -0.08f,
                       wy, time_s, &sc->tex[TEX_HAZARD], 0.65f, 0.68f, 0.78f, rig, RF_NONE);
    render_mesh_mirror(fb, cam, &sc->viaduct, v3(-9.0f, 4.78f, 20.0f), 0.0f,
                       wy, time_s, &sc->tex[TEX_CONCRETE], 0.65f, 0.68f, 0.78f, rig, RF_NONE);
    render_mesh_mirror(fb, cam, &sc->crane_mast, v3(-13.8f, 0.0f, 31.0f), 0.0f,
                       wy, time_s, &sc->tex[TEX_GRATE], 0.60f, 0.64f, 0.76f, rig, RF_DOUBLE_SIDED);
    render_mesh_mirror(fb, cam, &sc->crane_jib, v3(-10.4f, 14.8f, 31.0f), -0.18f,
                       wy, time_s, &sc->tex[TEX_GRATE], 0.60f, 0.64f, 0.76f, rig, RF_DOUBLE_SIDED);
    render_mesh_mirror(fb, cam, &sc->tower_shaft, v3(9.0f, 0.0f, 19.5f), 0.0f,
                       wy, time_s, &sc->tex[TEX_CONCRETE], 0.65f, 0.68f, 0.78f, rig, RF_NONE);
    if (!sc->lamp_out[0]) {
        render_mesh_mirror(fb, cam, &sc->tower_win, v3(9.0f, 3.95f, 19.5f), 0.0f,
                           wy, time_s, &sc->tex[TEX_WINDOW], 1.10f, 0.92f, 0.62f, rig, RF_UNLIT);
    }

    /* Reset depth buffer in 0.03ms via SIMD ps15_fill32 while preserving stats */
    RenderStats refl_stats = fb->stats;
    ps15_fill32((uint32_t *)fb->zb, 0u, (size_t)(fb->w * fb->h));
    fb->stats = refl_stats;

    /* ---- PASS 2: Opaque spy-city skyline, piers, seawalls & warehouses ---- */
    render_mesh(fb, cam, &sc->skyline_a, v3(-18.0f, 0.0f, 56.0f), 0.0f,
                &sc->tex[TEX_WINDOW], 0.85f, 0.85f, 0.95f, rig);
    render_mesh(fb, cam, &sc->skyline_b, v3(  3.0f, 0.0f, 60.0f), 0.0f,
                &sc->tex[TEX_WINDOW], 0.85f, 0.85f, 0.95f, rig);
    render_mesh(fb, cam, &sc->skyline_c, v3( 24.0f, 0.0f, 54.0f), 0.0f,
                &sc->tex[TEX_WINDOW], 0.80f, 0.82f, 0.92f, rig);

    /* Main dockyard wet-slate asphalt pier (with Blinn-Phong RF_WET_SPEC specular streaks!) */
    render_mesh_ex(fb, cam, &sc->ground, v3(16.8f, 0.0f, 30.0f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_CONCRETE], 0.98f, 0.98f, 1.02f, rig,
                   BLEND_OPAQUE, RF_WET_SPEC);
    render_mesh_ex(fb, cam, &sc->barge_deck, v3(-12.5f, 0.0f, 17.0f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_CONCRETE], 0.94f, 0.96f, 1.02f, rig,
                   BLEND_OPAQUE, RF_WET_SPEC);

    /* Raised curbed sidewalks flanking street canyon (greyport-street.jpg mood) */
    render_mesh(fb, cam, &sc->sidewalk_w, v3(-0.2f, 0.08f, 18.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.88f, 0.90f, 0.96f, rig);
    render_mesh(fb, cam, &sc->sidewalk_e, v3( 5.2f, 0.08f, 18.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.88f, 0.90f, 0.96f, rig);

    /* Stepped concrete quay seawalls with yellow caution stripes rising out of canal */
    render_mesh(fb, cam, &sc->seawall_e, v3(-5.18f, -1.10f, 5.2f), 0.0f,
                &sc->tex[TEX_HAZARD], 0.92f, 0.94f, 0.98f, rig);
    render_mesh(fb, cam, &sc->seawall_w, v3(-9.05f, -1.10f, 17.0f), 0.0f,
                &sc->tex[TEX_HAZARD], 0.90f, 0.92f, 0.96f, rig);
    render_mesh_ex(fb, cam, &sc->canal_rail, v3(-5.18f, 0.02f, 9.8f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.92f, 0.95f, 1.02f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);

    /* Elevated viaduct spanning over the harbor canal + harbor gantry crane */
    render_mesh(fb, cam, &sc->viaduct, v3(-9.0f, 4.78f, 20.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.86f, 0.88f, 0.95f, rig);
    render_mesh_ex(fb, cam, &sc->viaduct_rail, v3(-9.0f, 5.12f, 20.0f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.90f, 0.94f, 1.0f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->crane_mast, v3(-13.8f, 0.0f, 31.0f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.82f, 0.86f, 0.95f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->crane_jib, v3(-10.4f, 14.8f, 31.0f), -0.18f, 0.08f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.82f, 0.86f, 0.95f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);

    /* Warehouse 1: cold-war dark masonry depot with stone belt-course, pilasters & roll-up door */
    render_mesh(fb, cam, &sc->ware1, v3(-11.0f, 0.0f, 22.0f), 0.0f,
                brick, 1.0f, 0.98f, 0.96f, rig);
    render_mesh(fb, cam, &sc->ware1_plinth, v3(-11.0f, 0.0f, 22.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.78f, 0.80f, 0.85f, rig);
    render_mesh(fb, cam, &sc->ware1_belt, v3(-11.0f, 2.65f, 22.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.84f, 0.86f, 0.92f, rig);
    render_mesh(fb, cam, &sc->pilaster, v3(-6.1f, 2.50f, 18.1f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.82f, 0.85f, 0.90f, rig);
    render_mesh(fb, cam, &sc->pilaster, v3(-15.9f, 2.50f, 18.1f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.82f, 0.85f, 0.90f, rig);
    render_mesh(fb, cam, &sc->pilaster, v3(-6.1f, 2.50f, 25.9f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.82f, 0.85f, 0.90f, rig);
    render_mesh(fb, cam, &sc->pilaster, v3(-15.9f, 2.50f, 25.9f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.82f, 0.85f, 0.90f, rig);
    render_mesh(fb, cam, &sc->banner_a, v3(-6.82f, 2.15f, 17.93f), 0.0f,
                &sc->tex[TEX_INSIGNIA], 1.05f, 1.0f, 1.0f, rig);
    render_mesh(fb, cam, &sc->ware1_roof, v3(-11.0f, 4.92f, 22.0f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.85f, 0.88f, 0.94f, rig);
    render_mesh(fb, cam, &sc->ware1_door, v3(-8.4f, 0.0f, 17.95f), 0.0f,
                &sc->tex[TEX_METAL], 0.88f, 0.90f, 0.95f, rig);
    render_mesh(fb, cam, &sc->bollard, v3(-7.05f, 0.44f, 18.15f), 0.0f,
                &sc->tex[TEX_HAZARD], 0.92f, 0.90f, 0.85f, rig);
    render_mesh(fb, cam, &sc->bollard, v3(-9.75f, 0.44f, 18.15f), 0.0f,
                &sc->tex[TEX_HAZARD], 0.92f, 0.90f, 0.85f, rig);
    render_mesh_ex(fb, cam, &sc->ware1_win, v3(-11.4f, 3.15f, 17.96f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_WINDOW], 0.92f, 0.86f, 0.76f, rig,
                   BLEND_OPAQUE, RF_UNLIT);
    render_mesh_ex(fb, cam, &sc->ware1_rail, v3(-11.0f, 5.20f, 22.0f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.95f, 0.98f, 1.05f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);

    /* Warehouse 2: shifted east so Watchtower stands against open night sky */
    render_mesh(fb, cam, &sc->ware2, v3(15.5f, 0.0f, 27.5f), 0.0f,
                &sc->tex[TEX_METAL], 0.92f, 0.95f, 1.02f, rig);
    render_mesh(fb, cam, &sc->banner_b, v3(9.44f, 2.35f, 27.2f), 0.0f,
                &sc->tex[TEX_INSIGNIA], 1.05f, 1.0f, 1.0f, rig);
    render_mesh(fb, cam, &sc->ware2_base, v3(15.5f, 0.0f, 27.5f), 0.0f,
                &sc->tex[TEX_HAZARD], 0.95f, 0.95f, 0.98f, rig);
    render_mesh(fb, cam, &sc->ware2_roof, v3(15.5f, 6.90f, 27.5f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.82f, 0.85f, 0.92f, rig);
    render_mesh_ex(fb, cam, &sc->ware2_win, v3(15.5f, 4.6f, 22.95f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_WINDOW], 0.88f, 0.84f, 0.74f, rig,
                   BLEND_OPAQUE, RF_UNLIT);

    /* Overhead catwalk bridge spanning between Warehouse 1 & Warehouse 2 */
    render_mesh(fb, cam, &sc->catwalk, v3(1.75f, 5.20f, 25.5f), 0.0f,
                &sc->tex[TEX_METAL], 0.85f, 0.88f, 0.95f, rig);
    render_mesh_ex(fb, cam, &sc->catwalk_rail, v3(1.75f, 5.43f, 25.5f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.92f, 0.95f, 1.0f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);
    /* Catwalk structural steel support pillars to dock street */
    render_mesh(fb, cam, &sc->pole, v3(-1.2f, 2.60f, 25.5f), 0.0f,
                &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->pole, v3( 5.7f, 2.60f, 25.5f), 0.0f,
                &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);

    /* Fortified Watchtower at (9.0, 0, 19.5) — framed behind Operator's left shoulder */
    render_mesh(fb, cam, &sc->tower_shaft, v3(9.0f, 0.0f, 19.5f), 0.0f,
                &sc->tex[TEX_CONCRETE], 0.88f, 0.90f, 0.96f, rig);
    render_mesh(fb, cam, &sc->tower_cab, v3(9.0f, 3.40f, 19.5f), 0.0f,
                &sc->tex[TEX_METAL], 0.88f, 0.90f, 0.96f, rig);
    render_mesh_ex(fb, cam, &sc->tower_win, v3(9.0f, 3.95f, 19.5f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_WINDOW],
                   sc->lamp_out[0] ? 0.25f : 1.35f,
                   sc->lamp_out[0] ? 0.28f : 1.15f,
                   sc->lamp_out[0] ? 0.36f : 0.85f,
                   rig, BLEND_OPAQUE, RF_UNLIT);
    render_mesh(fb, cam, &sc->tower_roof, v3(9.0f, 5.20f, 19.5f), 0.785398f,
                &sc->tex[TEX_METAL], 0.82f, 0.85f, 0.92f, rig);
    render_mesh_ex(fb, cam, &sc->tower_rail, v3(9.0f, 3.95f, 19.5f), 0.0f, 0.0f, 1.0f,
                   &sc->tex[TEX_GRATE], 0.92f, 0.95f, 1.02f, rig,
                   BLEND_OPAQUE, RF_DOUBLE_SIDED);

    /* Shipping containers & file-loaded crate */
    render_mesh(fb, cam, &sc->container_a, v3(-3.2f, 0.0f, 16.2f), -0.08f,
                &sc->tex[TEX_HAZARD], 0.92f, 0.94f, 0.98f, rig);
    render_mesh(fb, cam, &sc->container_b, v3(-3.0f, 2.5f, 16.5f), -0.06f,
                &sc->tex[TEX_METAL],  0.88f, 0.90f, 0.95f, rig);
    render_mesh(fb, cam, &sc->container_c, v3(-5.2f, 0.0f, 13.5f), -0.18f,
                &sc->tex[TEX_HAZARD], 0.90f, 0.92f, 0.96f, rig);
    render_mesh(fb, cam, &sc->container_b, v3( 13.0f, 0.0f, 17.0f), -0.14f,
                &sc->tex[TEX_HAZARD], 0.88f, 0.90f, 0.95f, rig);
    render_mesh(fb, cam, &sc->crate, v3(1.6f, 0.0f, 16.8f), 0.38f,
                &sc->tex[TEX_METAL], 0.95f, 0.95f, 1.0f, rig);

    /* Classified Intelligence Terminals */
    for (int i = 0; i < GREYPORT_NUM_TERMS; i++) {
        V3 tp = sc->term_pos[i];
        render_mesh(fb, cam, &sc->term_base, tp, 0.0f,
                    &sc->tex[TEX_METAL], 0.82f, 0.86f, 0.94f, rig);
        float hr = sc->term_hacked[i] ? 0.55f : 1.05f;
        float hg = sc->term_hacked[i] ? 1.45f : 1.15f;
        float hb = sc->term_hacked[i] ? 0.75f : 1.25f;
        render_mesh_ex(fb, cam, &sc->term_screen,
                       v3(tp.x, tp.y + 1.02f, tp.z - 0.16f), 0.0f, -0.22f, 1.0f,
                       &sc->tex[TEX_TERMINAL], hr, hg, hb, rig,
                       BLEND_OPAQUE, RF_UNLIT);
    }

    /* Cantilevered sodium streetlamps with dark hooded heads and small glowing recessed bulbs */
    render_mesh(fb, cam, &sc->pole,     v3( 7.4f, 0.0f, 24.5f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_arm, v3( 7.0f, 4.45f, 24.5f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_head, v3(6.7f, 4.38f, 24.5f), 0.0f, &sc->tex[TEX_METAL], 0.82f, 0.85f, 0.90f, rig);
    render_mesh_ex(fb, cam, &sc->lamp_bulb, v3(6.7f, 4.26f, 24.5f), 0.0f, 0.0f, 1.0f, NULL,
                   sc->lamp_out[1] ? 0.22f : 1.55f,
                   sc->lamp_out[1] ? 0.24f : 1.15f,
                   sc->lamp_out[1] ? 0.28f : 0.65f,
                   rig, BLEND_OPAQUE, RF_UNLIT);

    render_mesh(fb, cam, &sc->pole,     v3(-5.05f, 0.0f, 13.6f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_arm, v3(-4.95f, 4.45f, 13.6f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_head, v3(-4.8f, 4.38f, 13.6f), 0.0f, &sc->tex[TEX_METAL], 0.82f, 0.85f, 0.90f, rig);
    render_mesh_ex(fb, cam, &sc->lamp_bulb, v3(-4.8f, 4.26f, 13.6f), 0.0f, 0.0f, 1.0f, NULL,
                   sc->lamp_out[2] ? 0.22f : 1.50f,
                   sc->lamp_out[2] ? 0.24f : 1.10f,
                   sc->lamp_out[2] ? 0.28f : 0.62f,
                   rig, BLEND_OPAQUE, RF_UNLIT);

    render_mesh(fb, cam, &sc->pole,     v3(-8.4f, 0.0f, 28.0f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_arm, v3(-8.0f, 4.45f, 28.0f), 0.0f, &sc->tex[TEX_METAL], 0.75f, 0.78f, 0.85f, rig);
    render_mesh(fb, cam, &sc->lamp_head, v3(-7.7f, 4.38f, 28.0f), 0.0f, &sc->tex[TEX_METAL], 0.82f, 0.85f, 0.90f, rig);
    render_mesh_ex(fb, cam, &sc->lamp_bulb, v3(-7.7f, 4.26f, 28.0f), 0.0f, 0.0f, 1.0f, NULL,
                   1.55f, 1.15f, 0.65f, rig, BLEND_OPAQUE, RF_UNLIT);

    /* Crate grounded contact shadow + overhead sagging street power wires */
    render_blob_shadow(fb, cam, &sc->shadow_quad, &sc->tex[TEX_SHADOW],
                       v3(2.0f, 0.0f, 17.4f), 0.95f);
    render_catenary(fb, cam, v3(-2.60f, 4.75f, 18.2f), v3( 6.0f, 4.42f, 18.0f), 0.42f);
    render_catenary(fb, cam, v3( 6.0f,  4.42f, 18.0f), v3( 4.05f, 6.55f, 23.2f), 0.48f);
    render_catenary(fb, cam, v3(-5.05f, 4.42f, 13.6f), v3(-8.4f, 4.42f, 28.0f), 0.72f);
    render_catenary(fb, cam, v3(-0.9f,  4.85f, 25.8f), v3( 3.2f,  9.8f, 31.8f), 0.65f);
}

void greyport_render_fx(FB *fb, const GreyportScene *sc, const Camera *cam,
                        const Guard *guards, int nguards, float time_s) {
    /* 1. Animated PS1 ABR semi-transparent harbor canal water over planar reflections! */
    render_water_plane(fb, cam, &sc->canal_water, v3(-22.2f, GREYPORT_WATER_Y, 24.0f),
                       &sc->tex[TEX_WATER], time_s, 1.08f, 1.12f, 1.25f,
                       &sc->rig, BLEND_HALF);

    /* Shimmering golden sodium reflection streak on canal waves */
    if (!sc->lamp_out[2]) {
        render_mesh_ex(fb, cam, &sc->puddle_quad,
                       v3(-7.0f + sinf(time_s * 2.4f) * 0.15f, GREYPORT_WATER_Y + 0.03f, 13.8f),
                       0.0f, 0.0f, 1.15f, &sc->tex[TEX_SHADOW],
                       0.72f, 0.44f, 0.15f, &sc->rig,
                       BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    }

    /* 2. Luminous wet-pavement sodium specular reflection pools & streaks */
    if (!sc->lamp_out[1]) {
        render_mesh_ex(fb, cam, &sc->puddle_quad, v3(5.3f, 0.02f, 17.5f),
                       0.0f, 0.0f, 1.0f, &sc->tex[TEX_SHADOW],
                       0.65f, 0.42f, 0.14f, &sc->rig,
                       BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    }
    if (!sc->lamp_out[2]) {
        render_mesh_ex(fb, cam, &sc->puddle_quad, v3(-4.8f, 0.02f, 14.2f),
                       0.0f, 0.0f, 1.25f, &sc->tex[TEX_SHADOW],
                       0.60f, 0.38f, 0.12f, &sc->rig,
                       BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    }
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(-7.8f, 0.02f, 27.8f),
                   0.0f, 0.0f, 1.0f, &sc->tex[TEX_SHADOW],
                   0.48f, 0.30f, 0.10f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    /* Shimmering specular ground highlights underneath quay street canyon */
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(2.4f, 0.02f, 17.0f),
                   0.0f, 0.0f, 0.90f, &sc->tex[TEX_SHADOW],
                   0.55f, 0.34f, 0.11f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(-2.8f, 0.02f, 14.8f),
                   0.0f, 0.0f, 0.85f, &sc->tex[TEX_SHADOW],
                   0.22f, 0.28f, 0.42f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(1.2f, 0.02f, 14.8f),
                   0.0f, 0.0f, 1.15f, &sc->tex[TEX_SHADOW],
                   0.26f, 0.32f, 0.46f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    /* Foreground specular wet puddles (golden streetlamp sheen + cool nocturne sky sheen) */
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(0.6f, 0.02f, 12.8f),
                   0.0f, 0.0f, 1.05f, &sc->tex[TEX_SHADOW],
                   0.45f, 0.30f, 0.12f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(1.3f, 0.02f, 12.2f),
                   0.0f, 0.0f, 0.85f, &sc->tex[TEX_SHADOW],
                   0.18f, 0.25f, 0.38f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    render_mesh_ex(fb, cam, &sc->puddle_quad, v3(-0.4f, 0.02f, 13.5f),
                   0.0f, 0.0f, 1.15f, &sc->tex[TEX_SHADOW],
                   0.48f, 0.32f, 0.12f, &sc->rig,
                   BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);

    /* 3. Volumetric streetlamp & Watchtower searchlight cones */
    if (!sc->lamp_out[1]) {
        render_mesh_ex(fb, cam, &sc->beam_cone, v3(5.3f, 0.0f, 18.0f),
                       0.0f, 0.0f, 0.44f, &sc->tex[TEX_BEAM],
                       0.72f, 0.46f, 0.16f, &sc->rig,
                       BLEND_QUARTER, RF_UNLIT);
    }
    /* Diagonal volumetric searchlight sweeping down-left from Watchtower behind Operator */
    if (!sc->lamp_out[0]) {
        float sweep = sinf(time_s * 0.40f) * 0.14f;
        render_mesh_ex(fb, cam, &sc->beam_cone, v3(9.0f, 4.35f, 19.5f),
                       -2.32f + sweep, 2.15f + sweep * 0.35f, 1.0f, &sc->tex[TEX_BEAM],
                       0.85f, 0.90f, 1.05f, &sc->rig,
                       BLEND_ADD, RF_UNLIT | RF_DOUBLE_SIDED);
    }

    /* 4. Patrol guards' volumetric handheld flashlight cones */
    for (int i = 0; i < nguards; i++) {
        const Guard *g = &guards[i];
        if (g->stunned > 0.0f) continue;
        float al = clampf(g->alert, 0.0f, 1.0f);
        V3 fwd = v3(sinf(g->yaw), -0.14f, cosf(g->yaw));
        V3 base = v3_add(v3(g->pos.x, g->pos.y + 1.15f, g->pos.z),
                         v3_scale(fwd, 5.8f));
        render_mesh_ex(fb, cam, &sc->guard_cone, base,
                       g->yaw + PS15_PI, 1.43f, 0.92f, &sc->tex[TEX_BEAM],
                       lerpf(0.62f, 1.05f, al),
                       lerpf(0.68f, 0.38f, al),
                       lerpf(0.78f, 0.22f, al), &sc->rig,
                       BLEND_QUARTER, RF_UNLIT);
    }

    /* 5. Depth-occluded warm sodium lamp bloom halos */
    if (!sc->lamp_out[0])
        render_glow3d(fb, cam, sc->lamp_pos[0], 1.45f, 115, 76, 26);
    if (!sc->lamp_out[1])
        render_glow3d(fb, cam, sc->lamp_pos[1], 1.25f, 125, 74, 18);
    if (!sc->lamp_out[2])
        render_glow3d(fb, cam, sc->lamp_pos[2], 1.15f, 120, 72, 18);
    render_glow3d(fb, cam, v3(-7.7f, 4.28f, 28.0f), 1.25f, 120, 70, 16);

    /* 6. Deterministic 3D night rain streaks */
    render_rain(fb, cam, time_s, 110);
}

void greyport_render_grapple(FB *fb, const GreyportScene *sc, const Camera *cam,
                             V3 hand, V3 anchor) {
    /* Taut braided steel grapple cable + 3-prong steel claw at strike point */
    render_line3d(fb, cam, hand, anchor, 0xc8d8e8u, BLEND_OPAQUE);
    render_line3d(fb, cam, v3_add(hand, v3(0, 0.015f, 0)),
                  v3_add(anchor, v3(0, 0.015f, 0)), 0x7488a4u, BLEND_HALF);
    V3 d = v3_sub(anchor, hand);
    float yaw = atan2f(d.x, d.z);
    render_mesh_ex(fb, cam, &sc->claw, anchor, yaw, 0.35f, 1.0f,
                   &sc->tex[TEX_METAL], 1.15f, 1.18f, 1.25f,
                   &sc->rig, BLEND_OPAQUE, RF_NONE);
}

int greyport_fire_ray_ex(GreyportScene *sc, V3 orig, V3 dir,
                         Guard *guards, int nguards, Mecha *mecha, V3 *out_hit) {
    float best_t = 65.0f;
    int kind = GP_HIT_NONE, best_idx = -1;

    for (int i = 0; i < sc->nworld; i++) {
        float t = ray_hit_aabb(orig, dir, best_t, &sc->world[i]);
        if (t > 0.0f && t < best_t) { best_t = t; kind = GP_HIT_WALL; best_idx = i; }
    }
    if (dir.y < -1e-4f) {
        float tg = -orig.y / dir.y;
        if (tg > 0.01f && tg < best_t) { best_t = tg; kind = GP_HIT_WALL; }
    }
    /* Shootable sodium lamps */
    for (int i = 0; i < GREYPORT_NUM_LAMPS; i++) {
        if (sc->lamp_out[i]) continue;
        float t = ray_hit_sphere(orig, dir, best_t, sc->lamp_pos[i], 0.56f);
        if (t > 0.0f && t < best_t) { best_t = t; kind = GP_HIT_LAMP; best_idx = i; }
    }
    /* Regime Troopers: separate precision headshot vs. body takedown! */
    for (int i = 0; i < nguards; i++) {
        if (guards[i].stunned > 0.0f) continue;
        V3 head  = v3(guards[i].pos.x, guards[i].pos.y + 1.58f, guards[i].pos.z);
        V3 chest = v3(guards[i].pos.x, guards[i].pos.y + 1.02f, guards[i].pos.z);
        float th = ray_hit_sphere(orig, dir, best_t, head, 0.25f);
        if (th > 0.0f && th < best_t) { best_t = th; kind = GP_HIT_GUARD_HEAD; best_idx = i; continue; }
        float tb = ray_hit_sphere(orig, dir, best_t, chest, 0.48f);
        if (tb > 0.0f && tb < best_t) { best_t = tb; kind = GP_HIT_GUARD_BODY; best_idx = i; }
    }
    /* Towering Regime Patrol Walker (Mecha Type-1): subsystem hitboxes! */
    if (mecha && mecha->coolant_hp > 0.01f) {
        float sy = sinf(mecha->yaw), cy = cosf(mecha->yaw);
        V3 p_sens = v3(mecha->pos.x + sy * 0.34f, mecha->pos.y + 3.00f, mecha->pos.z + cy * 0.34f);
        V3 p_cool = v3(mecha->pos.x - sy * 0.44f, mecha->pos.y + 2.55f, mecha->pos.z - cy * 0.44f);
        V3 p_legs = v3(mecha->pos.x,              mecha->pos.y + 1.15f, mecha->pos.z);
        V3 p_hull = v3(mecha->pos.x,              mecha->pos.y + 2.45f, mecha->pos.z);

        float ts = ray_hit_sphere(orig, dir, best_t, p_sens, 0.36f);
        if (ts > 0.0f && ts < best_t) { best_t = ts; kind = GP_HIT_MECHA_SENSOR; }
        float tc = ray_hit_sphere(orig, dir, best_t, p_cool, 0.42f);
        if (tc > 0.0f && tc < best_t) { best_t = tc; kind = GP_HIT_MECHA_COOLANT; }
        float tl = ray_hit_sphere(orig, dir, best_t, p_legs, 0.68f);
        if (tl > 0.0f && tl < best_t) { best_t = tl; kind = GP_HIT_MECHA_ACTUATOR; }
        float th = ray_hit_sphere(orig, dir, best_t, p_hull, 0.82f);
        if (th > 0.0f && th < best_t) { best_t = th; kind = GP_HIT_MECHA_ARMOR; }
    }

    V3 hit = v3_add(orig, v3_scale(dir, best_t));
    if (out_hit) *out_hit = hit;

    if (kind == GP_HIT_LAMP && best_idx >= 0) {
        sc->lamp_out[best_idx] = true;
    } else if (kind == GP_HIT_GUARD_HEAD && best_idx >= 0) {
        guards[best_idx].stunned = 25.0f; /* precision neutralization */
        guards[best_idx].alert = 0.0f;
    } else if (kind == GP_HIT_GUARD_BODY && best_idx >= 0) {
        guards[best_idx].stunned = 12.0f;
        guards[best_idx].alert = 0.25f;
    } else if (mecha && kind == GP_HIT_MECHA_SENSOR) {
        mecha->sensor_hp = fmaxf(0.0f, mecha->sensor_hp - 0.36f);
        mecha->alert = fminf(1.0f, mecha->alert + 0.35f);
    } else if (mecha && kind == GP_HIT_MECHA_ACTUATOR) {
        mecha->actuator_hp = fmaxf(0.15f, mecha->actuator_hp - 0.28f);
        mecha->alert = fminf(1.0f, mecha->alert + 0.35f);
    } else if (mecha && kind == GP_HIT_MECHA_COOLANT) {
        mecha->coolant_hp = fmaxf(0.0f, mecha->coolant_hp - 0.52f);
        mecha->alert = fminf(1.0f, mecha->alert + 0.45f);
    } else if (kind == GP_HIT_WALL || kind == GP_HIT_MECHA_ARMOR) {
        for (int i = 0; i < nguards; i++)
            guard_hear_ping(&guards[i], hit, 14.0f);
    }
    return kind;
}

int greyport_fire_ray(GreyportScene *sc, V3 orig, V3 dir,
                      Guard *guards, int nguards, V3 *out_hit) {
    int k = greyport_fire_ray_ex(sc, orig, dir, guards, nguards, NULL, out_hit);
    if (k == GP_HIT_GUARD_HEAD) return GP_HIT_GUARD_BODY;
    return k;
}
