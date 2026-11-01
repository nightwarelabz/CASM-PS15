/* CASM-PS15 demo host: Greyport Harbor & Dockyard Street at Night.
 * The NSA espionage operator infiltrates the wet sodium-lit pier, Regime
 * troopers and an autonomous Type-1 Patrol Mecha sweep the avenue, real-time
 * planar water reflections shimmer in the flooded canal, the 3-prong grapple
 * launches up toward the watchtower, and the finale composes the iconic
 * DeBrief 3/4 hero portrait.
 *
 * Headless: simulates & renders 900 deterministic frames, writes frame.ppm
 * (960x540 2x upscale), audio.wav (synth mix) and save.dat (CRC32 state). */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "greyport.h"
#include <time.h>

static const V3 GUARD0_WP[] = {
    { 7.5f, 0.0f, 21.5f },
    { 2.6f, 0.0f, 25.0f },
    { 2.6f, 0.0f, 14.5f },
    { 7.5f, 0.0f, 15.0f },
};
static const V3 GUARD1_WP[] = {
    { -2.2f, 0.0f, 29.5f },
    {  1.8f, 0.0f, 30.5f },
    {  1.8f, 0.0f, 26.8f },
    { -2.2f, 0.0f, 27.2f },
};
static const V3 MECHA_WP[] = {
    { 9.2f, 0.0f, 12.5f },
    { 9.2f, 0.0f, 21.2f },
};

