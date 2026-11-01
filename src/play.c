/* CASM-PS15 Interactive Stealth-Action Host: Greyport Night Operation.
 *
 * Perspective: Modern Third-Person Over-the-Shoulder (tightens on RMB aim;
 * V toggles front-orbit character showcase view).
 *
 * Modern Controls:
 *   WASD + Mouse / Arrows : Free 360 camera-relative movement & orbit look
 *   RMB / Q (hold)        : Over-the-shoulder precision ADS aim
 *   LMB / X / Return      : Fire suppressed HK USP (headshots, lamps, lures, Mecha)
 *   E (hold)              : Raycast-anything grapple (Ctrl holds length for pure swing)
 *   Space                 : Jump / Coyote jump / Wall-jump / Auto-mantle ledges
 *   Shift + C / Ctrl      : Sprint -> Tactical crouch-slide into cover
 *   F                     : Silent CQC takedown behind unaware trooper
 *   R                     : Hack Intel Terminal / Reload HK USP / New Op
 *   N (or 6)              : Toggle Crimson SIGINT Optic Visor
 *   M (or 8)              : Toggle Agency Handler Debrief Dossier
 *   1-5, 7                : Engine showcase (HUD, Snap, UV, DAC, Wireframe, CRT)
 *   Tab / Esc             : Release cursor / Quit */
#define _POSIX_C_SOURCE 202001L
#include "engine.h"
#include "greyport.h"
#include "platform.h"
#include <time.h>

static const V3 GUARD0_WP[] = {
    { 7.5f, 0.0f, 21.5f }, { 2.6f, 0.0f, 25.0f },
    { 2.6f, 0.0f, 14.5f }, { 7.5f, 0.0f, 15.0f },
};
static const V3 GUARD1_WP[] = {
    { -2.4f, 0.0f, 29.5f }, {  1.8f, 0.0f, 30.5f },
    {  1.8f, 0.0f, 26.8f }, { -2.4f, 0.0f, 27.2f },
};
static const V3 MECHA_WP[] = {
    { 9.2f, 0.0f, 11.5f }, { 9.2f, 0.0f, 21.6f },
    { 6.8f, 0.0f, 21.6f }, { 6.8f, 0.0f, 11.5f },
};

typedef struct {
    V3 pos;
    int value;
    uint32_t col;
    float timer;
    const char *tag;
} FloatPop;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static float turn_toward(float cur, float target, float max_step) {
    float d = target - cur;
    while (d >  PS15_PI) d -= 2.0f * PS15_PI;
    while (d < -PS15_PI) d += 2.0f * PS15_PI;
    if (fabsf(d) <= max_step) return target;
    return cur + (d > 0.0f ? max_step : -max_step);
}

static uint32_t present_buf[960 * 540];
static void present(Plat *plat, const FB *fb) {
    if (fb->w == 960 && fb->h == 540) {
        /* Modern tier: the framebuffer already is the 960x540 canvas —
         * present 1:1 (CRT mode shades odd rows in place). */
        memcpy(present_buf, fb->px, sizeof present_buf);
        if (fb->settings.crt_scanlines) {
            for (int y = 1; y < 540; y += 2) {
                uint32_t *row = present_buf + (size_t)y * 960u;
                for (int x = 0; x < 960; x++) {
                    uint32_t p = row[x];
                    row[x] = p - ((p >> 2) & 0x003F3F3Fu);
                }
            }
        }
    } else if (fb->settings.crt_scanlines) {
        ps15_upscale2x_crt(present_buf, fb->px, fb->w, fb->h);
    } else {
        ps15_upscale2x(present_buf, fb->px, fb->w, fb->h);
    }
    plat_present(plat, present_buf);
}

static void spawn_pop(FloatPop pops[8], V3 pos, int val, const char *tag, uint32_t col) {
    int slot = 0;
    float best = 999.0f;
    for (int i = 0; i < 8; i++) {
        if (pops[i].timer <= 0.0f) { slot = i; break; }
        if (pops[i].timer < best) { best = pops[i].timer; slot = i; }
    }
    pops[slot] = (FloatPop){ pos, val, col, 1.15f, tag };
}

static const char *compute_grade(int alarms, int takedowns, int shots_fired, bool mecha_down) {
    if (alarms == 0 && takedowns == 0 && !mecha_down && shots_fired == 0)
        return "GHOST-THAT-NEVER-WAS";
    if (alarms == 0 && takedowns <= 1 && !mecha_down)
        return "GHOST";
    if (alarms == 0)
        return "SURGEON";
    if (alarms <= 2 && !mecha_down)
        return "OPERATIVE";
    return "BUTCHER";
}

