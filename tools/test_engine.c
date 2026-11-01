/* CASM-PS15 automated engine & C-vs-Assembly differential test suite.
 * Verifies bit-for-bit equality between ALL hand-written assembly kernels
 * (ARM64 NEON / x86_64 SSE2) and portable C fallbacks, plus rasterizer,
 * planar mirror winding, ABR blending, 3D raycasts, modern movement & TPS
 * camera, Patrol Mecha AI, procedural audio synths, OBJ/PPM & CRC32 saves. */
#include "engine.h"
#include "greyport.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void test_asm_fill32(void) {
    uint32_t a[128], b[128];
    Rng rng = { 0x12345678ULL };
    bool ok = true;
    for (int off = 0; off < 8; off++) {
        for (size_t n = 0; n <= 75; n++) {
            memset(a, 0xAA, sizeof a);
            memset(b, 0xAA, sizeof b);
            uint32_t val = rng_next(&rng);
            ps15_fill32(a + off, val, n);
            ps15_c_fill32(b + off, val, n);
            if (memcmp(a, b, sizeof a) != 0) { ok = false; break; }
        }
    }
    CHECK(ok, "ps15_fill32 bit-identical to ps15_c_fill32 across alignments & sizes");
}

static void test_asm_upscale2x(void) {
    const int W = 53, H = 19; /* deliberately non-multiple of 4 */
    uint32_t src[W * H], dst_asm[W * 2 * H * 2], dst_c[W * 2 * H * 2];
    Rng rng = { 0xBEEFCAFEULL };
    for (int i = 0; i < W * H; i++) src[i] = rng_next(&rng) & 0x00FFFFFFu;

    memset(dst_asm, 0, sizeof dst_asm);
    memset(dst_c, 0, sizeof dst_c);
    ps15_upscale2x(dst_asm, src, W, H);
    ps15_c_upscale2x(dst_c, src, W, H);
    CHECK(memcmp(dst_asm, dst_c, sizeof dst_asm) == 0,
          "ps15_upscale2x bit-identical to ps15_c_upscale2x (including odd tails)");

    memset(dst_asm, 0, sizeof dst_asm);
    memset(dst_c, 0, sizeof dst_c);
    ps15_upscale2x_crt(dst_asm, src, W, H);
    ps15_c_upscale2x_crt(dst_c, src, W, H);
    CHECK(memcmp(dst_asm, dst_c, sizeof dst_asm) == 0,
          "ps15_upscale2x_crt bit-identical to ps15_c_upscale2x_crt (odd tails + 25% CRT scanline)");
    CHECK(dst_asm[W * 2] <= dst_asm[0],
          "ps15_upscale2x_crt attenuates odd scanline rows");
}

static void test_asm_span_tex64(void) {
    Tex tex[TEX_COUNT];
    texgen_all(tex);
    uint32_t px_asm[FB_W], px_c[FB_W];
    float zb_asm[FB_W], zb_c[FB_W];
    Rng rng = { 0xCA515A5ULL };
    bool ok = true;

    for (int iter = 0; iter < 2000; iter++) {
        for (int i = 0; i < FB_W; i++) {
            px_asm[i] = px_c[i] = 0x112233u;
            float zinit = (rng_next(&rng) & 3) ? rng_f01(&rng) * 2.0f : 0.0f;
            zb_asm[i] = zb_c[i] = zinit;
        }
        int x = (int)(rng_next(&rng) % (FB_W - 32));
        int cnt = 1 + (int)(rng_next(&rng) % 24);
        int tid = (int)(rng_next(&rng) % TEX_COUNT);

        Span64 s;
        s.x = x;
        s.count = cnt;
        s.tex = tex[tid].px;
        s.invz = 0.05f + rng_f01(&rng) * 2.5f;
        s.dinvz = (rng_f01(&rng) - 0.5f) * 0.08f;
        s.u  = rng_next(&rng);
        s.du = rng_next(&rng) >> 10;
        s.v  = rng_next(&rng);
        s.dv = rng_next(&rng) >> 10;
        s.r  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.dr = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);
        s.g  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.dg = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);
        s.b  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.db = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);

        int fog = (int)(rng_next(&rng) % 257u);
        s.inv_fog = 256 - fog;
        s.fogr = fog * 28;
        s.fogg = fog * 44;
        s.fogb = fog * 72;
        s.dither[0] = -4; s.dither[1] = 1; s.dither[2] = -2; s.dither[3] = 3;

        s.row_px = px_asm; s.row_zb = zb_asm;
        ps15_span_tex64(&s);

        s.row_px = px_c; s.row_zb = zb_c;
        ps15_c_span_tex64(&s);

        if (memcmp(px_asm, px_c, sizeof px_asm) != 0 ||
            memcmp(zb_asm, zb_c, sizeof zb_asm) != 0) {
            ok = false;
            fprintf(stderr, "span_tex64 mismatch at iter %d (tid=%d x=%d cnt=%d)\n",
                    iter, tid, x, cnt);
            break;
        }
    }
    texgen_free(tex);
    CHECK(ok, "ps15_span_tex64 100% bit-identical to ps15_c_span_tex64 across 2000 random spans");
}