static int write_ppm(const char *path, const FB *fb) {
    uint32_t *up = fb_upscale2x(fb);
    FILE *f = fopen(path, "wb");
    if (!f) { free(up); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", fb->w * 2, fb->h * 2);
    size_t npix = (size_t)(fb->w * 2) * (size_t)(fb->h * 2);
    unsigned char *rgb = malloc(npix * 3);
    for (size_t i = 0; i < npix; i++) {
        rgb[i * 3 + 0] = (unsigned char)(up[i] >> 16);
        rgb[i * 3 + 1] = (unsigned char)(up[i] >> 8);
        rgb[i * 3 + 2] = (unsigned char)(up[i]);
    }
    size_t wr = fwrite(rgb, 1, npix * 3, f);
    free(rgb);
    fclose(f);
    free(up);
    return (wr == npix * 3) ? 0 : -1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void) {
    GreyportScene sc;
    greyport_init(&sc);

    FB *fb = fb_new(FB_W, FB_H);
    Humanoid oper    = humanoid_new();
    Humanoid soldier = humanoid_guard_new();
    MechaMesh mmesh  = mecha_mesh_new();

    Body player = { v3(-4.2f, 0.0f, 9.2f), v3(0, 0, 0), 0.35f, 1.8f, false, false, v3(0, 0, 0), 0 };
    Guard guards[2] = {
        { v3( 7.5f, 0.0f, 21.5f), 3.14f, 0.0f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f },
        { v3(-2.2f, 0.0f, 29.5f), 1.57f, 0.0f, 0, 0.4f, 0.0f, v3(0,0,0), 0.0f }
    };
    Mecha walker;
    mecha_init(&walker, v3(9.2f, 0.0f, 13.0f), 0.0f);
    float player_yaw = 0.4f;

    const V3 route[] = {
        v3( 0.4f, 0.0f, 13.5f),
        v3( 3.4f, 0.0f, 19.5f),
        v3( 2.6f, 0.0f, 26.0f),
        v3( 3.5f, 0.0f, 15.6f),
        v3(-1.45f, 0.0f, 9.2f)
    };
    int route_i = 0;
    enum { WALK, GRAPPLE, DONE } phase = WALK;
    int grapple_frames = 0;
    bool grapple_fired = false, alarmed = false;
    float alert_peak = 0.0f, noise = 0.0f, step_acc = 0.0f;
    float done_blend = 0.0f;

    Mixer mix;
    mixer_init(&mix, 16);
    au_hum(&mix, 16.0f);

    const int FRAMES = 900;
    const float DT = 1.0f / 60.0f;
    double render_ms = 0.0;
    long total_tris_in = 0, total_tris_drawn = 0;

    for (int frame = 0; frame < FRAMES; frame++) {
        float t = frame * DT;
        float speed = 0.0f;

        if (phase == WALK) {
            V3 tgt = route[route_i];
            V3 to = v3_sub(tgt, player.pos);
            to.y = 0.0f;
            float d = v3_len(to);
            if (d < 0.85f) {
                route_i++;
                if (route_i == 3 && !grapple_fired) {
                    grapple_fire(&player, sc.anchors[0]);
                    au_grapple(&mix, t);
                    grapple_fired = true;
                    phase = GRAPPLE;
                    noise = 0.92f;
                } else if (route_i >= 5) {
                    phase = DONE;
                }
            } else {
                V3 dir = v3_norm(to);
                player_yaw = atan2f(dir.x, dir.z);
                player.vel.x = dir.x * 4.2f;
                player.vel.z = dir.z * 4.2f;
                speed = 4.2f;
                noise = 0.38f;
            }
        } else if (phase == GRAPPLE) {
            player.vel.x *= 0.985f;
            player.vel.z *= 0.985f;
            if (++grapple_frames > 36) {
                grapple_release(&player);
                player.vel = v3(1.2f, 1.4f, -4.6f);
                phase = WALK;
            }
        } else {
            player.vel.x = 0.0f;
            player.vel.z = 0.0f;
            noise = 0.0f;
            /* 3/4 banner stance showing NSA crest, ruby visor, twin whips & USP bore */
            player_yaw = 2.76f;
            done_blend = clampf(done_blend + DT * 2.2f, 0.0f, 1.0f);
        }
        if (!player.grounded) noise = fmaxf(noise, 0.22f);

        V3 prev_pos = player.pos;
        physics_step(&player, sc.world, sc.nworld, DT);
        float moved = v3_len(v3_sub(player.pos, prev_pos));

        float vis = light_exposure_at(&sc.rig, v3(player.pos.x, player.pos.y + 1.0f, player.pos.z));
        guard_update_ex(&guards[0], GUARD0_WP, 4, player.pos, noise, vis,
                        sc.world, sc.nworld, DT);
        guard_update_ex(&guards[1], GUARD1_WP, 4, player.pos, noise, vis,
                        sc.world, sc.nworld, DT);
        mecha_update(&walker, MECHA_WP, 2, player.pos, noise, vis,
                     sc.world, sc.nworld, DT);

        float max_al = fmaxf(fmaxf(guards[0].alert, guards[1].alert), walker.alert);
        if (max_al > alert_peak) alert_peak = max_al;
        if (max_al > 0.7f && !alarmed) {
            alarmed = true;
            au_tone(&mix, t, 0.35f, 660.0f, 880.0f, 0.15f);
            au_tone(&mix, t + 0.38f, 0.35f, 660.0f, 880.0f, 0.13f);
        }

        /* Third-person over-shoulder camera -> DeBrief 3/4 hero portrait in finale */
        Camera follow = tps_camera_step((Camera){ player.pos, player_yaw, -0.10f },
                                        &player, player_yaw, -0.10f,
                                        false, false, sc.world, sc.nworld, 1.0f);
        Camera cam = follow;
        fb->settings.fov_scale = 1.0f;
        if (done_blend > 0.001f) {
            V3 hero_pos = v3(-1.43f, 1.33f, 7.68f);
            cam.pos   = v3(lerpf(follow.pos.x, hero_pos.x, done_blend),
                           lerpf(follow.pos.y, hero_pos.y, done_blend),
                           lerpf(follow.pos.z, hero_pos.z, done_blend));
            cam.yaw   = lerpf(follow.yaw, -0.30f, done_blend);
            cam.pitch = lerpf(follow.pitch, 0.02f, done_blend);
            fb->settings.fov_scale = lerpf(1.0f, 1.32f, done_blend);
        }

        if (walker.step_event) {
            float mg, mpan;
            au_positional(v3_len(v3_sub(walker.pos, cam.pos)), &mg, &mpan);
            au_mecha_step(&mix, t, mg * 1.15f);
        }
        if (player.grounded && speed > 0.0f) {
            step_acc += moved;
            if (step_acc > 1.80f) {
                step_acc = 0.0f;
                float g, pan;
                au_positional(v3_len(v3_sub(player.pos, cam.pos)), &g, &pan);
                au_footstep_ex(&mix, t, true, g * 1.4f);
            }
        }

        /* ---- PS1.5 software render ---- */
        double r0 = now_ms();
        greyport_update_lights(&sc, guards, 2, t);
        if (done_blend > 0.5f && sc.rig.nlights < GREYPORT_MAX_LIGHTS) {
            int li = sc.rig.nlights++;
            sc.lights[li] = (PointLight){
                v3(-2.75f, 2.15f, 8.15f),
                6.5f, 1.05f * done_blend, 0.85f * done_blend, 0.58f * done_blend
            };
        }

        greyport_render_opaque(fb, &sc, &cam, t);

        /* Grounded contact shadows */
        float op_gy = world_ground_y(sc.world, sc.nworld, v3(player.pos.x, player.pos.y + 0.2f, player.pos.z));
        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                           v3(player.pos.x, op_gy, player.pos.z), 0.58f);
        for (int i = 0; i < 2; i++)
            render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                               guards[i].pos, 0.58f);
        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                           walker.pos, 1.25f);

        /* Operator pose & 3-prong grapple cable */
        Pose op;
        if (phase == DONE) {
            op = anim_blend(anim_sample(&CLIP_IDLE, t), anim_sample(&CLIP_AIM, t), done_blend);
        } else if (speed > 0.1f) {
            op = anim_blend(anim_sample(&CLIP_IDLE, t),
                            anim_sample(&CLIP_WALK, t * 1.65f),
                            clampf(speed / 3.8f, 0.0f, 1.0f));
        } else {
            op = anim_sample(&CLIP_IDLE, t);
        }
        if (player.grappling) {
            op.arm_r = -2.35f; op.arm_l = -2.05f;
            op.elbow_r = 0.20f; op.elbow_l = 0.35f;
        }
        render_humanoid(fb, &cam, &oper, player.pos, player_yaw, &op,
                        sc.tex, 1.0f, 1.0f, 1.02f, &sc.rig);

        if (player.grappling) {
            V3 hand_w = v3(player.pos.x + sinf(player_yaw) * 0.25f,
                           player.pos.y + 1.55f,
                           player.pos.z + cosf(player_yaw) * 0.25f);
            greyport_render_grapple(fb, &sc, &cam, hand_w, player.grapple_anchor);
        }

        /* Regime troopers + autonomous Type-1 Patrol Mecha */
        for (int i = 0; i < 2; i++) {
            Pose gp = (guards[i].wait > 0.0f) ? anim_sample(&CLIP_IDLE, t)
                                              : anim_sample(&CLIP_WALK, t * 1.35f);
            gp.arm_r = -0.72f; gp.elbow_r = 0.65f;
            render_humanoid(fb, &cam, &soldier, guards[i].pos, guards[i].yaw, &gp,
                            sc.tex, 1.0f, 1.0f, 1.0f, &sc.rig);
        }
        render_mecha(fb, &cam, &mmesh, &walker, sc.tex, &sc.rig);

        /* Translucent PS1 ABR pass: canal water over reflections, cones, halos & rain */
        greyport_render_fx(fb, &sc, &cam, guards, 2, t);

        /* Clean finale portrait frame vs active diegetic amber visor HUD */
        if (phase != DONE) {
            hud_draw_visor(fb, cam.yaw, t, 12, vis, max_al, noise, player.grappling);
        } else {
            raster_text(fb, 8, 7, "DEBRIEF // GREYPORT HARBOR", 0xffbe3cu);
            raster_text(fb, FB_W - 64, 7, "CASM-P15", 0x94a8c8u);
        }

        render_ms += now_ms() - r0;
        total_tris_in += fb->stats.tris_submitted;
        total_tris_drawn += fb->stats.tris_drawn;
    }

    /* ---- CRC32 persistence round-trip ---- */
    SaveState sv = { 0, 0, player.pos, alert_peak, FRAMES, 0 };
    int save_ok = (save_write("save.dat", &sv) == 0);
    SaveState back;
    int load_ok = save_ok && (save_read("save.dat", &back) == 0) &&
                  back.frames == (uint32_t)FRAMES &&
                  fabsf(back.player_pos.x - player.pos.x) < 0.001f &&
                  fabsf(back.player_pos.z - player.pos.z) < 0.001f;

    if (write_ppm("frame.ppm", fb) != 0) { fprintf(stderr, "frame.ppm failed\n"); return 1; }
    if (au_write_wav("audio.wav", &mix) != 0) { fprintf(stderr, "audio.wav failed\n"); return 1; }

    printf("CASM-PS15 engine — Greyport PS1.5 demo ledger\n");
    printf("renderer:    %dx%d software (2x -> 960x540), 8px persp spans, RGB555+Bayer, ABR+mirror\n",
           FB_W, FB_H);
    printf("geometry:    %ld avg submitted tris/frame, %ld avg visible tris/frame\n",
           total_tris_in / FRAMES, total_tris_drawn / FRAMES);
    printf("frames:      %d simulated+rendered, mean render %.2f ms/frame (%.0f fps)\n",
           FRAMES, render_ms / FRAMES, 1000.0 / (render_ms / FRAMES));
    printf("operator:    route done=%s grapple fired=%s final pos (%.1f, %.1f, %.1f)\n",
           phase == DONE ? "yes" : "no", grapple_fired ? "yes" : "no",
           player.pos.x, player.pos.y, player.pos.z);
    printf("perception:  one-field light + 2 troopers + Type-1 Mecha, alert peak %.2f, alarm=%s\n",
           alert_peak, alarmed ? "yes" : "no");
    printf("audio:       %d samples synth mix -> audio.wav (hum, rain, footsteps, mecha, grapple%s)\n",
           mix.len, alarmed ? ", alarm" : "");
    printf("persistence: CRC32 save=%s load-verify=%s\n",
           save_ok ? "ok" : "FAIL", load_ok ? "ok" : "FAIL");
    printf("assets:      crate.obj=%s (%d verts) brick.ppm=%s\n",
           sc.have_crate ? "loaded" : "FALLBACK", sc.crate.nv,
           sc.have_brick_file ? "loaded" : "FALLBACK");
    printf("outputs:     frame.ppm (960x540), audio.wav, save.dat\n");

    fb_free(fb);
    mecha_mesh_free(&mmesh);
    humanoid_free(&oper);
    humanoid_free(&soldier);
    greyport_free(&sc);
    mixer_free(&mix);
    return (save_ok && load_ok && phase == DONE && grapple_fired &&
            sc.have_crate && sc.have_brick_file) ? 0 : 2;
}