int main(int argc, char **argv) {
    long max_frames = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--frames") == 0) max_frames = strtol(argv[i + 1], NULL, 10);

    GreyportScene sc;
    greyport_init(&sc);
    FB *fb = fb_new(FB_MODERN_W, FB_MODERN_H);
    Humanoid oper    = humanoid_new();
    Humanoid soldier = humanoid_guard_new();
    MechaMesh mmesh  = mecha_mesh_new();
    ParticlePool particles;
    particles_init(&particles, 0xCA515001ULL);

    Plat *plat = plat_open("CASM-PS15 — DeBrief: Greyport Harbor", 960, 540);
    if (!plat) { fprintf(stderr, "no window available\n"); return 1; }
    AudioLive live;
    au_live_init(&live);
    int audio_ok = (audev_start(&live) == 0);
    printf("window: X11/Cocoa platform  audio: %s\n",
           audio_ok ? audev_name() : "unavailable (silent session)");

    Body player = { v3(-3.2f, 0.0f, 8.5f), v3(0, 0, 0), 0.35f, 1.8f, true, false, v3(0, 0, 0), 0 };
    Guard guards[2] = {
        { v3( 7.5f, 0.0f, 21.5f), 3.14f, 0.0f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f },
        { v3(-2.4f, 0.0f, 29.5f), 1.57f, 0.0f, 0, 0.4f, 0.0f, v3(0,0,0), 0.0f }
    };
    Mecha walker;
    mecha_init(&walker, v3(9.2f, 0.0f, 13.5f), 0.0f);

    float cam_yaw = 0.18f, cam_pitch = -0.08f, char_yaw = 0.18f;
    Camera cam = tps_camera_step((Camera){ player.pos, cam_yaw, cam_pitch },
                                 &player, cam_yaw, cam_pitch, false, false,
                                 sc.world, sc.nworld, 1.0f);

    int showcase_front = 0, show_tech = 0, show_dossier = 0;
    int ammo = 12, shots_fired = 0, takedowns = 0, alarms = 0, grapples = 0;
    bool crouched = false, alarmed = false, grapple_latch_guard = false, extracted = false;
    float slide_timer = 0.0f, coyote_t = 0.0f, jump_buf_t = 0.0f, wall_timer = 1.35f;
    V3 slide_dir = v3(0, 0, 1), wall_norm = v3(0, 0, 0);
    bool wall_running = false;
    float fire_cd = 0.0f, recoil = 0.0f, hitmark_t = 0.0f, op_time = 0.0f;
    float alert_peak = 0.0f, step_acc = 0.0f, noise = 0.0f, last_render_ms = 0.0f;
    const char *banner_msg = "INFILTRATE DOCKYARD: HACK 2 INTEL TERMINALS [R]";
    float banner_t = 4.5f;
    FloatPop pops[8];
    memset(pops, 0, sizeof pops);

    long frames = 0;
    double start = now_s(), last = start, acc = 0.0;
    const float DT = 1.0f / 60.0f;

    Input in;
    while (plat_poll(plat, &in)) {
        double tnow = now_s();
        double ft = tnow - last;
        last = tnow;
        if (ft > 0.25) ft = 0.25;
        acc += ft;

        bool aiming = (in.keys & K_AIM) != 0;
        float sens = aiming ? 0.0017f : 0.0028f;
        cam_yaw += in.mouse_dx * sens;
        cam_pitch = clampf(cam_pitch - in.mouse_dy * sens, -1.18f, 1.15f);
        if (in.keys & K_LOOK_L) cam_yaw   -= (float)ft * 2.1f;
        if (in.keys & K_LOOK_R) cam_yaw   += (float)ft * 2.1f;
        if (in.keys & K_LOOK_U) cam_pitch  = clampf(cam_pitch + (float)ft * 1.5f, -1.18f, 1.15f);
        if (in.keys & K_LOOK_D) cam_pitch  = clampf(cam_pitch - (float)ft * 1.5f, -1.18f, 1.15f);

        if (in.pressed & K_TOGGLE_VIEW) showcase_front = !showcase_front;
        if (in.pressed & K_HUD)         show_tech = !show_tech;
        if (in.pressed & K_DOSSIER)     show_dossier = !show_dossier;
        if (in.pressed & K_SNAP_MODE)   fb->settings.snap_mode = (fb->settings.snap_mode + 1) % 3;
        if (in.pressed & K_UV_MODE)     fb->settings.affine_uv = !fb->settings.affine_uv;
        if (in.pressed & K_DAC_MODE) {
            GraphicProfile next = (fb->settings.profile + 1) % 3;
            fb_set_profile(fb, next);
        }
        if (in.pressed & K_WIRE_MODE)   fb->settings.wireframe = !fb->settings.wireframe;
        if (in.pressed & K_CRT)         fb->settings.crt_scanlines = !fb->settings.crt_scanlines;
        if (in.pressed & K_VISOR) {
            fb->settings.visor_mode = !fb->settings.visor_mode;
            au_live_visor(&live);
        }
        if (in.pressed & K_JUMP) jump_buf_t = 0.14f;
        if (in.keys & K_QUIT) break;

        while (acc >= DT) {
            acc -= DT;
            particles_update(&particles, DT);
            fb->settings.fov_scale = lerpf(fb->settings.fov_scale, aiming ? 1.22f : 1.0f, DT * 12.0f);
            if (!extracted) op_time += DT;
            if (fire_cd > 0.0f)   fire_cd   = fmaxf(0.0f, fire_cd - DT);
            if (recoil > 0.0f)    recoil    = fmaxf(0.0f, recoil - DT * 5.5f);
            if (hitmark_t > 0.0f) hitmark_t = fmaxf(0.0f, hitmark_t - DT);
            if (banner_t > 0.0f)  banner_t  = fmaxf(0.0f, banner_t - DT);
            if (sc.muzzle_flash > 0.0f) sc.muzzle_flash = fmaxf(0.0f, sc.muzzle_flash - DT * 9.0f);
            if (jump_buf_t > 0.0f) jump_buf_t = fmaxf(0.0f, jump_buf_t - DT);
            for (int i = 0; i < 8; i++)
                if (pops[i].timer > 0.0f) pops[i].timer = fmaxf(0.0f, pops[i].timer - DT);

            V3 fwd = v3(sinf(cam_yaw), 0.0f, cosf(cam_yaw));
            V3 right = v3(fwd.z, 0.0f, -fwd.x);
            V3 wish = v3(0, 0, 0);
            if (in.keys & K_FWD)   wish = v3_add(wish, fwd);
            if (in.keys & K_BACK)  wish = v3_sub(wish, fwd);
            if (in.keys & K_RIGHT) wish = v3_add(wish, right);
            if (in.keys & K_LEFT)  wish = v3_sub(wish, right);
            bool moving = v3_len(wish) > 0.1f;
            if (moving) wish = v3_norm(wish);

            /* Sprint -> Tactical Crouch-Slide when pressing crouch at speed! */
            bool want_crouch = (in.keys & K_CROUCH) != 0;
            float cur_hspd = hypotf(player.vel.x, player.vel.z);
            if ((in.pressed & K_CROUCH) && player.grounded && cur_hspd > 4.6f && slide_timer <= 0.0f) {
                slide_timer = 0.52f;
                slide_dir = moving ? wish : v3(sinf(char_yaw), 0.0f, cosf(char_yaw));
                crouched = true;
                player.height = 1.15f;
                au_live_footstep(&live, 1);
                in.pressed &= ~K_CROUCH;
            } else if (want_crouch || slide_timer > 0.0f) {
                crouched = true;
                player.height = 1.2f;
            } else if (crouched && physics_can_stand(&player, sc.world, sc.nworld, 1.8f)) {
                crouched = false;
                player.height = 1.8f;
            }

            if (slide_timer > 0.0f) {
                slide_timer = fmaxf(0.0f, slide_timer - DT);
                float s_spd = 7.4f * (0.45f + 0.55f * (slide_timer / 0.52f));
                player.vel.x = slide_dir.x * s_spd;
                player.vel.z = slide_dir.z * s_spd;
            } else if (!player.grappling) {
                float target_spd = aiming ? 2.15f : crouched ? 1.65f :
                                   (in.keys & K_SPRINT) ? 6.1f : 3.65f;
                float tx = moving ? wish.x * target_spd : 0.0f;
                float tz = moving ? wish.z * target_spd : 0.0f;
                float accel = player.grounded ? 18.0f : 6.5f;
                player.vel.x = lerpf(player.vel.x, tx, clampf(accel * DT, 0.0f, 1.0f));
                player.vel.z = lerpf(player.vel.z, tz, clampf(accel * DT, 0.0f, 1.0f));
            } else if (moving) {
                /* Nexuiz Tarzan pendulum swing steering */
                player.vel.x += wish.x * 7.8f * DT;
                player.vel.z += wish.z * 7.8f * DT;
            }

            /* Smooth 360 character facing: follows movement, locks to reticle on ADS/fire */
            if (aiming || recoil > 0.05f) {
                char_yaw = turn_toward(char_yaw, cam_yaw, 18.0f * DT);
            } else if (slide_timer > 0.0f) {
                char_yaw = turn_toward(char_yaw, atan2f(slide_dir.x, slide_dir.z), 14.0f * DT);
            } else if (moving) {
                char_yaw = turn_toward(char_yaw, atan2f(wish.x, wish.z), 12.5f * DT);
            }

            /* Coyote time + Jump buffering + Wall-jump + Ledge auto-mantle */
            if (player.grounded) coyote_t = 0.12f;
            else                 coyote_t = fmaxf(0.0f, coyote_t - DT);

            if (jump_buf_t > 0.0f) {
                if (coyote_t > 0.0f) {
                    player.vel.y = 8.7f;
                    player.grounded = false;
                    coyote_t = 0.0f;
                    jump_buf_t = 0.0f;
                } else if (wall_running) {
                    player.vel.y = 8.4f;
                    player.vel.x += wall_norm.x * 5.6f;
                    player.vel.z += wall_norm.z * 5.6f;
                    wall_running = false;
                    jump_buf_t = 0.0f;
                    au_live_footstep(&live, 0);
                }
            }

            /* Nexuiz-style Grapple (EVERY surface grapples; waiting-for-release guard) */
            if (!(in.keys & K_GRAPPLE)) grapple_latch_guard = false;
            if ((in.keys & K_GRAPPLE) && !player.grappling && !grapple_latch_guard) {
                V3 aim_dir = v3(sinf(cam_yaw) * cosf(cam_pitch), sinf(cam_pitch),
                                cosf(cam_yaw) * cosf(cam_pitch));
                V3 launch_eye = v3(player.pos.x, player.pos.y + 1.52f, player.pos.z);
                if (grapple_fire_ray(&player, launch_eye, aim_dir,
                                     sc.world, sc.nworld, sc.anchors, sc.nanchors, 32.0f)) {
                    au_live_grapple(&live);
                    grapples++;
                    noise = 0.78f;
                }
                grapple_latch_guard = true;
            }
            if (player.grappling && !(in.keys & K_GRAPPLE))
                grapple_release(&player);

            /* Suppressed HK USP Fire (LMB): WYSIWYG ray from over-shoulder reticle! */
            if ((in.pressed & K_FIRE) && fire_cd <= 0.0f && !player.grappling) {
                in.pressed &= ~K_FIRE;
                if (ammo > 0) {
                    ammo--;
                    shots_fired++;
                    fire_cd = 0.17f;
                    recoil = 1.0f;
                    char_yaw = cam_yaw;
                    au_live_pistol(&live);
                    sc.muzzle_pos = v3(player.pos.x + sinf(char_yaw) * 0.55f,
                                       player.pos.y + (crouched ? 1.05f : 1.42f),
                                       player.pos.z + cosf(char_yaw) * 0.55f);
                    sc.muzzle_flash = 1.0f;
                    noise = fmaxf(noise, 0.45f);

                    V3 rdir = v3_norm(v3(sinf(cam.yaw) * cosf(cam.pitch), sinf(cam.pitch),
                                         cosf(cam.yaw) * cosf(cam.pitch)));
                    V3 rorig = v3_add(cam.pos, v3_scale(rdir, 1.15f));
                    V3 hit_pt;
                    int hk = greyport_fire_ray_ex(&sc, rorig, rdir, guards, 2, &walker, &hit_pt);
                    if (hk == GP_HIT_LAMP) {
                        au_live_glass(&live);
                        hitmark_t = 0.22f;
                        spawn_pop(pops, hit_pt, 0, "LAMP OUT", 0xffd258u);
                        particles_spawn_burst(&particles, hit_pt, v3(0, -1.0f, 0), 14, 2.2f, 7.5f, 0.45f, 0xffd258u, true);
                    } else if (hk == GP_HIT_GUARD_HEAD) {
                        takedowns++;
                        hitmark_t = 0.28f;
                        spawn_pop(pops, hit_pt, 100, "CRIT", 0xff6238u);
                        particles_spawn_burst(&particles, hit_pt, v3(0, 0.6f, 0), 12, 1.8f, 5.0f, 0.35f, 0xff4230u, false);
                    } else if (hk == GP_HIT_GUARD_BODY) {
                        takedowns++;
                        hitmark_t = 0.20f;
                        spawn_pop(pops, hit_pt, 38, NULL, 0xffbe3cu);
                        particles_spawn_burst(&particles, hit_pt, v3(0, 0.3f, 0), 8, 1.4f, 5.0f, 0.28f, 0xff6040u, false);
                    } else if (hk == GP_HIT_MECHA_SENSOR) {
                        hitmark_t = 0.25f;
                        spawn_pop(pops, hit_pt, 45, "SENSOR", 0xff6238u);
                        particles_spawn_burst(&particles, hit_pt, v3_scale(rdir, -1.6f), 14, 2.5f, 4.0f, 0.42f, 0xffc848u, true);
                    } else if (hk == GP_HIT_MECHA_ACTUATOR) {
                        hitmark_t = 0.22f;
                        spawn_pop(pops, hit_pt, 32, "ACTUATOR", 0xffbe3cu);
                        particles_spawn_burst(&particles, hit_pt, v3_scale(rdir, -1.6f), 12, 2.2f, 4.0f, 0.38f, 0xffc848u, true);
                    } else if (hk == GP_HIT_MECHA_COOLANT) {
                        hitmark_t = 0.30f;
                        spawn_pop(pops, hit_pt, 85, walker.coolant_hp <= 0.01f ? "MECHA DOWN" : "COOLANT", 0xff4838u);
                        particles_spawn_burst(&particles, hit_pt, v3(0, 1.8f, 0), 18, 3.2f, 1.5f, 0.65f, 0x58c8ffu, true);
                    } else if (hk == GP_HIT_WALL) {
                        particles_spawn_burst(&particles, hit_pt, v3_scale(rdir, -1.2f), 6, 1.6f, 6.0f, 0.25f, 0xd8caa8u, false);
                    }
                } else {
                    au_live_tone(&live, 0.04f, 420.0f, 310.0f, 0.08f);
                    banner_msg = "MAGAZINE EMPTY - PRESS [R] TO RELOAD";
                    banner_t = 2.0f;
                }
            }

            /* Silent Close-Quarters Takedown (F) behind trooper */
            if (in.pressed & K_MELEE) {
                in.pressed &= ~K_MELEE;
                for (int i = 0; i < 2; i++) {
                    if (guards[i].stunned > 0.0f) continue;
                    float d = v3_len(v3_sub(guards[i].pos, player.pos));
                    if (d < 2.1f) {
                        guards[i].stunned = 30.0f;
                        guards[i].alert = 0.0f;
                        takedowns++;
                        au_live_footstep(&live, 0);
                        spawn_pop(pops, v3_add(guards[i].pos, v3(0, 1.5f, 0)), 0, "TAKEDOWN", 0x74e8a8u);
                        break;
                    }
                }
            }

            /* Interact / Hack Terminal / Reload / Restart Op (R) */
            if (in.pressed & K_USE) {
                in.pressed &= ~K_USE;
                if (extracted) {
                    /* Fresh mission reset */
                    player.pos = v3(-3.2f, 0.0f, 8.5f);
                    player.vel = v3(0, 0, 0);
                    sc.term_hacked[0] = sc.term_hacked[1] = false;
                    sc.lamp_out[0] = sc.lamp_out[1] = sc.lamp_out[2] = false;
                    mecha_init(&walker, v3(9.2f, 0.0f, 13.5f), 0.0f);
                    ammo = 12; shots_fired = 0; takedowns = 0; alarms = 0; op_time = 0.0f;
                    extracted = false; show_dossier = 0;
                    banner_msg = "NEW OPERATION LIVE - INFILTRATE DOCKYARD";
                    banner_t = 3.5f;
                } else {
                    bool hacked_any = false;
                    for (int i = 0; i < GREYPORT_NUM_TERMS; i++) {
                        if (!sc.term_hacked[i] &&
                            v3_len(v3_sub(sc.term_pos[i], player.pos)) < 2.4f) {
                            sc.term_hacked[i] = true;
                            hacked_any = true;
                            au_live_hack(&live);
                            spawn_pop(pops, v3_add(sc.term_pos[i], v3(0, 1.4f, 0)), 0, "INTEL FILED", 0x68f0a8u);
                            banner_msg = (sc.term_hacked[0] && sc.term_hacked[1])
                                ? "ALL ARCHIVE FILES SECURED - EXTRACT AT WEST HARBOR BARGE!"
                                : "TERMINAL 1/2 HACKED - LOCATE SECOND TERMINAL";
                            banner_t = 4.5f;
                        }
                    }
                    if (!hacked_any && ammo < 12) {
                        ammo = 12;
                        au_live_tone(&live, 0.07f, 620.0f, 920.0f, 0.09f);
                        banner_msg = "HK USP RELOADED (12 RDS)";
                        banner_t = 1.6f;
                    }
                }
            }

            /* Check Harbor Barge Extraction once both terminals are hacked */
            if (!extracted && sc.term_hacked[0] && sc.term_hacked[1]) {
                if (v3_len(v3_sub(player.pos, v3(-11.5f, 0.0f, 18.0f))) < 3.6f) {
                    extracted = true;
                    show_dossier = 1;
                    au_live_hack(&live);
                    banner_msg = "OPERATION COMPLETE - HANDLER DEBRIEF FILED";
                    banner_t = 8.0f;
                }
            }

            noise = fmaxf(noise * 0.90f,
                          player.grappling ? 0.35f :
                          !moving ? 0.0f :
                          crouched ? 0.06f :
                          (in.keys & K_SPRINT) ? 0.82f : 0.30f);

            V3 prev = player.pos;
            wall_running = physics_wallrun_step(&player, &wall_norm, &wall_timer,
                                                sc.world, sc.nworld, DT);
            if (!wall_running && moving)
                physics_mantle_step(&player, wish, sc.world, sc.nworld, DT);
            physics_step(&player, sc.world, sc.nworld, DT);

            float moved = v3_len(v3_sub(player.pos, prev));
            if ((player.grounded || wall_running) && moving && slide_timer <= 0.0f) {
                step_acc += moved;
                if (step_acc > (crouched ? 2.2f : 1.80f)) {
                    step_acc = 0.0f;
                    if (!crouched) au_live_footstep(&live, !wall_running);
                }
            }

            float vis = light_exposure_at(&sc.rig, v3(player.pos.x, player.pos.y + (crouched ? 0.65f : 1.15f), player.pos.z));
            if (crouched) vis *= 0.65f;
            guard_update_ex(&guards[0], GUARD0_WP, 4, player.pos, noise, vis, sc.world, sc.nworld, DT);
            guard_update_ex(&guards[1], GUARD1_WP, 4, player.pos, noise, vis, sc.world, sc.nworld, DT);
            mecha_update(&walker, MECHA_WP, 4, player.pos, noise, vis, sc.world, sc.nworld, DT);
            if (walker.step_event) {
                float mg, mpan;
                au_positional(v3_len(v3_sub(walker.pos, player.pos)), &mg, &mpan);
                au_live_mecha_step(&live, mg * 1.1f);
            }

            float max_al = fmaxf(fmaxf(guards[0].alert, guards[1].alert), walker.alert);
            if (max_al > alert_peak) alert_peak = max_al;
            if (max_al > 0.72f && !alarmed) {
                alarmed = true;
                alarms++;
                au_live_tone(&live, 0.35f, 660.0f, 880.0f, 0.14f);
            }
            if (max_al < 0.30f) alarmed = false;

            if (showcase_front) {
                V3 front_pos = v3(player.pos.x + sinf(char_yaw) * 1.65f,
                                  player.pos.y + 1.32f,
                                  player.pos.z + cosf(char_yaw) * 1.65f);
                cam.pos = camera_resolve(front_pos, sc.world, sc.nworld, 0.25f);
                cam.yaw = char_yaw + PS15_PI;
                cam.pitch = -0.04f;
            } else {
                cam = tps_camera_step(cam, &player, cam_yaw, cam_pitch,
                                      crouched, aiming, sc.world, sc.nworld, DT);
            }
        }

        double r0 = now_s();
        float tt = (float)(r0 - start);
        greyport_update_lights(&sc, guards, 2, tt);
        greyport_render_opaque(fb, &sc, &cam, tt);

        /* Grounded contact shadows */
        float op_gy = world_ground_y(sc.world, sc.nworld, v3(player.pos.x, player.pos.y + 0.2f, player.pos.z));
        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                           v3(player.pos.x, op_gy, player.pos.z), 0.58f);
        for (int i = 0; i < 2; i++)
            render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW], guards[i].pos, 0.58f);
        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW], walker.pos, 1.25f);

        /* Operator full-body modern animation state machine */
        float hspd = hypotf(player.vel.x, player.vel.z);
        Pose op;
        if (slide_timer > 0.0f) {
            op = anim_sample(&CLIP_CROUCH, tt);
            op.crouch = 1.0f; op.lean = -0.32f; op.leg_r = -0.95f; op.knee_r = 0.15f;
        } else if (crouched) {
            op = anim_sample(&CLIP_CROUCH, hspd > 0.2f ? tt * 1.5f : 0.0f);
        } else if (wall_running) {
            op = anim_sample(&CLIP_RUN, tt * 1.6f);
            op.lean = 0.24f;
        } else if (hspd > 4.3f) {
            op = anim_sample(&CLIP_RUN, tt * 1.35f);
        } else if (hspd > 0.25f) {
            op = anim_sample(&CLIP_WALK, tt * 1.6f);
        } else {
            op = anim_sample(&CLIP_IDLE, tt);
        }
        if (aiming || recoil > 0.05f || showcase_front) {
            Pose ap = anim_sample(&CLIP_AIM, tt);
            op.arm_l = ap.arm_l - cam_pitch * 0.55f;
            op.arm_r = ap.arm_r - cam_pitch * 0.55f - recoil * 0.22f;
            op.elbow_l = ap.elbow_l;
            op.elbow_r = ap.elbow_r;
            op.aim = 1.0f;
            op.head_yaw = clampf(cam_yaw - char_yaw, -0.6f, 0.6f);
        }
        if (player.grappling) {
            op.arm_r = -2.35f; op.arm_l = -2.05f; op.aim = 0.0f;
        }
        render_humanoid(fb, &cam, &oper, player.pos, char_yaw, &op,
                        sc.tex, 1.0f, 1.0f, 1.02f, &sc.rig);

        if (player.grappling) {
            V3 hand = v3(player.pos.x + sinf(char_yaw) * 0.25f,
                         player.pos.y + 1.52f,
                         player.pos.z + cosf(char_yaw) * 0.25f);
            greyport_render_grapple(fb, &sc, &cam, hand, player.grapple_anchor);
        }

        /* Regime troopers + autonomous Patrol Mecha */
        for (int i = 0; i < 2; i++) {
            Pose gp = (guards[i].stunned > 0.0f) ? anim_sample(&CLIP_CROUCH, 0.0f)
                    : (guards[i].wait > 0.0f)    ? anim_sample(&CLIP_IDLE, tt)
                                                 : anim_sample(&CLIP_WALK, tt * 1.35f);
            if (guards[i].stunned > 0.0f) { gp.crouch = 1.0f; gp.lean = 0.75f; }
            else { gp.arm_r = -0.72f; gp.elbow_r = 0.65f; }
            render_humanoid(fb, &cam, &soldier, guards[i].pos, guards[i].yaw, &gp,
                            sc.tex, 1.0f, 1.0f, 1.0f, &sc.rig);
        }
        render_mecha(fb, &cam, &mmesh, &walker, sc.tex, &sc.rig);

        /* Extraction beacon on West Harbor Barge once intel is secured */
        if (sc.term_hacked[0] && sc.term_hacked[1] && !extracted) {
            render_glow3d(fb, &cam, v3(-11.5f, 1.2f, 18.0f), 1.4f, 45, 195, 115);
        }

        greyport_render_fx(fb, &sc, &cam, guards, 2, tt);
        particles_render(fb, &cam, &particles);
        if (fb->settings.visor_mode) {
            fb_apply_visor(fb, tt);
        } else if (fb->settings.profile == GFX_PS2) {
            fb_bloom(fb, 0.70f, 0.32f);
        }

        /* World-projected Mecha SIGINT telemetry callout (hud-dockside.jpg!) */
        int mx, my; float mz;
        if ((aiming || fb->settings.visor_mode) &&
            render_project(fb, &cam, v3(walker.pos.x, walker.pos.y + 2.1f, walker.pos.z), &mx, &my, &mz) &&
            mz < 34.0f) {
            raster_rect(fb, mx - 1, my - 1, 3, 3, 0xffbe3cu);
            raster_line2d(fb, mx + 2, my, mx + 14, my - 8, 0xd69428u);
            raster_rect(fb, mx + 14, my - 8, 18, 1, 0xd69428u);
            char mtag[40];
            if (walker.coolant_hp <= 0.01f)
                snprintf(mtag, sizeof mtag, "GOVERNOR OFFLINE");
            else if (walker.sensor_hp < walker.actuator_hp)
                snprintf(mtag, sizeof mtag, "SENSOR %d%%", (int)(walker.sensor_hp * 100.0f));
            else
                snprintf(mtag, sizeof mtag, "ACTUATOR %d%%", (int)(walker.actuator_hp * 100.0f));
            raster_text(fb, mx + 16, my - 15, mtag, 0xffbe3cu);
        }

        /* Floating amber visor hit numbers & event tags */
        for (int i = 0; i < 8; i++) {
            if (pops[i].timer <= 0.0f) continue;
            V3 wpos = v3_add(pops[i].pos, v3(0.0f, (1.15f - pops[i].timer) * 0.85f, 0.0f));
            int sx, sy;
            if (render_project(fb, &cam, wpos, &sx, &sy, NULL)) {
                if (pops[i].tag) raster_text(fb, sx - 12, sy - 7, pops[i].tag, pops[i].col);
                if (pops[i].value > 0) raster_number(fb, sx - 4, sy, pops[i].value, pops[i].col);
            }
        }

        /* Precision over-the-shoulder reticle + dynamic hitmarker */
        if (!showcase_front) {
            int cx = fb->w / 2, cy = fb->h / 2;
            int gap = aiming ? 3 : 5;
            uint32_t rc = aiming ? 0xffbe3cu : 0x94a8c8u;
            raster_rect(fb, cx - gap - 3, cy, 3, 1, rc);
            raster_rect(fb, cx + gap + 1, cy, 3, 1, rc);
            raster_rect(fb, cx, cy - gap - 3, 1, 3, rc);
            raster_rect(fb, cx, cy + gap + 1, 1, 3, rc);
            if (hitmark_t > 0.0f) {
                raster_line2d(fb, cx - 5, cy - 5, cx - 2, cy - 2, 0xff6238u);
                raster_line2d(fb, cx + 5, cy - 5, cx + 2, cy - 2, 0xff6238u);
                raster_line2d(fb, cx - 5, cy + 5, cx - 2, cy + 2, 0xff6238u);
                raster_line2d(fb, cx + 5, cy + 5, cx + 2, cy + 2, 0xff6238u);
            }
        }

        /* Diegetic Amber Visor HUD */
        float vis_now = light_exposure_at(&sc.rig, v3(player.pos.x, player.pos.y + 1.0f, player.pos.z));
        float max_al = fmaxf(fmaxf(guards[0].alert, guards[1].alert), walker.alert);
        hud_draw_visor(fb, cam.yaw, op_time, ammo, vis_now, max_al, noise, player.grappling);

        /* Top-left live objective tracker */
        int n_hacked = (sc.term_hacked[0] ? 1 : 0) + (sc.term_hacked[1] ? 1 : 0);
        char obj_line[64];
        if (extracted)
            snprintf(obj_line, sizeof obj_line, "EXTRACTED [R: NEW OP]");
        else if (n_hacked < 2)
            snprintf(obj_line, sizeof obj_line, "INTEL: %d/2 [R:HACK]", n_hacked);
        else
            snprintf(obj_line, sizeof obj_line, "EXTRACT: WEST BARGE!");
        raster_text(fb, 10, 8, obj_line, n_hacked == 2 ? 0x68f0a8u : 0xffbe3cu);
        if (banner_t > 0.0f)
            raster_text(fb, fb->w / 2 - (int)strlen(banner_msg) * 3, 33, banner_msg, 0xffd468u);

        /* Agency Handler Debrief Dossier Overlay (M / 8 or on Extraction) */
        if (show_dossier) {
            raster_rect(fb, 96, 48, 288, 152, 0x0b0f18u);
            raster_rect(fb, 96, 48, 288, 1, 0xffbe3cu);
            raster_rect(fb, 96, 199, 288, 1, 0xffbe3cu);
            raster_rect(fb, 96, 48, 1, 152, 0xd69428u);
            raster_rect(fb, 383, 48, 1, 152, 0xd69428u);
            raster_text(fb, 110, 58, "NSA ARCHIVE // HANDLER DEBRIEF RECORD", 0xffbe3cu);
            char dline[64];
            snprintf(dline, sizeof dline, "STATUS      : %s", extracted ? "EXTRACTED" : "IN FIELD");
            raster_text(fb, 110, 76, dline, 0xd0d8e8u);
            snprintf(dline, sizeof dline, "INTEL FILED : %d / 2 TERMINALS", n_hacked);
            raster_text(fb, 110, 88, dline, 0xd0d8e8u);
            snprintf(dline, sizeof dline, "ALARMS      : %d   PEAK ALERT: %d%%", alarms, (int)(alert_peak * 100.0f));
            raster_text(fb, 110, 100, dline, 0xd0d8e8u);
            snprintf(dline, sizeof dline, "TAKEDOWNS   : %d   SHOTS FIRED: %d", takedowns, shots_fired);
            raster_text(fb, 110, 112, dline, 0xd0d8e8u);
            int lamps_out = (sc.lamp_out[0]?1:0) + (sc.lamp_out[1]?1:0) + (sc.lamp_out[2]?1:0);
            snprintf(dline, sizeof dline, "LAMPS CUT   : %d / 3   MECHA: %s",
                     lamps_out, walker.coolant_hp <= 0.01f ? "DISABLED" : "ACTIVE");
            raster_text(fb, 110, 124, dline, 0xd0d8e8u);
            snprintf(dline, sizeof dline, "EVALUATION  : %s",
                     compute_grade(alarms, takedowns, shots_fired, walker.coolant_hp <= 0.01f));
            raster_text(fb, 110, 144, dline, 0x68f0a8u);
            raster_text(fb, 110, 176, extracted ? "[R] LAUNCH NEXT OP   [M] CLOSE"
                                                : "[M] CLOSE DOSSIER    [1] ENGINE STATS", 0x94a8c8u);
        }

        if (show_tech) {
            static const char *DAC_NAMES[3]  = { "555+BAYER", "555-FLAT", "RGB888" };
            static const char *GFX_NAMES[3]  = { "PS1", "PS1.5", "PS2" };
            char line[112];
            snprintf(line, sizeof line, "TRIS:%d MS:%.1f GFX:%s FILTER:%s DAC:%s",
                     fb->stats.tris_drawn, last_render_ms,
                     GFX_NAMES[fb->settings.profile],
                     fb->settings.filter_mode == FILTER_BILINEAR ? "BILIN" : "POINT",
                     DAC_NAMES[fb->settings.dither_mode]);
            raster_text(fb, 76, fb->h - 9, line, 0x7c92b4u);
        }

        last_render_ms = (float)((now_s() - r0) * 1000.0);
        present(plat, fb);
        frames++;
        if (max_frames && frames >= max_frames) break;
    }

    double elapsed = now_s() - start;
    printf("session: %.1f s, %ld frames (%.0f fps), grapples=%d, alert peak %.2f, grade=%s\n",
           elapsed, frames, frames / (elapsed > 0.001 ? elapsed : 1.0),
           grapples, alert_peak,
           compute_grade(alarms, takedowns, shots_fired, walker.coolant_hp <= 0.01f));

    audev_stop();
    plat_close(plat);
    fb_free(fb);
    mecha_mesh_free(&mmesh);
    humanoid_free(&oper);
    humanoid_free(&soldier);
    greyport_free(&sc);
    return 0;
}
