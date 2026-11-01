/* Perception & 3D raycasts: patrol guards with light-modulated 3D vision cones,
 * slab line-of-sight occlusion, acoustic bullet-ping distraction lures, stun
 * incapacitation, and hitscan AABB/sphere ray tests. */
#include "engine.h"

static bool slab_axis(float p0, float d, float mn, float mx,
                      float *tmin, float *tmax) {
    if (fabsf(d) < 1e-6f) return (p0 >= mn && p0 <= mx);
    float inv = 1.0f / d;
    float t1 = (mn - p0) * inv;
    float t2 = (mx - p0) * inv;
    if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
    if (t1 > *tmin) *tmin = t1;
    if (t2 < *tmax) *tmax = t2;
    return *tmin <= *tmax;
}

float ray_hit_aabb(V3 orig, V3 dir, float max_d, const AABB *box) {
    float tmin = 0.001f, tmax = max_d;
    if (!slab_axis(orig.x, dir.x, box->min.x, box->max.x, &tmin, &tmax)) return -1.0f;
    if (!slab_axis(orig.y, dir.y, box->min.y, box->max.y, &tmin, &tmax)) return -1.0f;
    if (!slab_axis(orig.z, dir.z, box->min.z, box->max.z, &tmin, &tmax)) return -1.0f;
    return tmin;
}

float ray_hit_sphere(V3 orig, V3 dir, float max_d, V3 center, float radius) {
    V3 oc = v3_sub(orig, center);
    float b = v3_dot(oc, dir);
    float c = v3_dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    float t = -b - sqrtf(disc);
    if (t < 0.001f) t = -b + sqrtf(disc);
    return (t >= 0.001f && t <= max_d) ? t : -1.0f;
}

static bool seg_hits_aabb_3d(V3 a, V3 b, const AABB *box) {
    float tmin = 0.001f, tmax = 0.999f;
    V3 d = v3_sub(b, a);
    if (!slab_axis(a.x, d.x, box->min.x, box->max.x, &tmin, &tmax)) return false;
    if (!slab_axis(a.y, d.y, box->min.y, box->max.y, &tmin, &tmax)) return false;
    if (!slab_axis(a.z, d.z, box->min.z, box->max.z, &tmin, &tmax)) return false;
    return true;
}

bool los_clear(V3 a, V3 b, const AABB *world, int n) {
    if (fabsf(a.y) < 1e-4f && fabsf(b.y) < 1e-4f) {
        a.y = 1.55f;
        b.y = 1.45f;
    }
    for (int i = 0; i < n; i++) {
        if (seg_hits_aabb_3d(a, b, &world[i])) return false;
    }
    return true;
}

static float wrap_pi(float a) {
    while (a >  PS15_PI) a -= 2.0f * PS15_PI;
    while (a < -PS15_PI) a += 2.0f * PS15_PI;
    return a;
}

void guard_hear_ping(Guard *g, V3 ping_pos, float max_range) {
    if (!g || g->stunned > 0.0f) return;
    float d = v3_len(v3_sub(ping_pos, g->pos));
    if (d <= max_range) {
        g->investigate = ping_pos;
        g->inv_timer = 4.5f;
        g->wait = 0.0f;
        if (g->alert < 0.38f) g->alert = 0.38f;
    }
}

