/* Physics: gravity, symmetric AABB world collision (X/Z walls, ground landing,
 * overhead ceiling bonk, crouch headroom check), camera sphere push-out, and
 * reel-in pendulum grapple mechanics. */
#include "engine.h"

float world_ground_y(const AABB *world, int n, V3 p) {
    float best = 0.0f;
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (p.x >= a->min.x && p.x <= a->max.x &&
            p.z >= a->min.z && p.z <= a->max.z && a->max.y <= p.y + 0.05f) {
            if (a->max.y > best) best = a->max.y;
        }
    }
    return best;
}

static bool overlaps_h(V3 p, float radius, float height, const AABB *a) {
    return p.x + radius > a->min.x && p.x - radius < a->max.x &&
           p.z + radius > a->min.z && p.z - radius < a->max.z &&
           p.y < a->max.y && p.y + height > a->min.y;
}

bool physics_can_stand(const Body *b, const AABB *world, int n, float stand_h) {
    for (int i = 0; i < n; i++) {
        if (overlaps_h(b->pos, b->radius * 0.95f, stand_h, &world[i]))
            return false;
    }
    return true;
}

V3 camera_resolve(V3 cam, const AABB *world, int n, float radius) {
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        float nx = clampf(cam.x, a->min.x, a->max.x);
        float ny = clampf(cam.y, a->min.y, a->max.y);
        float nz = clampf(cam.z, a->min.z, a->max.z);
        V3 d = v3(cam.x - nx, cam.y - ny, cam.z - nz);
        float dist = v3_len(d);
        if (dist >= radius) continue;
        if (dist > 1e-5f) {
            cam = v3_add(v3(nx, ny, nz), v3_scale(v3_norm(d), radius));
        } else {
            float exits[6] = { cam.x - a->min.x, a->max.x - cam.x,
                               cam.y - a->min.y, a->max.y - cam.y,
                               cam.z - a->min.z, a->max.z - cam.z };
            int best = 0;
            for (int k = 1; k < 6; k++) if (exits[k] < exits[best]) best = k;
            if (best == 0) cam.x = a->min.x - radius;
            if (best == 1) cam.x = a->max.x + radius;
            if (best == 2) cam.y = a->min.y - radius;
            if (best == 3) cam.y = a->max.y + radius;
            if (best == 4) cam.z = a->min.z - radius;
            if (best == 5) cam.z = a->max.z + radius;
        }
    }
    if (cam.y < 0.35f) cam.y = 0.35f;
    return cam;
}

void grapple_fire(Body *b, V3 anchor) {
    b->grappling = true;
    b->grapple_anchor = anchor;
    V3 hand = v3(b->pos.x, b->pos.y + 1.4f, b->pos.z);
    b->grapple_len = v3_len(v3_sub(anchor, hand));
}

void grapple_release(Body *b) {
    b->grappling = false;
}

/* Physical raycast-anything grapple (DeBrief law: EVERY solid surface grapples) */
bool grapple_fire_ray(Body *b, V3 eye, V3 aim_dir,
                      const AABB *world, int nworld,
                      const V3 *anchors, int nanchors, float max_dist) {
    V3 dir = v3_norm(aim_dir);
    float best_t = max_dist;
    V3 best_pt = v3(0, 0, 0);
    bool hit = false;

    for (int i = 0; i < nworld; i++) {
        float t = ray_hit_aabb(eye, dir, best_t, &world[i]);
        if (t > 0.35f && t < best_t) {
            best_t = t;
            best_pt = v3_add(eye, v3_scale(dir, t));
            hit = true;
        }
    }
    /* High-precision snap assist for thin lamp arms & tower rails in narrow cone */
    float best_score = 0.90f;
    for (int i = 0; i < nanchors; i++) {
        V3 to = v3_sub(anchors[i], eye);
        float d = v3_len(to);
        if (d < 1.2f || d > max_dist) continue;
        float c = v3_dot(v3_scale(to, 1.0f / d), dir);
        if (c > best_score && (!hit || d <= best_t + 2.5f) &&
            los_clear(eye, anchors[i], world, nworld)) {
            best_score = c;
            best_pt = anchors[i];
            hit = true;
        }
    }
    if (hit) grapple_fire(b, best_pt);
    return hit;
}

