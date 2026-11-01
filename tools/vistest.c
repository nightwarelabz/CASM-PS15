/* Visual test harness: renders the Greyport harbor, sculpted NSA operator,
 * Regime Trooper, Type-1 Patrol Mecha, planar canal reflections, and diegetic
 * visor HUD from showcase and stress-test camera poses.
 *   operator_hero    — 3/4 espionage character close-up (DeBrief banner homage)
 *   operator_rear    — 3/4 rear inspection (MOLLE pack, radio pack & twin whips)
 *   tps_action       — Third-person over-shoulder gameplay + grapple + amber HUD
 *   canal_reflection — Flooded harbor canal planar reflections + viaduct + crane
 *   mecha_trooper    — Type-1 Patrol Mecha + Regime Trooper + 3-bar banners
 *   visor_sigint     — Crimson SIGINT night-optic visor + CRT scanlines
 *   street_hero      — Wide shot down the sodium-lit Greyport canyon
 *   wall_close       — Near corner brick bevels & mortar alignment
 * Run from the repo root: ./vistest <outdir> */
#include "engine.h"
#include "greyport.h"
#include <sys/stat.h>

static int write_ppm(const char *path, const FB *fb, bool crt) {
    uint32_t *up = crt ? fb_upscale2x_crt(fb) : fb_upscale2x(fb);
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

typedef enum {
    SHOT_HERO,
    SHOT_REAR,
    SHOT_TPS_ACTION,
    SHOT_CANAL,
    SHOT_MECHA,
    SHOT_VISOR,
    SHOT_STREET,
    SHOT_WALL_CLOSE,
    SHOT_STUDIO_OP_FRONT,
    SHOT_STUDIO_OP_REAR,
    SHOT_STUDIO_SOLDIER_FRONT,
    SHOT_STUDIO_SOLDIER_REAR,
    SHOT_STUDIO_MECHA_FRONT,
    SHOT_STUDIO_MECHA_REAR
} ShotKind;

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "vistest-out";
    mkdir(outdir, 0755);

    GreyportScene sc;
    greyport_init(&sc);
    Humanoid oper    = humanoid_new();
    Humanoid soldier = humanoid_guard_new();
    MechaMesh mmesh  = mecha_mesh_new();
    FB *fb = fb_new(FB_W, FB_H);
    fb_set_profile(fb, GFX_PS2); /* PS2 Emotion Engine GS: TrueColor + subpixel + bilinear */

    static const struct {
        const char *name;
        ShotKind    kind;
        V3          cam_pos; float yaw, pitch;
        V3          op_pos;  float op_yaw;
    } POSES[] = {
        /* 3/4 portrait matching the DeBrief banner: harbor & viaduct left, operator right, tower behind */
        { "operator_hero",    SHOT_HERO,
          {  1.52f, 1.38f, 11.55f },  0.22f,  0.02f,
          {  2.15f, 0.00f, 12.52f },  3.32f },
        /* 3/4 rear gear silhouette: hydration pack + ribbed radio + twin unequal whips + holster */
        { "operator_rear",    SHOT_REAR,
          { -0.45f, 1.38f,  8.05f }, -0.36f, -0.04f,
          { -1.05f, 0.00f,  9.55f }, -0.32f },
        /* Over-the-shoulder gameplay down the dockyard avenue with grapple + Mecha + amber HUD */
        { "tps_action",       SHOT_TPS_ACTION,
          {  0.00f, 0.00f,  0.00f },  0.22f, -0.04f,
          {  1.25f, 0.00f, 10.80f },  0.22f },
        /* West harbor canal planar reflections, viaduct bridge, crane & glowing windows */
        { "canal_reflection", SHOT_CANAL,
          { -5.00f, 2.05f,  6.40f }, -0.26f, -0.11f,
          { -3.80f, 0.00f, 11.40f }, -0.45f },
        /* Close-up of Type-1 Patrol Mecha + Regime Trooper beneath 3-bars banners */
        { "mecha_trooper",    SHOT_MECHA,
          {  5.55f, 1.68f, 18.35f },  0.22f,  0.05f,
          {  1.20f, 0.00f, 11.00f },  0.30f },
        /* Crimson SIGINT night-optic visor + Trinitron scanlines */
        { "visor_sigint",     SHOT_VISOR,
          {  0.00f, 0.00f,  0.00f },  0.18f, -0.05f,
          {  1.40f, 0.00f, 12.20f },  0.18f },
        /* Classic wide shot down the sodium-lit Greyport canyon */
        { "street_hero",      SHOT_STREET,
          {  0.00f, 2.60f,  7.00f },  0.15f, -0.12f,
          {  1.20f, 0.00f, 14.80f },  0.25f },
        /* Near corner brick bevels & mortar alignment */
        { "wall_close",       SHOT_WALL_CLOSE,
          { -1.50f, 1.70f, 17.20f }, -0.85f,  0.02f,
          {  1.20f, 0.00f, 19.50f },  2.80f },
        /* Exact concept art replica model sheet turntable poses (pure white studio backdrop) */
        { "operator_model_sheet",     SHOT_STUDIO_OP_FRONT,
          {  0.00f, 1.02f,  2.18f }, PS15_PI, -0.01f,
          {  0.00f, 0.00f,  0.00f }, -0.15f },
        { "operator_rear_sheet",      SHOT_STUDIO_OP_REAR,
          {  0.00f, 1.02f,  2.18f }, PS15_PI, -0.01f,
          {  0.00f, 0.00f,  0.00f },  2.98f },
        { "enemy_soldier_sheet",      SHOT_STUDIO_SOLDIER_FRONT,
          {  0.00f, 0.95f,  2.08f }, PS15_PI, -0.01f,
          {  0.00f, 0.00f,  0.00f }, -0.36f },
        { "enemy_soldier_rear_sheet", SHOT_STUDIO_SOLDIER_REAR,
          {  0.00f, 0.95f,  2.08f }, PS15_PI, -0.01f,
          {  0.00f, 0.00f,  0.00f },  2.78f },
        { "enemy_mecha_sheet",        SHOT_STUDIO_MECHA_FRONT,
          {  0.00f, 1.72f,  3.85f }, PS15_PI, -0.02f,
          {  0.00f, 0.00f,  0.00f }, -0.36f },
        { "enemy_mecha_rear_sheet",   SHOT_STUDIO_MECHA_REAR,
          {  0.00f, 1.72f,  3.85f }, PS15_PI, -0.02f,
          {  0.00f, 0.00f,  0.00f },  2.78f },
    };

    for (int i = 0; i < (int)(sizeof POSES / sizeof POSES[0]); i++) {
        ShotKind kind = POSES[i].kind;
        float t = 1.25f;

        Guard guards[2] = {
            { v3( 5.4f, 0.0f, 19.4f), -2.55f, 0.28f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f },
            { v3(-2.0f, 0.0f, 28.0f),  1.80f, 0.10f, 0, 0.0f, 0.0f, v3(0,0,0), 0.0f }
        };
        Mecha walker;
        mecha_init(&walker, v3(7.5f, 0.0f, 20.6f), -2.72f);
        walker.anim_t = 0.85f;
        walker.alert  = (kind == SHOT_MECHA || kind == SHOT_VISOR) ? 0.65f : 0.25f;

        if (kind == SHOT_HERO) {
            walker.pos = v3(6.8f, 0.0f, 28.5f);
        } else if (kind == SHOT_MECHA) {
            guards[0].pos = v3(5.65f, 0.0f, 19.8f);
            guards[0].yaw = -2.65f;
            walker.pos    = v3(7.45f, 0.0f, 20.5f);
            walker.yaw    = -2.72f;
        }

        Body op_body = { POSES[i].op_pos, v3(0,0,0), 0.35f, 1.8f, true, false, v3(0,0,0), 0 };
        Camera cam = { POSES[i].cam_pos, POSES[i].yaw, POSES[i].pitch };
        if (kind == SHOT_HERO) {
            fb->settings.fov_scale = 1.28f;
        } else if (kind == SHOT_TPS_ACTION) {
            fb->settings.fov_scale = 1.18f;
        } else {
            fb->settings.fov_scale = 1.0f;
        }

        if (kind == SHOT_TPS_ACTION || kind == SHOT_VISOR) {
            bool ads = (kind == SHOT_TPS_ACTION);
            cam = tps_camera_step((Camera){ op_body.pos, POSES[i].yaw, POSES[i].pitch },
                                  &op_body, POSES[i].yaw, POSES[i].pitch,
                                  false, ads, sc.world, sc.nworld, 1.0f);
        }

        if (kind >= SHOT_STUDIO_OP_FRONT) {
            for (int p = 0; p < fb->w * fb->h; p++) fb->px[p] = 0x00FFFFFF;
            fb_clear_depth(fb);

            PointLight pl[3] = {
                { v3(-1.8f, 2.9f, 2.4f), 9.0f, 1.35f, 1.32f, 1.28f }, /* Key light (front-left) */
                { v3( 2.2f, 1.6f, 1.8f), 8.0f, 0.55f, 0.56f, 0.60f }, /* Fill light (front-right) */
                { v3( 0.0f, 2.8f,-2.4f), 7.5f, 0.75f, 0.78f, 0.82f }  /* Rim / kicker light (rear-top) */
            };
            LightRig srig = {
                .lights = pl,
                .nlights = 3,
                .amb_r = 0.34f, .amb_g = 0.34f, .amb_b = 0.36f,
                .spots = NULL,
                .nspots = 0,
                .fog_r = 1.0f, .fog_g = 1.0f, .fog_b = 1.0f, .fog_dist = 1000.0f
            };
            fb->settings.fov_scale = 1.0f;
            if (kind >= SHOT_STUDIO_MECHA_FRONT) {
                /* Mecha wide mechanical stance contact shadows */
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3(-0.46f, 0, 0), 0.52f);
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3( 0.46f, 0, 0), 0.52f);
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3( 0.08f, 0, -0.06f), 1.35f);
            } else {
                /* Humanoid left/right boot contact shadows + directional cast shadow */
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3(-0.10f, 0, 0), 0.24f);
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3( 0.10f, 0, 0), 0.24f);
                render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                                   v3( 0.06f, 0, -0.05f), 0.58f);
            }

            if (kind == SHOT_STUDIO_OP_FRONT || kind == SHOT_STUDIO_OP_REAR) {
                Pose op = anim_sample(&CLIP_AIM, 0.0f);
                if (kind == SHOT_STUDIO_OP_FRONT) {
                    op.head_yaw = 0.12f;
                    op.head_pitch = 0.02f;
                }
                render_humanoid(fb, &cam, &oper, POSES[i].op_pos, POSES[i].op_yaw, &op,
                                sc.tex, 1.0f, 1.0f, 1.0f, &srig);
            } else if (kind == SHOT_STUDIO_SOLDIER_FRONT || kind == SHOT_STUDIO_SOLDIER_REAR) {
                Pose sp = anim_sample(&CLIP_IDLE, 0.0f);
                sp.arm_r = -0.55f; sp.elbow_r = 0.85f;
                sp.arm_l = -0.38f; sp.elbow_l = 0.95f;
                if (kind == SHOT_STUDIO_SOLDIER_FRONT) {
                    sp.head_yaw = 0.18f;
                    sp.head_pitch = 0.02f;
                }
                render_humanoid(fb, &cam, &soldier, POSES[i].op_pos, POSES[i].op_yaw, &sp,
                                sc.tex, 1.0f, 1.0f, 1.0f, &srig);
            } else if (kind == SHOT_STUDIO_MECHA_FRONT || kind == SHOT_STUDIO_MECHA_REAR) {
                Mecha mw;
                mecha_init(&mw, POSES[i].op_pos, POSES[i].op_yaw);
                mw.anim_t = 0.0f; mw.alert = 0.40f;
                render_mecha(fb, &cam, &mmesh, &mw, sc.tex, &srig);
            }

            char path[512];
            snprintf(path, sizeof path, "%s/%s.ppm", outdir, POSES[i].name);
            if (write_ppm(path, fb, false) != 0) {
                fprintf(stderr, "write %s failed\n", path);
                return 1;
            }
            printf("wrote %s (%d tris drawn)\n", path, fb->stats.tris_drawn);
            continue;
        }

        greyport_update_lights(&sc, guards, 2, t);
        if (kind == SHOT_HERO && sc.rig.nlights < GREYPORT_MAX_LIGHTS) {
            int li = sc.rig.nlights++;
            sc.lights[li] = (PointLight){
                v3(1.10f, 2.20f, 11.80f),
                5.8f, 1.35f, 1.05f, 0.70f
            };
        } else if (kind == SHOT_REAR && sc.rig.nlights < GREYPORT_MAX_LIGHTS) {
            int li = sc.rig.nlights++;
            sc.lights[li] = (PointLight){
                v3(POSES[i].op_pos.x - 1.30f, 2.05f, POSES[i].op_pos.z - 1.05f),
                5.8f, 0.92f, 0.72f, 0.48f
            };
        }
        if (kind == SHOT_MECHA && sc.rig.nlights < GREYPORT_MAX_LIGHTS) {
            int li = sc.rig.nlights++;
            sc.lights[li] = (PointLight){
                v3(4.8f, 3.2f, 16.8f), 8.5f, 0.82f, 0.68f, 0.52f
            };
        }

        greyport_render_opaque(fb, &sc, &cam, t);

        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                           POSES[i].op_pos, 0.58f);
        for (int g = 0; g < 2; g++)
            render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                               guards[g].pos, 0.58f);
        render_blob_shadow(fb, &cam, &sc.shadow_quad, &sc.tex[TEX_SHADOW],
                           walker.pos, 1.25f);

        Pose op = (kind == SHOT_HERO || kind == SHOT_TPS_ACTION)
                  ? anim_sample(&CLIP_AIM, 0.2f)
                  : anim_sample(&CLIP_IDLE, 1.3f);
        if (kind == SHOT_HERO) {
            op.head_yaw   = (POSES[i].yaw + PS15_PI) - POSES[i].op_yaw;
            op.head_pitch =  0.04f;
        } else if (kind == SHOT_TPS_ACTION) {
            op.arm_l = -2.10f; op.elbow_l = 0.30f;
        }
        render_humanoid(fb, &cam, &oper, POSES[i].op_pos, POSES[i].op_yaw, &op,
                        sc.tex, 1.0f, 1.0f, 1.02f, &sc.rig);

        if (kind == SHOT_TPS_ACTION) {
            V3 hand_w = v3(POSES[i].op_pos.x - 0.22f, 1.52f, POSES[i].op_pos.z + 0.18f);
            greyport_render_grapple(fb, &sc, &cam, hand_w, sc.anchors[5]);
        }

        for (int g = 0; g < 2; g++) {
            Pose gp = anim_sample(&CLIP_WALK, 0.6f + g * 0.4f);
            gp.arm_r = -0.72f; gp.elbow_r = 0.65f;
            render_humanoid(fb, &cam, &soldier, guards[g].pos, guards[g].yaw, &gp,
                            sc.tex, 1.0f, 1.0f, 1.0f, &sc.rig);
        }
        render_mecha(fb, &cam, &mmesh, &walker, sc.tex, &sc.rig);

        greyport_render_fx(fb, &sc, &cam, guards, 2, t);

        if (kind == SHOT_VISOR) {
            fb_apply_visor(fb, t);
        } else {
            fb_bloom(fb, 0.70f, 0.32f);
            if (kind == SHOT_HERO) {
                fb_dof(fb, 2.1f, 0.75f, 6.0f);
            }
        }
        if (kind == SHOT_TPS_ACTION || kind == SHOT_VISOR) {
            float vis = light_exposure_at(&sc.rig, v3(POSES[i].op_pos.x, 1.0f, POSES[i].op_pos.z));
            hud_draw_visor(fb, cam.yaw, 42.6f, 12, vis, walker.alert, 0.34f,
                           kind == SHOT_TPS_ACTION);
        }

        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", outdir, POSES[i].name);
        if (write_ppm(path, fb, kind == SHOT_VISOR) != 0) {
            fprintf(stderr, "write %s failed\n", path);
            return 1;
        }
        printf("wrote %s (%d tris drawn)\n", path, fb->stats.tris_drawn);
    }

    fb_free(fb);
    mecha_mesh_free(&mmesh);
    humanoid_free(&oper);
    humanoid_free(&soldier);
    greyport_free(&sc);
    return 0;
}