void guard_update_ex(Guard *g, const V3 *waypoints, int nw, V3 player,
                     float player_noise, float player_vis,
                     const AABB *world, int n, float dt) {
    if (g->stunned > 0.0f) {
        g->stunned = fmaxf(0.0f, g->stunned - dt);
        g->alert = clampf(g->alert - dt * 0.25f, 0.0f, 1.0f);
        return;
    }
    if (g->inv_timer > 0.0f) g->inv_timer = fmaxf(0.0f, g->inv_timer - dt);

    V3 eye_g = v3(g->pos.x, g->pos.y + 1.60f, g->pos.z);
    V3 tgt_p = v3(player.x, player.y + 1.25f, player.z);
    V3 to_p = v3_sub(tgt_p, eye_g);
    float dist = v3_len(to_p);

    float vis = clampf(player_vis, 0.12f, 1.25f);
    float max_sight = 7.5f + 12.5f * vis;
    float seen = 0.0f, heard = 0.0f;
    if (dist < max_sight && dist > 1e-3f) {
        V3 facing = v3(sinf(g->yaw), 0.0f, cosf(g->yaw));
        V3 horiz = v3_norm(v3(to_p.x, 0.0f, to_p.z));
        float cosang = v3_dot(facing, horiz);
        float elev = fabsf(to_p.y) / dist;
        if (cosang > 0.35f && elev < 0.78f && los_clear(eye_g, tgt_p, world, n)) {
            seen = (1.0f - dist / max_sight) * (0.65f + 0.85f * vis);
        }
    }
    if (player_noise > 0.0f && dist < 4.5f + player_noise * 10.5f) {
        heard = player_noise * fmaxf(0.0f, 1.0f - dist / 16.5f);
    }

    float evidence = fmaxf(seen, heard);
    if (evidence > 0.02f)
        g->alert = clampf(g->alert + evidence * dt * 1.65f, 0.0f, 1.0f);
    else
        g->alert = clampf(g->alert - dt * 0.055f, 0.0f, 1.0f);

    if (evidence > 0.25f && g->alert > 0.30f && dist > 0.3f) {
        float want = atan2f(to_p.x, to_p.z);
        float diff = wrap_pi(want - g->yaw);
        g->yaw = wrap_pi(g->yaw + clampf(diff, -3.4f * dt, 3.4f * dt));
    }

    if (g->wait > 0.0f && g->inv_timer <= 0.0f) {
        g->wait -= dt * (g->alert > 0.4f ? 2.0f : 1.0f);
        if (evidence <= 0.05f) g->yaw = wrap_pi(g->yaw + sinf(g->wait * 3.2f) * 0.45f * dt);
    } else if (nw > 0 || g->inv_timer > 0.0f) {
        V3 target = (g->inv_timer > 0.0f) ? g->investigate : waypoints[g->waypoint % nw];
        V3 to = v3_sub(target, g->pos);
        to.y = 0.0f;
        float d = v3_len(to);
        if (d < 0.45f) {
            if (g->inv_timer > 0.0f) {
                g->inv_timer = 0.0f;
                g->wait = 1.6f;
            } else {
                g->waypoint = (g->waypoint + 1) % nw;
                g->wait = 1.2f;
            }
        } else {
            V3 dir = v3_norm(to);
            if (evidence <= 0.25f) {
                float want = atan2f(dir.x, dir.z);
                float diff = wrap_pi(want - g->yaw);
                g->yaw = wrap_pi(g->yaw + clampf(diff, -2.6f * dt, 2.6f * dt));
            }
            float spd = (g->alert > 0.6f || g->inv_timer > 0.0f) ? 2.15f : 1.6f;
            V3 next = v3_add(g->pos, v3_scale(dir, spd * dt));
            Body gb = { next, v3(0, 0, 0), 0.35f, 1.8f, true, false, v3(0, 0, 0), 0 };
            bool blocked = false;
            for (int i = 0; i < n; i++) {
                if (gb.pos.x + gb.radius > world[i].min.x &&
                    gb.pos.x - gb.radius < world[i].max.x &&
                    gb.pos.z + gb.radius > world[i].min.z &&
                    gb.pos.z - gb.radius < world[i].max.z &&
                    world[i].max.y > 0.5f) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) g->pos = next;
        }
    }
}

void guard_update(Guard *g, const V3 *waypoints, int nw, V3 player,
                  float player_noise, const AABB *world, int n, float dt) {
    guard_update_ex(g, waypoints, nw, player, player_noise, 1.0f, world, n, dt);
}

/* ---------- Autonomous Regime Patrol Walker (Mecha Type-1) ------------- */
void mecha_init(Mecha *m, V3 pos, float yaw) {
    memset(m, 0, sizeof *m);
    m->pos = pos;
    m->yaw = yaw;
    m->sensor_hp   = 1.0f;
    m->actuator_hp = 1.0f;
    m->coolant_hp  = 1.0f;
}

