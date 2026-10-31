/* Perception: patrol guards with vision cones, line-of-sight occlusion
 * by world AABBs, hearing from player noise, and an alert level that
 * rises on evidence and decays in quiet. Stealth math, no rendering. */
#include "engine.h"

static bool seg_hits_aabb_xz(V3 a, V3 b, const AABB *box) {
    float dx = b.x - a.x, dz = b.z - a.z;
    float tmin = 0, tmax = 1;
    if (fabsf(dx) < 1e-6f) {
        if (a.x < box->min.x || a.x > box->max.x) return false;
    } else {
        float t1 = (box->min.x - a.x) / dx, t2 = (box->max.x - a.x) / dx;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (fabsf(dz) < 1e-6f) {
        if (a.z < box->min.z || a.z > box->max.z) return false;
    } else {
        float t1 = (box->min.z - a.z) / dz, t2 = (box->max.z - a.z) / dz;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    return true;
}

bool los_clear(V3 a, V3 b, const AABB *world, int n) {
    for (int i = 0; i < n; i++) {
        if (world[i].max.y < 1.0f) continue;    /* low slabs don't block sight */
        if (seg_hits_aabb_xz(a, b, &world[i])) return false;
    }
    return true;
}

void guard_update(Guard *g, const V3 *waypoints, int nw, V3 player,
                  float player_noise, const AABB *world, int n, float dt) {
    /* patrol movement */
    if (g->wait > 0) {
        g->wait -= dt;
    } else {
        V3 target = waypoints[g->waypoint];
        V3 to = v3_sub(target, g->pos);
        to.y = 0;
        float d = v3_len(to);
        if (d < 0.4f) {
            g->waypoint = (g->waypoint + 1) % nw;
            g->wait = 1.2f;
        } else {
            V3 dir = v3_norm(to);
            float want = atan2f(dir.x, dir.z);
            float diff = want - g->yaw;
            while (diff > 3.14159265f) diff -= 2 * 3.14159265f;
            while (diff < -3.14159265f) diff += 2 * 3.14159265f;
            g->yaw += clampf(diff, -2.2f * dt, 2.2f * dt);
            g->pos = v3_add(g->pos, v3_scale(dir, 1.6f * dt));
        }
    }
    /* senses */
    V3 to_p = v3_sub(player, g->pos);
    float dist = v3_len(to_p);
    float seen = 0, heard = 0;
    if (dist < 18.0f) {
        V3 facing = v3(sinf(g->yaw), 0, cosf(g->yaw));
        float cosang = v3_dot(facing, v3_norm(v3(to_p.x, 0, to_p.z)));
        if (cosang > 0.35f && los_clear(g->pos, player, world, n)) {
            seen = (1.0f - dist / 18.0f) * 1.4f;
        }
    }
    if (player_noise > 0 && dist < 4.0f + player_noise * 10.0f)
        heard = player_noise * (1.0f - dist / 16.0f);
    float evidence = fmaxf(seen, heard);
    if (evidence > 0.02f)
        g->alert = clampf(g->alert + evidence * dt * 1.6f, 0, 1);
    else
        g->alert = clampf(g->alert - dt * 0.05f, 0, 1);
}
