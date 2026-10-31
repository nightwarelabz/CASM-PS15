/* CASM-PS15 demo host: a Greyport street at night.
 * The operator walks a route, a guard patrols, perception runs, the
 * grapple fires at the watchtower, and every engine system reports in
 * the ledger. Headless: renders frames, writes frame.ppm (960x540
 * upscale), audio.wav (the synth mix) and save.dat (persistence). */
#define _POSIX_C_SOURCE 199309L
#include "engine.h"
#include <time.h>

static const AABB WORLD[] = {
    { { -9, 0, 22 },    { 1, 5, 30 } },      /* warehouse 1 */
    { { 4, 0, 27.5f },  { 16, 7, 36.5f } },  /* warehouse 2 */
    { { -2.6f, 0, 33.4f }, { -1.4f, 11, 34.6f } }, /* watchtower */
    { { 1.4f, 0, 17.4f },  { 2.6f, 1.2f, 18.6f } }, /* crate */
};
#define NWORLD (int)(sizeof WORLD / sizeof WORLD[0])

static const V3 GUARD_WP[] = {
    { 8, 0, 24 }, { -6, 0, 28 }, { -6, 0, 16 }, { 8, 0, 16 }
};
static const PointLight LAMPS[] = {
    { { -2, 11.4f, 34 }, 16, 1.0f, 0.58f, 0.16f },   /* tower sodium */
    { { 6, 4.4f, 18 },   13, 1.0f, 0.58f, 0.16f },   /* street sodium */
    { { -8, 4.4f, 28 },  13, 1.0f, 0.55f, 0.15f },   /* street sodium */
};