void mecha_update(Mecha *m, const V3 *waypoints, int nw, V3 player,
                  float player_noise, float player_vis,
                  const AABB *world, int n, float dt) {
    m->step_event = false;
    if (m->coolant_hp <= 0.01f) {
        /* Coolant/power spine ruptured: walker shuts down in place */
        m->alert = fmaxf(0.0f, m->alert - dt * 0.4f);
        return;
    }

    V3 eye = v3(m->pos.x, m->pos.y + 3.05f, m->pos.z);
    V3 tgt = v3(player.x, player.y + 1.20f, player.z);
    V3 to_p = v3_sub(tgt, eye);
    float dist = v3_len(to_p);

    float sens = clampf(m->sensor_hp, 0.0f, 1.0f);
    float vis = clampf(player_vis, 0.10f, 1.3f);
    float max_sight = (6.0f + 18.0f * sens) * (0.55f + 0.55f * vis);
    float seen = 0.0f, heard = 0.0f;

    if (sens > 0.1f && dist < max_sight && dist > 1e-3f) {
        V3 facing = v3(sinf(m->yaw), 0.0f, cosf(m->yaw));
        V3 horiz = v3_norm(v3(to_p.x, 0.0f, to_p.z));
        if (v3_dot(facing, horiz) > 0.42f && los_clear(eye, tgt, world, n)) {
            seen = (1.0f - dist / max_sight) * vis * sens;
        }
    }
    /* Blinded walker compensates by leaning harder on acoustic governor */
    float audio_gain = (sens < 0.4f) ? 1.45f : 1.0f;
    if (player_noise > 0.0f && dist < 22.0f * audio_gain) {
        heard = player_noise * audio_gain * fmaxf(0.0f, 1.0f - dist / 22.0f);
    }

    float ev = fmaxf(seen, heard);
    if (ev > 0.03f)
        m->alert = clampf(m->alert + ev * dt * 1.35f, 0.0f, 1.0f);
    else
        m->alert = clampf(m->alert - dt * 0.045f, 0.0f, 1.0f);

    float mob = 0.35f + 0.65f * clampf(m->actuator_hp, 0.0f, 1.0f);
    if (m->wait > 0.0f && m->alert < 0.5f) {
        m->wait -= dt;
        return;
    }

    V3 goal = (m->alert > 0.65f) ? player
            : (nw > 0 ? waypoints[m->waypoint % nw] : m->pos);
    V3 to = v3(goal.x - m->pos.x, 0.0f, goal.z - m->pos.z);
    float d = v3_len(to);
    if (d < 0.85f) {
        if (nw > 0 && m->alert <= 0.65f) {
            m->waypoint = (m->waypoint + 1) % nw;
            m->wait = 1.8f;
        }
        return;
    }

    V3 dir = v3_scale(to, 1.0f / d);
    float want = atan2f(dir.x, dir.z);
    float diff = wrap_pi(want - m->yaw);
    float turn_rate = 1.35f * mob;
    m->yaw = wrap_pi(m->yaw + clampf(diff, -turn_rate * dt, turn_rate * dt));

    float spd = ((m->alert > 0.65f) ? 2.05f : 1.42f) * mob;
    V3 next = v3_add(m->pos, v3_scale(v3(sinf(m->yaw), 0.0f, cosf(m->yaw)), spd * dt));
    bool blocked = false;
    for (int i = 0; i < n; i++) {
        if (next.x + 0.85f > world[i].min.x && next.x - 0.85f < world[i].max.x &&
            next.z + 0.85f > world[i].min.z && next.z - 0.85f < world[i].max.z &&
            world[i].max.y > 0.6f) {
            blocked = true;
            break;
        }
    }
    if (!blocked) m->pos = next;

    float prev_phase = fmodf(m->anim_t, 0.90f);
    m->anim_t += dt * (0.75f + 0.35f * mob);
    float cur_phase = fmodf(m->anim_t, 0.90f);
    if (cur_phase < prev_phase) m->step_event = true;
}