static void test_asm_span_abr64(void) {
    Tex tex[TEX_COUNT];
    texgen_all(tex);
    uint32_t px_asm[FB_W], px_c[FB_W];
    float zb_asm[FB_W], zb_c[FB_W];
    Rng rng = { 0xAB264015ULL };
    bool ok = true;

    for (int iter = 0; iter < 2400; iter++) {
        for (int i = 0; i < FB_W; i++) {
            uint32_t bg = rng_next(&rng) & 0x00FFFFFFu;
            px_asm[i] = px_c[i] = bg;
            float zinit = (rng_next(&rng) & 3) ? rng_f01(&rng) * 2.0f : 0.0f;
            zb_asm[i] = zb_c[i] = zinit;
        }
        int x = (int)(rng_next(&rng) % (FB_W - 32));
        int cnt = 1 + (int)(rng_next(&rng) % 24);
        int tid = (int)(rng_next(&rng) % TEX_COUNT);
        int mode = 1 + (iter % 4); /* BLEND_HALF..BLEND_QUARTER */

        Span64 s;
        s.x = x;
        s.count = cnt;
        s.tex = tex[tid].px;
        s.invz = 0.05f + rng_f01(&rng) * 2.5f;
        s.dinvz = (rng_f01(&rng) - 0.5f) * 0.08f;
        s.u  = rng_next(&rng);
        s.du = rng_next(&rng) >> 10;
        s.v  = rng_next(&rng);
        s.dv = rng_next(&rng) >> 10;
        s.r  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.dr = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);
        s.g  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.dg = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);
        s.b  = (int32_t)((rng_f01(&rng) * 320.0f - 20.0f) * 65536.0f);
        s.db = (int32_t)((rng_f01(&rng) - 0.5f) * 16.0f * 65536.0f);

        int fog = (int)(rng_next(&rng) % 257u);
        s.inv_fog = 256 - fog;
        s.fogr = fog * 28;
        s.fogg = fog * 44;
        s.fogb = fog * 72;
        s.dither[0] = -4; s.dither[1] = 1; s.dither[2] = -2; s.dither[3] = 3;

        s.row_px = px_asm; s.row_zb = zb_asm;
        ps15_span_abr64(&s, mode);

        s.row_px = px_c; s.row_zb = zb_c;
        ps15_c_span_abr64(&s, mode);

        if (memcmp(px_asm, px_c, sizeof px_asm) != 0 ||
            memcmp(zb_asm, zb_c, sizeof zb_asm) != 0) {
            ok = false;
            fprintf(stderr, "span_abr64 mismatch at iter %d (mode=%d tid=%d x=%d cnt=%d)\n",
                    iter, mode, tid, x, cnt);
            break;
        }
    }
    texgen_free(tex);
    CHECK(ok, "ps15_span_abr64 100% bit-identical to ps15_c_span_abr64 across all 4 ABR modes");
}