void physics_step(Body *b, const AABB *world, int n, float dt) {
    b->vel.y -= 22.0f * dt;
    if (b->vel.y < -30.0f) b->vel.y = -30.0f;

    /* Grapple: Nexuiz Tarzan reel-in + rubber-band pendulum swing.
     * Holding crouch (height < 1.3m) pauses reel-in while keeping pure swing! */
    if (b->grappling) {
        bool hold_len = (b->height < 1.30f);
        if (!hold_len && b->grapple_len > 2.2f) {
            b->grapple_len = fmaxf(2.2f, b->grapple_len - 4.8f * dt);
        }
        V3 hand = v3(b->pos.x, b->pos.y + 1.4f, b->pos.z);
        V3 to_hand = v3_sub(hand, b->grapple_anchor);
        float d = v3_len(to_hand);
        if (d > 1e-4f) {
            V3 out_dir = v3_scale(to_hand, 1.0f / d);
            if (d > b->grapple_len) {
                float vr = v3_dot(b->vel, out_dir);
                if (vr > 0.0f) b->vel = v3_sub(b->vel, v3_scale(out_dir, vr));
                float stretch = d - b->grapple_len;
                b->vel = v3_sub(b->vel, v3_scale(out_dir, stretch * 9.0f));
            }
            if (!hold_len) {
                /* Motorized winch pull toward anchor */
                b->vel = v3_sub(b->vel, v3_scale(out_dir, 9.5f * dt));
            }
        }
    }

    /* X-axis integrate + minimum-penetration wall resolve */
    b->pos.x += b->vel.x * dt;
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (!overlaps_h(b->pos, b->radius, b->height, a)) continue;
        float pen_min = (b->pos.x + b->radius) - a->min.x;
        float pen_max = a->max.x - (b->pos.x - b->radius);
        b->pos.x = (pen_min < pen_max) ? (a->min.x - b->radius)
                                       : (a->max.x + b->radius);
        b->vel.x = 0.0f;
    }

    /* Z-axis integrate + minimum-penetration wall resolve */
    b->pos.z += b->vel.z * dt;
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (!overlaps_h(b->pos, b->radius, b->height, a)) continue;
        float pen_min = (b->pos.z + b->radius) - a->min.z;
        float pen_max = a->max.z - (b->pos.z - b->radius);
        b->pos.z = (pen_min < pen_max) ? (a->min.z - b->radius)
                                       : (a->max.z + b->radius);
        b->vel.z = 0.0f;
    }

    /* Y-axis integrate + ceiling bonk + rooftop/ground landing */
    float prev_y = b->pos.y;
    b->pos.y += b->vel.y * dt;
    b->grounded = false;

    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (!overlaps_h(b->pos, b->radius * 0.95f, b->height, a)) continue;
        if (b->vel.y > 0.0f && prev_y + b->height <= a->min.y + 0.15f) {
            /* Overhead ceiling hit */
            b->pos.y = a->min.y - b->height;
            b->vel.y = 0.0f;
        } else if (b->vel.y <= 0.0f && prev_y >= a->max.y - 0.25f) {
            /* Landed on rooftop / crate / platform */
            b->pos.y = a->max.y;
            b->vel.y = 0.0f;
            b->grounded = true;
        }
    }

    float gy = world_ground_y(world, n, v3(b->pos.x, prev_y + 0.35f, b->pos.z));
    if (b->pos.y <= gy) {
        b->pos.y = gy;
        if (b->vel.y < 0.0f) b->vel.y = 0.0f;
        b->grounded = true;
    }
    if (b->pos.y <= 0.0f) {
        b->pos.y = 0.0f;
        if (b->vel.y < 0.0f) b->vel.y = 0.0f;
        b->grounded = true;
    }
}

/* Kinetic wall-running along vertical dockyard facades when airborne at speed */
bool physics_wallrun_step(Body *b, V3 *out_normal, float *wall_timer,
                          const AABB *world, int n, float dt) {
    if (b->grounded) {
        *wall_timer = 1.35f;
        return false;
    }
    if (b->grappling || *wall_timer <= 0.0f) return false;

    float hspd = hypotf(b->vel.x, b->vel.z);
    if (hspd < 4.1f) return false;

    float reach = b->radius + 0.24f;
    V3 best_n = v3(0, 0, 0);
    float best_d = reach;
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (b->pos.y + 0.4f >= a->max.y || b->pos.y + 1.4f <= a->min.y) continue;
        if (b->pos.z + b->radius > a->min.z && b->pos.z - b->radius < a->max.z) {
            float d1 = fabsf(b->pos.x - a->max.x);
            float d2 = fabsf(b->pos.x - a->min.x);
            if (d1 < best_d) { best_d = d1; best_n = v3( 1, 0, 0); }
            if (d2 < best_d) { best_d = d2; best_n = v3(-1, 0, 0); }
        }
        if (b->pos.x + b->radius > a->min.x && b->pos.x - b->radius < a->max.x) {
            float d1 = fabsf(b->pos.z - a->max.z);
            float d2 = fabsf(b->pos.z - a->min.z);
            if (d1 < best_d) { best_d = d1; best_n = v3(0, 0,  1); }
            if (d2 < best_d) { best_d = d2; best_n = v3(0, 0, -1); }
        }
    }
    if (best_d >= reach) return false;

    /* Project horizontal velocity parallel to wall & counteract most of gravity */
    float vn = b->vel.x * best_n.x + b->vel.z * best_n.z;
    if (vn < 0.0f) {
        b->vel.x -= best_n.x * vn;
        b->vel.z -= best_n.z * vn;
    }
    b->vel.y += 17.5f * dt; /* leaves gentle downward arc as wall-run budget decays */
    if (b->vel.y < -1.8f) b->vel.y = -1.8f;
    *wall_timer = fmaxf(0.0f, *wall_timer - dt);
    if (out_normal) *out_normal = best_n;
    return true;
}

