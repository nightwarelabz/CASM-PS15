/* CASM-PS15 windowed host: play in the Greyport street.
 * First person by default (V toggles third person). WASD + mouse to
 * move/look, Shift sprint, Ctrl crouch, Space jump, E grapple
 * (press fires, hold keeps, release detaches), Esc quits.
 * Live synth audio runs through the platform device backend. */
#define _POSIX_C_SOURCE 199309L
#include "engine.h"
#include "platform.h"
#include <time.h>

static const AABB WORLD[] = {
    { { -9, 0, 22 },    { 1, 5, 30 } },
    { { 4, 0, 27.5f },  { 16, 7, 36.5f } },
    { { -2.6f, 0, 33.4f }, { -1.4f, 11, 34.6f } },
    { { 1.4f, 0, 17.4f },  { 2.6f, 1.2f, 18.6f } },
};
#define NWORLD (int)(sizeof WORLD / sizeof WORLD[0])
static const V3 GUARD_WP[] = {
    { 8, 0, 24 }, { -6, 0, 28 }, { -6, 0, 16 }, { 8, 0, 16 }
};
static const PointLight LAMPS[] = {
    { { -2, 11.4f, 34 }, 16, 1.0f, 0.58f, 0.16f },
    { { 6, 4.4f, 18 },   13, 1.0f, 0.58f, 0.16f },
    { { -8, 4.4f, 28 },  13, 1.0f, 0.55f, 0.15f },
};

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint32_t present_buf[960 * 540];
static void present(Plat *plat, const FB *fb) {
    for (int y = 0; y < 540; y++)
        for (int x = 0; x < 960; x++)
            present_buf[y * 960 + x] = fb->px[(y / 2) * fb->w + (x / 2)];
    plat_present(plat, present_buf);
}

/* grapple anchor search: lamp heads and tower top in the facing cone */
static int find_anchor(V3 eye, float yaw, float pitch, V3 *out) {
    V3 fwd = v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
    const V3 cand[] = { v3(-2, 11.2f, 34), v3(6, 4.6f, 18), v3(-8, 4.6f, 28),
                        v3(1, 5.2f, 30), v3(16, 7.2f, 36.5f) };
    float best = 0.75f;
    int found = 0;
    for (int i = 0; i < 5; i++) {
        V3 to = v3_sub(cand[i], eye);
        float d = v3_len(to);
        if (d < 3 || d > 28) continue;
        float dot = v3_dot(v3_norm(to), fwd);
        if (dot > best) { best = dot; *out = cand[i]; found = 1; }
    }
    return found;
}