static void test_raster_and_blends(void) {
    FB *fb = fb_new(FB_W, FB_H);
    fb_clear_depth(fb);
    CHECK(fb_depth_at(fb, 32, 32) > 1e8f, "cleared 1/z depth reports far plane");

    /* Back-facing triangle (area < 0) is culled unless RF_DOUBLE_SIDED or RF_MIRROR_Y */
    SVertEx a = { 10, 10, 5.0f, 0, 0, 1, 1, 1 };
    SVertEx b = { 50, 10, 5.0f, 1, 0, 1, 1, 1 };
    SVertEx c = { 10, 50, 5.0f, 0, 1, 1, 1, 1 };
    raster_tri_ex(fb, a, c, b, NULL, 0, 0, BLEND_OPAQUE, RF_NONE, 0, 0, 0, 0);
    CHECK(fb->stats.tris_drawn == 0 && fb->stats.tris_culled == 1,
          "back-facing triangle culled cleanly");

    /* RF_MIRROR_Y flips winding so mirrored back-order draws cleanly */
    raster_tri_ex(fb, a, c, b, NULL, 0, 0, BLEND_OPAQUE, RF_MIRROR_Y, 0, 0, 0, 0);
    CHECK(fb->stats.tris_drawn == 1, "RF_MIRROR_Y reverses winding for planar reflections");

    fb_clear_depth(fb);
    fb->stats = (RenderStats){ 0 };
    raster_tri_ex(fb, a, b, c, NULL, 0, 0, BLEND_OPAQUE, RF_NONE, 0, 0, 0, 0);
    CHECK(fb->stats.tris_drawn == 1, "front-facing triangle rasterized");
    CHECK(fabsf(fb_depth_at(fb, 18, 18) - 5.0f) < 0.05f, "1/z depth stored accurately");

    uint32_t base_px = fb->px[18 * FB_W + 18];
    SVertEx sa = { 10, 10, 4.9f, 0, 0, 0.5f, 0.5f, 0.5f };
    SVertEx sb = { 50, 10, 4.9f, 1, 0, 0.5f, 0.5f, 0.5f };
    SVertEx sc_v = { 10, 50, 4.9f, 0, 1, 0.5f, 0.5f, 0.5f };
    raster_tri_ex(fb, sa, sb, sc_v, NULL, 0, 0, BLEND_SUB, RF_NONE, 0, 0, 0, 0);
    CHECK(fb->px[18 * FB_W + 18] < base_px, "PS1 BLEND_SUB darkens underlying surface");
    CHECK(fabsf(fb_depth_at(fb, 18, 18) - 5.0f) < 0.05f,
          "semi-transparent blend preserves opaque z-buffer");

    hud_draw_visor(fb, 0.35f, 65.4f, 12, 0.5f, 0.25f, 0.4f, true);
    fb_apply_visor(fb, 1.0f);
    CHECK(((fb->px[18 * FB_W + 18] >> 16) & 0xFF) >= ((fb->px[18 * FB_W + 18]) & 0xFF),
          "crimson SIGINT visor biases red phosphor above blue");

    fb_free(fb);
}