/* Modern ledge auto-mantle: smoothly vaults onto crates, containers & rooftops */
bool physics_mantle_step(Body *b, V3 move_dir, const AABB *world, int n, float dt) {
    if (b->grounded || b->grappling) return false;
    float md_len = hypotf(move_dir.x, move_dir.z);
    if (md_len < 0.15f) return false;
    V3 dir = v3(move_dir.x / md_len, 0.0f, move_dir.z / md_len);
    V3 probe = v3(b->pos.x + dir.x * (b->radius + 0.36f),
                  b->pos.y,
                  b->pos.z + dir.z * (b->radius + 0.36f));

    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (probe.x < a->min.x || probe.x > a->max.x ||
            probe.z < a->min.z || probe.z > a->max.z)
            continue;
        float ledge_h = a->max.y - b->pos.y;
        if (ledge_h < 0.30f || ledge_h > 2.15f) continue;

        /* Verify clear standing headroom on top of the ledge */
        Body top_test = *b;
        top_test.pos = v3(probe.x, a->max.y + 0.03f, probe.z);
        if (!physics_can_stand(&top_test, world, n, 1.75f)) continue;

        /* Smoothly haul upward & nudge onto ledge crest */
        b->vel.y = fmaxf(b->vel.y, 6.4f);
        if (b->pos.y + 0.45f >= a->max.y) {
            b->pos.x += dir.x * 3.8f * dt;
            b->pos.z += dir.z * 3.8f * dt;
        }
        return true;
    }
    return false;
}

/* Dedicated Third-Person Over-the-Shoulder Camera Rig (DeBrief standard):
 *   - Low tactical over-right-shoulder follow in exploration/stealth/grapple
 *   - Tight right-shoulder zoom on aim (RMB) for precision targeting
 *   - Raycast + sphere-push occlusion against all world geometry */
Camera tps_camera_step(Camera prev, const Body *b, float yaw, float pitch,
                       bool crouching, bool aiming,
                       const AABB *world, int nworld, float dt) {
    float cp = cosf(pitch), sp = sinf(pitch);
    float sy = sinf(yaw),   cy = cosf(yaw);
    V3 fwd   = v3(sy * cp, sp, cy * cp);
    V3 right = v3(cy, 0.0f, -sy);

    float pivot_h = crouching ? 1.12f : 1.50f;
    float sh_off  = aiming ? 0.50f : 0.44f;
    float boom    = aiming ? 1.32f : 2.95f;

    V3 spine = v3(b->pos.x, b->pos.y + pivot_h, b->pos.z);
    V3 shoulder = v3_add(spine, v3_scale(right, sh_off));
    V3 ideal = v3_sub(shoulder, v3_scale(fwd, boom));
    ideal.y += aiming ? 0.04f : 0.14f;

    /* Raycast from spine to ideal camera so walls never occlude the operator */
    V3 ray = v3_sub(ideal, spine);
    float ray_len = v3_len(ray);
    if (ray_len > 1e-4f) {
        V3 rdir = v3_scale(ray, 1.0f / ray_len);
        float hit_t = ray_len;
        for (int i = 0; i < nworld; i++) {
            float t = ray_hit_aabb(spine, rdir, hit_t, &world[i]);
            if (t > 0.05f && t < hit_t) hit_t = t;
        }
        if (hit_t < ray_len) {
            ideal = v3_add(spine, v3_scale(rdir, fmaxf(0.42f, hit_t - 0.26f)));
        }
    }
    ideal = camera_resolve(ideal, world, nworld, 0.28f);

    float alpha = (dt <= 0.0f || dt > 0.25f) ? 1.0f : (1.0f - expf(-16.0f * dt));
    Camera out;
    out.pos = v3(lerpf(prev.pos.x, ideal.x, alpha),
                 lerpf(prev.pos.y, ideal.y, alpha),
                 lerpf(prev.pos.z, ideal.z, alpha));
    out.pos = camera_resolve(out.pos, world, nworld, 0.26f);
    out.yaw   = yaw;
    out.pitch = pitch;
    return out;
}