int main(int argc, char **argv) {
    long max_frames = 0;                    /* --frames N: smoke-run limit */
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--frames") == 0) max_frames = strtol(argv[i + 1], NULL, 10);
    Tex tex[TEX_COUNT];
    texgen_all(tex);
    Tex brick_file;
    int have_brick_file = (tex_load_ppm("assets/brick.ppm", &brick_file) == 0);
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
    Mesh crate = mesh_load_obj("assets/crate.obj");
    int have_crate = crate.nv > 0;
    if (!have_crate) crate = mesh_box(1.2f, 1.2f, 1.2f);
    LightRig rig = { LAMPS, 3, 0.16f, 0.19f, 0.28f };

    Plat *plat = plat_open("CASM-PS15 — Greyport", 960, 540);
    if (!plat) { fprintf(stderr, "no window available\n"); return 1; }
    AudioLive live;
    au_live_init(&live);
    int audio_ok = (audev_start(&live) == 0);
    printf("window: X11/Cocoa platform  audio: %s\n",
           audio_ok ? audev_name() : "unavailable (silent session)");

    Body player = { v3(-6, 0, 9), v3(0, 0, 0), 0.35f, 1.8f, false, false, v3(0, 0, 0), 0 };
    Guard guard = { v3(8, 0, 24), 3.14f, 0, 0, 0 };
    float yaw = 0.4f, pitch = 0.0f;
    int third_person = 0, grapples = 0;
    float alert_peak = 0, step_acc = 0, noise = 0;
    int alarmed = 0;
    long frames = 0;
    double start = now_s(), last = start, acc = 0;
    const float DT = 1.0f / 60.0f;

    Input in;
    while (plat_poll(plat, &in)) {
        double tnow = now_s();
        double ft = tnow - last;
        last = tnow;
        if (ft > 0.25) ft = 0.25;
        acc += ft;

        yaw += in.mouse_dx * 0.0028f;
        pitch = clampf(pitch - in.mouse_dy * 0.0028f, -1.2f, 1.2f);
        if (in.pressed & K_TOGGLE_VIEW) third_person = !third_person;
        if (in.keys & K_QUIT) break;

        while (acc >= DT) {
            acc -= DT;
            /* movement in look space */
            float speed = (in.keys & K_SPRINT) ? 5.8f :
                          (in.keys & K_CROUCH) ? 1.1f : 3.1f;
            player.height = (in.keys & K_CROUCH) ? 1.2f : 1.8f;
            V3 fwd = v3(sinf(yaw), 0, cosf(yaw));
            V3 right = v3(fwd.z, 0, -fwd.x);
            V3 wish = v3(0, 0, 0);
            if (in.keys & K_FWD) wish = v3_add(wish, fwd);
            if (in.keys & K_BACK) wish = v3_sub(wish, fwd);
            if (in.keys & K_RIGHT) wish = v3_add(wish, right);
            if (in.keys & K_LEFT) wish = v3_sub(wish, right);
            float moving = v3_len(wish) > 0.1f;
            if (moving) wish = v3_norm(wish);
            if (!player.grappling) {
                player.vel.x = wish.x * (moving ? speed : 0);
                player.vel.z = wish.z * (moving ? speed : 0);
            }
            noise = player.grappling ? 0.3f :
                    !moving ? 0.0f :
                    (in.keys & K_SPRINT) ? 0.8f :
                    (in.keys & K_CROUCH) ? 0.1f : 0.35f;
            if ((in.pressed & K_JUMP) && player.grounded) player.vel.y = 8.5f;

            /* grapple: press fires, hold keeps, release detaches */
            V3 eye = v3(player.pos.x, player.pos.y + player.height - 0.18f, player.pos.z);
            if ((in.pressed & K_GRAPPLE) && !player.grappling) {
                V3 anchor;
                if (find_anchor(eye, yaw, pitch, &anchor)) {
                    grapple_fire(&player, anchor);
                    au_live_grapple(&live);
                    grapples++;
                    noise = 0.9f;
                }
            }
            if (player.grappling && !(in.keys & K_GRAPPLE))
                grapple_release(&player);

            V3 prev = player.pos;
            physics_step(&player, WORLD, NWORLD, DT);
            float moved = v3_len(v3_sub(player.pos, prev));
            if (player.grounded && moving) {
                step_acc += moved;
                if (step_acc > 1.85f) { step_acc = 0; au_live_footstep(&live, 1); }
            }
            guard_update(&guard, GUARD_WP, 4, player.pos, noise, WORLD, NWORLD, DT);
            if (guard.alert > alert_peak) alert_peak = guard.alert;
            if (guard.alert > 0.7f && !alarmed) {
                alarmed = 1;
                au_live_tone(&live, 0.35f, 660, 880, 0.15f);
            }
            if (guard.alert < 0.3f) alarmed = 0;
        }

        /* camera */
        Camera cam;
        if (third_person) {
            V3 back = v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
            V3 want = v3_sub(v3(player.pos.x, player.pos.y + 1.6f, player.pos.z),
                             v3_scale(back, 6.5f));
            cam.pos = camera_resolve(want, WORLD, NWORLD, 0.45f);
            cam.yaw = yaw; cam.pitch = pitch;
        } else {
            cam.pos = v3(player.pos.x, player.pos.y + player.height - 0.18f, player.pos.z);
            cam.yaw = yaw; cam.pitch = pitch;
        }

        fb_sky(fb);
        fb_clear_depth(fb);
        const Tex *brick = have_brick_file ? &brick_file : &tex[TEX_BRICK];
        render_mesh(fb, &cam, &ground, v3(0, 0, 35), 0, &tex[TEX_CONCRETE], 1, 1, 1, &rig);
        render_mesh(fb, &cam, &ware1, v3(-4, 0, 22), 0, brick, 1, 1, 1, &rig);
        render_mesh(fb, &cam, &ware2, v3(10, 0, 27.5f), 0, &tex[TEX_METAL], 0.9f, 0.92f, 1, &rig);
        render_mesh(fb, &cam, &tower, v3(-2, 0, 33.4f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
        render_mesh(fb, &cam, &platform, v3(-2, 11, 32.5f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
        render_mesh(fb, &cam, &pole, v3(6, 0, 18), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
        render_mesh(fb, &cam, &pole, v3(-8, 0, 28), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
        render_mesh(fb, &cam, &lamp_head, v3(6, 4.4f, 18), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
        render_mesh(fb, &cam, &lamp_head, v3(-8, 4.4f, 28), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
        render_mesh(fb, &cam, &crate, v3(2, 0, 17.4f), 0.4f, &tex[TEX_METAL], 0.85f, 0.85f, 0.9f, &rig);
        double tt = now_s() - start;
        if (third_person) {
            Pose op = anim_sample(&CLIP_WALK, (float)tt * 1.7f);
            if (v3_len(player.vel) < 0.2f) op = anim_sample(&CLIP_IDLE, (float)tt);
            if (player.grappling) { op.arm_r = -2.4f; op.arm_l = -2.2f; }
            render_humanoid(fb, &cam, &oper, player.pos, yaw, &op, tex, 0.85f, 0.9f, 0.85f, &rig);
        }
        Pose gp = guard.wait > 0 ? anim_sample(&CLIP_IDLE, (float)tt)
                                 : anim_sample(&CLIP_WALK, (float)tt * 1.4f);
        render_humanoid(fb, &cam, &soldier, guard.pos, guard.yaw, &gp, tex, 1.05f, 0.95f, 0.85f, &rig);
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
            if (fb->zb[gy * FB_W + gx] < vz - 1.5f) continue;
            raster_glow(fb, gx, gy, (int)(260.0f / vz), 120, 66, 10);
        }
        raster_bar(fb, 8, 8, 90, 5, guard.alert, 0xd84040);
        raster_number(fb, FB_W - 40, 8, (int)frames % 1000, 0xcccccc);
        present(plat, fb);
        frames++;
        if (max_frames && frames >= max_frames) break;
    }

    printf("session: %.1f s, %ld frames (%.0f fps), grapples=%d, alert peak %.2f\n",
           now_s() - start, frames,
           frames / (now_s() - start > 0 ? now_s() - start : 1), grapples, alert_peak);
    printf("assets: brick.ppm=%s crate.obj=%s\n",
           have_brick_file ? "loaded" : "procedural fallback",
           have_crate ? "loaded" : "procedural fallback");
    audev_stop();
    plat_close(plat);
    fb_free(fb);
    humanoid_free(&oper);
    humanoid_free(&soldier);
    mesh_free(&ground); mesh_free(&ware1); mesh_free(&ware2);
    mesh_free(&tower); mesh_free(&platform); mesh_free(&lamp_head);
    mesh_free(&pole); mesh_free(&crate);
    if (have_brick_file) free(brick_file.px);
    texgen_free(tex);
    return 0;
}