static void test_physics_camera_and_ai(void) {
    AABB world[3] = {
        { v3(-2, 0, 4), v3(2, 4, 8) },         /* ground warehouse wall */
        { v3(-2, 1.4f, -4), v3(2, 3.0f, -1) }, /* low overhang (1.4m clearance) */
        { v3(4.0f, 0.0f, 0.0f), v3(5.2f, 1.1f, 1.2f) } /* waist-high crate */
    };
    CHECK(!los_clear(v3(0, 1.5f, 0), v3(0, 1.5f, 10), world, 3),
          "3D LOS occluded by 4m warehouse wall");
    CHECK(los_clear(v3(0, 5.2f, 0), v3(0, 5.2f, 10), world, 3),
          "3D LOS clear over top of 4m warehouse wall");

    /* Raycast tests */
    float thit = ray_hit_aabb(v3(0, 2.0f, 0), v3(0, 0, 1), 20.0f, &world[0]);
    CHECK(fabsf(thit - 4.0f) < 0.02f, "ray_hit_aabb returns exact slab entry distance");
    float shit = ray_hit_sphere(v3(0, 1.6f, 0), v3(0, 0, 1), 20.0f, v3(0, 1.6f, 5.0f), 0.5f);
    CHECK(fabsf(shit - 4.5f) < 0.02f, "ray_hit_sphere returns exact sphere entry distance");

    /* Raycast-anything grapple attaches directly to solid AABB wall */
    Body gb = { v3(0, 0, 0), v3(0, 0, 0), 0.35f, 1.8f, true, false, v3(0,0,0), 0 };
    CHECK(grapple_fire_ray(&gb, v3(0, 1.6f, 0), v3(0, 0.25f, 0.9682f),
                           world, 3, NULL, 0, 25.0f) && gb.grappling,
          "grapple_fire_ray latches onto arbitrary solid AABB architecture");
    CHECK(fabsf(gb.grapple_anchor.z - 4.0f) < 0.05f,
          "grapple_fire_ray sets anchor on AABB hit face");

    /* Ledge auto-mantle smoothly vaults airborne operator onto waist-high crate */
    Body mb = { v3(3.62f, 0.25f, 0.6f), v3(2.4f, 0.1f, 0.0f), 0.35f, 1.8f, false, false, v3(0,0,0), 0 };
    bool mantled = false;
    for (int step = 0; step < 24; step++) {
        if (physics_mantle_step(&mb, v3(1, 0, 0), world, 3, 1.0f / 60.0f))
            mantled = true;
        mb.vel.x = 2.4f;
        physics_step(&mb, world, 3, 1.0f / 60.0f);
    }
    CHECK(mantled && mb.pos.y >= 1.09f,
          "physics_mantle_step smoothly vaults airborne operator onto waist-high ledge");

    /* Wall-run sustains lateral run along vertical warehouse wall */
    Body wb = { v3(0.0f, 1.2f, 3.60f), v3(5.2f, -1.0f, 0.5f), 0.35f, 1.8f, false, false, v3(0,0,0), 0 };
    V3 wn = v3(0, 0, 0);
    float wt = 1.35f;
    CHECK(physics_wallrun_step(&wb, &wn, &wt, world, 3, 1.0f / 60.0f) && fabsf(wn.z) > 0.8f,
          "physics_wallrun_step detects adjacent vertical wall and damps fall");

    /* Crouch headroom check under 1.4m overhang */
    Body b = { v3(0, 0, -2.5f), v3(0, 0, 0), 0.35f, 1.2f, true, false, v3(0, 0, 0), 0 };
    CHECK(!physics_can_stand(&b, world, 3, 1.8f),
          "physics_can_stand blocks standing up inside 1.4m tunnel");
    b.pos = v3(0, 0, 0);
    CHECK(physics_can_stand(&b, world, 3, 1.8f),
          "physics_can_stand allows standing in open street");

    /* Third-person camera occludes cleanly against walls and tightens on ADS */
    Camera c_wall = tps_camera_step((Camera){ b.pos, 0.0f, 0.0f }, &b, 0.0f, 0.0f,
                                    false, false, world, 3, 1.0f);
    CHECK(c_wall.pos.z > -1.0f,
          "tps_camera_step pulls camera forward of solid wall behind operator");
    b.pos = v3(0, 0, 2.2f);
    Camera c_hip = tps_camera_step((Camera){ b.pos, 0.0f, 0.0f }, &b, 0.0f, 0.0f,
                                   false, false, world, 3, 1.0f);
    Camera c_ads = tps_camera_step((Camera){ b.pos, 0.0f, 0.0f }, &b, 0.0f, 0.0f,
                                   false, true, world, 3, 1.0f);
    CHECK(v3_len(v3_sub(c_ads.pos, b.pos)) < v3_len(v3_sub(c_hip.pos, b.pos)),
          "tps_camera_step pulls tighter over right shoulder during ADS");

    /* Guard acoustic ping + Mecha subsystem damage */
    Guard g = { v3(0, 0, 12.0f), 0.0f, 0.0f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f };
    guard_hear_ping(&g, v3(3.0f, 0.0f, 14.0f), 15.0f);
    CHECK(g.inv_timer > 1.0f && g.alert >= 0.3f,
          "guard_hear_ping triggers acoustic investigation state");

    Mecha mc;
    mecha_init(&mc, v3(8.0f, 0.0f, 10.0f), 0.0f);
    V3 mwp[2] = { v3(8.0f, 0.0f, 10.0f), v3(8.0f, 0.0f, 16.0f) };
    mecha_update(&mc, mwp, 2, v3(8.0f, 0.0f, 13.0f), 0.8f, 0.9f, world, 3, 0.1f);
    CHECK(mc.alert > 0.0f, "mecha_update elevates alert on lit noisy target in LOS");
}

static void test_audio_and_greyport_hitscan(void) {
    Mixer m;
    mixer_init(&m, 2);
    au_pistol_shot(&m, 0.05f);
    au_glass_break(&m, 0.25f);
    au_visor_toggle(&m, 0.45f);
    au_terminal_hack(&m, 0.65f);
    au_mecha_step(&m, 0.95f, 1.0f);
    CHECK(m.len > AU_RATE, "offline espionage synth cues populate PCM buffer");
    mixer_free(&m);

    AudioLive live;
    au_live_init(&live);
    au_live_pistol(&live);
    au_live_glass(&live);
    au_live_visor(&live);
    au_live_hack(&live);
    au_live_mecha_step(&live, 0.9f);
    float pcm[512];
    au_live_render(&live, pcm, 256);
    float peak = 0.0f;
    for (int i = 0; i < 512; i++) if (fabsf(pcm[i]) > peak) peak = fabsf(pcm[i]);
    CHECK(peak > 0.02f && peak <= 0.96f, "au_live_render mixes active synth voices within headroom");

    GreyportScene sc;
    greyport_init(&sc);
    Guard g = { v3(2.0f, 0.0f, 14.0f), 3.14f, 0.0f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f };
    Mecha mc;
    mecha_init(&mc, v3(8.0f, 0.0f, 14.0f), 3.14f);
    V3 hitp = v3(0,0,0);
    int hr = greyport_fire_ray_ex(&sc, v3(2.0f, 1.60f, 10.0f), v3(0, 0, 1), &g, 1, &mc, &hitp);
    CHECK(hr == GP_HIT_GUARD_HEAD && g.stunned > 10.0f,
          "greyport_fire_ray_ex registers headshot takedown on trooper");

    int mr = greyport_fire_ray_ex(&sc, v3(8.0f, 3.05f, 10.0f), v3(0, 0, 1), &g, 1, &mc, &hitp);
    CHECK(mr == GP_HIT_MECHA_SENSOR && mc.sensor_hp < 1.0f,
          "greyport_fire_ray_ex damages Patrol Mecha crimson sensor slit");
    greyport_free(&sc);
}

