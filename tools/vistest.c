/* Visual test harness: renders the Greyport street from fixed camera
 * poses chosen to expose texture-mapping artifacts — oblique wall,
 * grazing ground plane, street view, hero angle. Writes PPM stills.
 * Run from the repo root: ./vistest <outdir> */
#include "engine.h"
#include <sys/stat.h>

static const PointLight LAMPS[] = {
    { { -2, 11.4f, 34 }, 16, 1.0f, 0.58f, 0.16f },
    { { 6, 4.4f, 18 },   13, 1.0f, 0.58f, 0.16f },
    { { -8, 4.4f, 28 },  13, 1.0f, 0.55f, 0.15f },
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

static void render_scene(FB *fb, const Tex *tex, const Camera *cam,
                         const Mesh *ground, const Mesh *ware1,
                         const Mesh *ware2, const Mesh *tower,
                         const Mesh *platform, const Mesh *pole,
                         const Mesh *lamp_head, const Mesh *crate,
                         const Tex *brick, const Humanoid *oper) {
    LightRig rig = { LAMPS, 3, 0.16f, 0.19f, 0.28f };
    fb_sky(fb);
    fb_clear_depth(fb);
    render_mesh(fb, cam, ground, v3(0, 0, 35), 0, &tex[TEX_CONCRETE], 1, 1, 1, &rig);
    render_mesh(fb, cam, ware1, v3(-4, 0, 22), 0, brick, 1, 1, 1, &rig);
    render_mesh(fb, cam, ware2, v3(10, 0, 27.5f), 0, &tex[TEX_METAL], 0.9f, 0.92f, 1, &rig);
    render_mesh(fb, cam, tower, v3(-2, 0, 33.4f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
    render_mesh(fb, cam, platform, v3(-2, 11, 32.5f), 0, &tex[TEX_METAL], 0.8f, 0.8f, 0.85f, &rig);
    render_mesh(fb, cam, pole, v3(6, 0, 18), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
    render_mesh(fb, cam, pole, v3(-8, 0, 28), 0, &tex[TEX_METAL], 0.7f, 0.7f, 0.75f, &rig);
    render_mesh(fb, cam, lamp_head, v3(6, 4.4f, 18), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
    render_mesh(fb, cam, lamp_head, v3(-8, 4.4f, 28), 0, NULL, 1.4f, 0.85f, 0.25f, &rig);
    render_mesh(fb, cam, crate, v3(2, 0, 17.4f), 0.4f, &tex[TEX_METAL], 0.85f, 0.85f, 0.9f, &rig);
    Pose idle = anim_sample(&CLIP_IDLE, 1.3f);
    render_humanoid(fb, cam, oper, v3(1, 0, 20), 2.8f, &idle, tex, 0.85f, 0.9f, 0.85f, &rig);
    for (int i = 0; i < 3; i++) {
        V3 d = v3_sub(LAMPS[i].pos, cam->pos);
        float vx = cosf(cam->yaw) * d.x - sinf(cam->yaw) * d.z;
        float vz0 = sinf(cam->yaw) * d.x + cosf(cam->yaw) * d.z;
        float py = cosf(cam->pitch) * d.y - sinf(cam->pitch) * vz0;
        float vz = sinf(cam->pitch) * d.y + cosf(cam->pitch) * vz0;
        if (vz < 1) continue;
        int gx = (int)(FB_W * 0.5f + vx * 215.0f / vz);
        int gy = (int)(FB_H * 0.5f - py * 215.0f / vz);
        if (gx < 0 || gx >= FB_W || gy < 0 || gy >= FB_H) continue;
        if (fb->zb[gy * FB_W + gx] < vz - 1.5f) continue;
        raster_glow(fb, gx, gy, (int)(260.0f / vz), 120, 66, 10);
    }
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "vistest-out";
    mkdir(outdir, 0755);
    Tex tex[TEX_COUNT];
    texgen_all(tex);
    Tex brick_file;
    const Tex *brick = (tex_load_ppm("assets/brick.ppm", &brick_file) == 0)
                       ? &brick_file : &tex[TEX_BRICK];
    Mesh ground = mesh_plane(90, 90, 14);
    Mesh ware1 = mesh_box(10, 5, 8);
    Mesh ware2 = mesh_box(12, 7, 9);
    Mesh tower = mesh_box(1.2f, 11, 1.2f);
    Mesh platform = mesh_box(3, 0.5f, 3);
    Mesh lamp_head = mesh_box(0.5f, 0.3f, 0.5f);
    Mesh pole = mesh_box(0.18f, 4.4f, 0.18f);
    Mesh crate = mesh_load_obj("assets/crate.obj");
    if (crate.nv <= 0) crate = mesh_box(1.2f, 1.2f, 1.2f);
    Humanoid oper = humanoid_new();
    FB *fb = fb_new(FB_W, FB_H);

    /* poses: pos, yaw, pitch — oblique & grazing angles stress UV mapping */
    static const struct { const char *name; V3 pos; float yaw, pitch; } POSES[] = {
        { "wall_oblique", { -7.5f, 1.6f, 12.0f },  0.55f, -0.05f }, /* brick wall at a slant */
        { "ground_graze", { 2.0f, 1.1f, 8.0f },    0.10f, -0.28f }, /* concrete running away */
        { "street_hero",  { 0.0f, 2.6f, 7.0f },    0.15f, -0.12f }, /* down the street */
        { "wall_close",   { -1.5f, 1.7f, 17.2f }, -0.85f,  0.02f }, /* near corner, hard angle */
    };
    for (int i = 0; i < 4; i++) {
        Camera cam = { POSES[i].pos, POSES[i].yaw, POSES[i].pitch };
        render_scene(fb, tex, &cam, &ground, &ware1, &ware2, &tower,
                     &platform, &pole, &lamp_head, &crate, brick, &oper);
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", outdir, POSES[i].name);
        if (write_ppm(path, fb) != 0) { fprintf(stderr, "write %s failed\n", path); return 1; }
        printf("wrote %s\n", path);
    }
    return 0;
}