static int write_ppm(const char *path, const FB *fb) {
    uint32_t *up = fb_upscale2x(fb);
    FILE *f = fopen(path, "wb");
    if (!f) { free(up); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", fb->w * 2, fb->h * 2);
    for (int i = 0; i < fb->w * 2 * fb->h * 2; i++) {
        unsigned char px[3] = { (unsigned char)(up[i] >> 16),
                                (unsigned char)(up[i] >> 8),
                                (unsigned char)up[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    free(up);
    return 0;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void) {
    Tex tex[TEX_COUNT];
    texgen_all(tex);
    FB *fb = fb_new(FB_W, FB_H);
    Humanoid oper = humanoid_new();
    Humanoid soldier = humanoid_new();
    Mesh ground = mesh_plane(90, 90, 14);
    Mesh ware1 = mesh_box(10, 5, 8);
    Mesh ware2 = mesh_box(12, 7, 9);
    Mesh tower = mesh_box(1.2f, 11, 1.2f);
    Mesh platform = mesh_box(3, 0.5f, 3);
    Mesh lamp_head = mesh_box(0.5f, 0.3f, 0.5f);
    Mesh pole = mesh_box(0.18f, 4.4f, 0.18f);
    Mesh crate = mesh_box(1.2f, 1.2f, 1.2f);
    LightRig rig = { LAMPS, 3, 0.16f, 0.19f, 0.28f };

    Body player = { v3(-6, 0, 9), v3(0, 0, 0), 0.35f, 1.8f, false, false, v3(0, 0, 0), 0 };
    Guard guard = { v3(8, 0, 24), 3.14f, 0, 0, 0 };
    float player_yaw = 0.4f;
    const V3 route[] = { v3(-1, 0, 15), v3(5, 0, 21), v3(2, 0, 27), v3(0, 0, 11) };
    int route_i = 0;
    enum { WALK, GRAPPLE, DONE } phase = WALK;
    int grapple_frames = 0;
    bool grapple_fired = false, alarmed = false;
    float alert_peak = 0, noise = 0, step_acc = 0, prev_speed = 0;
    Mixer mix;
    mixer_init(&mix, 16);
    au_hum(&mix, 16.0f);

    const int FRAMES = 900;
    const float DT = 1.0f / 60.0f;
    double render_ms = 0;

    for (int frame = 0; frame < FRAMES; frame++) {
        float t = frame * DT;
        /* ---- scripted operator control ---- */
        float speed = 0;
        if (phase == WALK) {
            V3 tgt = route[route_i];
            V3 to = v3_sub(tgt, player.pos); to.y = 0;
            float d = v3_len(to);
            if (d < 0.8f) {
                route_i++;
                if (route_i == 3 && !grapple_fired) {
                    grapple_fire(&player, v3(-2, 11.2f, 34));
                    au_grapple(&mix, t);
                    grapple_fired = true;
                    phase = GRAPPLE;
                    noise = 0.9f;
                } else if (route_i >= 4) {
                    phase = DONE;
                }
            } else {
                V3 dir = v3_norm(to);
                player_yaw = atan2f(dir.x, dir.z);
                player.vel.x = dir.x * 3.1f;
                player.vel.z = dir.z * 3.1f;
                speed = 3.1f;
                noise = 0.35f;
            }
        } else if (phase == GRAPPLE) {
            player.vel.x *= 0.98f; player.vel.z *= 0.98f;
            if (++grapple_frames > 42) {
                grapple_release(&player);
                player.vel = v3(-2.5f, 2.0f, -4.0f);
                phase = WALK;
            }
        } else {
            player.vel.x = 0; player.vel.z = 0;
            noise = 0;
            player_yaw = 0.15f;   /* face the street for the final frame */
        }
        if (!player.grounded) noise = fmaxf(noise, 0.2f);

        V3 prev_pos = player.pos;
        physics_step(&player, WORLD, NWORLD, DT);
        float moved = v3_len(v3_sub(player.pos, prev_pos));
        if (player.grounded && speed > 0) {
            step_acc += moved;
            if (step_acc > 1.85f) {
                step_acc = 0;
                float g, p;
                au_positional(2.0f, &g, &p);
                au_footstep(&mix, t, true);
            }
        }
        prev_speed = speed;

        /* ---- guard ---- */
        guard_update(&guard, GUARD_WP, 4, player.pos, noise, WORLD, NWORLD, DT);
        if (guard.alert > alert_peak) alert_peak = guard.alert;
        if (guard.alert > 0.7f && !alarmed) {
            alarmed = true;
            au_tone(&mix, t, 0.35f, 660, 880, 0.15f);   /* alarm sting */
        }

        /* ---- render ---- */
        double r0 = now_ms();
        Camera cam;
        V3 facing = v3(sinf(player_yaw), 0, cosf(player_yaw));
        cam.pos = v3_add(v3_sub(player.pos, v3_scale(facing, 6.5f)), v3(0, 3.1f, 0));
        cam.yaw = player_yaw;
        cam.pitch = -0.20f;
        fb_sky(fb);
        fb_clear_depth(fb);
        render_mesh(fb, &cam, &ground, v3(0, 0, 35), 0, &tex[TEX_CONCRETE], 1, 1, 1, &rig);
        render_mesh(fb, &cam, &ware1, v3(-4, 0, 22), 0, &tex[TEX_BRICK], 1, 1, 1, &rig);
        render_mesh(fb, &cam, &ware2, v3(10, 0, 27.5f), 0, &tex[TEX_METAL], 0.9f, 0.92f, 1, &rig);
        render_mesh(fb, &cam, &tower, v3(-2, 0, 33.4f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
        render_mesh(fb, &cam, &platform, v3(-2, 11, 32.5f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
        render_mesh(fb, &cam, &pole, v3(6, 0, 18), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
        render_mesh(fb, &cam, &pole, v3(-8, 0, 28), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
        render_mesh(fb, &cam, &lamp_head, v3(6, 4.4f, 18), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
        render_mesh(fb, &cam, &lamp_head, v3(-8, 4.4f, 28), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
        render_mesh(fb, &cam, &crate, v3(2, 0, 17.4f), 0.4f, &tex[TEX_METAL], 0.85f, 0.85f, 0.9f, &rig);

        Pose op = (speed > 0.1f)
            ? anim_blend(anim_sample(&CLIP_IDLE, t), anim_sample(&CLIP_WALK, t * 1.7f),
                         clampf(speed / 3.0f, 0, 1))
            : anim_sample(&CLIP_IDLE, t);
        if (player.grappling) { op.arm_r = -2.4f; op.arm_l = -2.2f; }
        render_humanoid(fb, &cam, &oper, player.pos, player_yaw, &op,
                        &tex[TEX_CAMO], 0.85f, 0.9f, 0.85f, &rig);
        Pose gp = anim_sample(&CLIP_WALK, t * 1.4f);
        if (guard.wait > 0) gp = anim_sample(&CLIP_IDLE, t);
        render_humanoid(fb, &cam, &soldier, guard.pos, guard.yaw, &gp,
                        &tex[TEX_CAMO], 1.05f, 0.95f, 0.85f, &rig);

        /* lamp glows */
        for (int i = 0; i < 3; i++) {
            V3 d = v3_sub(LAMPS[i].pos, cam.pos);
            float vx = cosf(cam.yaw) * d.x - sinf(cam.yaw) * d.z;
            float vz0 = sinf(cam.yaw) * d.x + cosf(cam.yaw) * d.z;
            float py = cosf(cam.pitch) * d.y - sinf(cam.pitch) * vz0;
            float vz = sinf(cam.pitch) * d.y + cosf(cam.pitch) * vz0;
            if (vz < 1) continue;
            int gx = (int)(FB_W * 0.5f + vx * 215.0f / vz);
            int gy = (int)(FB_H * 0.5f - py * 215.0f / vz);
            if (gx < 0 || gx >= FB_W || gy < 0 || gy >= FB_H) continue;
            if (fb->zb[gy * FB_W + gx] < vz - 1.5f) continue; /* occluded */
            raster_glow(fb, gx, gy, (int)(260.0f / vz), 120, 66, 10);
        }
        /* grapple line */
        if (player.grappling) {
            raster_rect(fb, FB_W / 2, FB_H / 3, 1, FB_H / 3, 0x888888);
        }
        /* HUD: alert bar + frame counter */
        raster_bar(fb, 8, 8, 90, 5, guard.alert, 0xd84040);
        raster_number(fb, FB_W - 40, 8, frame, 0xcccccc);
        raster_number(fb, 8, 18, (int)(alert_peak * 100), 0xd84040);
        render_ms += now_ms() - r0;
        (void)prev_speed;
    }

    /* ---- persistence round-trip ---- */
    SaveState sv = { 0, 0, player.pos, guard.alert, FRAMES, 0 };
    int save_ok = (save_write("save.dat", &sv) == 0);
    SaveState back;
    int load_ok = save_ok && (save_read("save.dat", &back) == 0) &&
                  back.frames == FRAMES &&
                  fabsf(back.player_pos.x - player.pos.x) < 0.001f;

    if (write_ppm("frame.ppm", fb) != 0) { fprintf(stderr, "frame.ppm failed\n"); return 1; }
    if (au_write_wav("audio.wav", &mix) != 0) { fprintf(stderr, "audio.wav failed\n"); return 1; }

    int tris = ground.nt + ware1.nt + ware2.nt + tower.nt + platform.nt +
               pole.nt * 2 + lamp_head.nt * 2 + crate.nt +
               (oper.parts[0].nt + oper.parts[1].nt + oper.parts[2].nt +
                oper.parts[3].nt + oper.parts[4].nt + oper.parts[5].nt +
                oper.parts[6].nt) * 2;
    printf("CASM-PS15 engine v1 — Greyport demo ledger\n");
    printf("renderer:   %dx%d software, textured, RGB555+dither, %d tris/frame\n",
           FB_W, FB_H, tris);
    printf("frames:     %d simulated+rendered, mean render %.2f ms/frame\n",
           FRAMES, render_ms / FRAMES);
    printf("operator:   route done=%s grapple fired=%s final pos (%.1f, %.1f, %.1f)\n",
           phase == DONE ? "yes" : "no", grapple_fired ? "yes" : "no",
           player.pos.x, player.pos.y, player.pos.z);
    printf("perception: guard alert peak %.2f, alarm raised=%s\n",
           alert_peak, alarmed ? "yes" : "no");
    printf("audio:      %d samples synth mix -> audio.wav (hum, footsteps, grapple%s)\n",
           mix.len, alarmed ? ", alarm" : "");
    printf("persistence: save=%s load-verify=%s\n",
           save_ok ? "ok" : "FAIL", load_ok ? "ok" : "FAIL");
    printf("outputs:    frame.ppm (960x540), audio.wav, save.dat\n");

    fb_free(fb);
    humanoid_free(&oper);
    humanoid_free(&soldier);
    mesh_free(&ground); mesh_free(&ware1); mesh_free(&ware2);
    mesh_free(&tower); mesh_free(&platform); mesh_free(&lamp_head);
    mesh_free(&pole); mesh_free(&crate);
    texgen_free(tex);
    mixer_free(&mix);
    return (save_ok && load_ok && grapple_fired) ? 0 : 2;
}