static void test_persistence_and_assets(void) {
    const char *spath = "/tmp/casm_ps15_test_save.dat";
    SaveState s = { 0, 0, v3(-12.75f, 5.0f, 33.25f), 0.82f, 900, 0 };
    CHECK(save_write(spath, &s) == 0, "CRC32 save_write succeeds with negative coords");
    SaveState out;
    CHECK(save_read(spath, &out) == 0 &&
          fabsf(out.player_pos.x - s.player_pos.x) < 1e-5f &&
          fabsf(out.player_pos.y - s.player_pos.y) < 1e-5f &&
          fabsf(out.alert - s.alert) < 1e-5f,
          "CRC32 save_read round-trips negative & elevated coordinates bit-exactly");

    FILE *f = fopen(spath, "r+b");
    if (f) {
        fseek(f, 11, SEEK_SET);
        uint8_t byte = 0;
        if (fread(&byte, 1, 1, f) == 1) {
            byte ^= 0x04u;
            fseek(f, 11, SEEK_SET);
            fwrite(&byte, 1, 1, f);
        }
        fclose(f);
    }
    CHECK(save_read(spath, &out) == -1, "CRC32 detects single-bit save corruption");
    remove(spath);

    const char *opath = "/tmp/casm_ps15_test.obj";
    FILE *of = fopen(opath, "w");
    fprintf(of,
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0.5 1.5 0\nv 0 1 0\n"
        "vn 0 0 1\n"
        "f -5//-1 -4//-1 -3//-1 -2//-1 -1//-1\n");
    fclose(of);
    Mesh om = mesh_load_obj(opath);
    CHECK(om.nt == 3 && om.nv == 9 && om.radius > 0.5f,
          "OBJ loader parses v//vn, negative indices, and 5-gon fan");
    mesh_free(&om);
    remove(opath);
}

int main(void) {
    test_asm_fill32();
    test_asm_upscale2x();
    test_asm_span_tex64();
    test_asm_span_abr64();
    test_raster_and_blends();
    test_physics_camera_and_ai();
    test_audio_and_greyport_hitscan();
    test_persistence_and_assets();

    /* PS1.5 Visual Enhancements: ParticlePool, fov_scale, specular & rim flags */
    ParticlePool pool;
    particles_init(&pool, 0x12345678ULL);
    int active0 = 0;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) if (pool.items[i].life > 0.0f) active0++;
    CHECK(active0 == 0, "particles_init clears active pool");
    particles_spawn_burst(&pool, v3(0, 1, 0), v3(1, 0, 0), 12, 1.5f, 4.0f, 0.40f, 0xffd258u, true);
    int active1 = 0;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) if (pool.items[i].life > 0.0f) active1++;
    CHECK(active1 == 12, "particles_spawn_burst creates active 3D sparks");
    particles_update(&pool, 0.20f);
    int active2 = 0;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) if (pool.items[i].life > 0.0f) active2++;
    CHECK(active2 == 12 && pool.items[0].life < 0.30f, "particles_update ages active sparks");
    particles_update(&pool, 0.30f);
    int active3 = 0;
    for (int i = 0; i < PS15_MAX_PARTICLES; i++) if (pool.items[i].life > 0.0f) active3++;
    CHECK(active3 == 0, "particles_update reclaims expired sparks cleanly");

    FB *fb_fov = fb_new(FB_W, FB_H);
    fb_fov->settings.fov_scale = 1.0f;
    int sx1, sy1, sx2, sy2; float sz1, sz2;
    Camera c0 = { v3(0,0,0), 0, 0 };
    render_project(fb_fov, &c0, v3(1.0f, 0.0f, 5.0f), &sx1, &sy1, &sz1);
    fb_fov->settings.fov_scale = 1.32f;
    render_project(fb_fov, &c0, v3(1.0f, 0.0f, 5.0f), &sx2, &sy2, &sz2);
    CHECK(sx2 > sx1, "fov_scale > 1.0 optical portrait lens magnifies projection correctly");
    fb_free(fb_fov);

    printf("CASM-PS15 test suite: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
