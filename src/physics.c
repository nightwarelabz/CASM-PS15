/* Physics: gravity, AABB world collision (axis-separated), ground
 * query, and the grapple — fire, reel constraint, release with momentum. */
#include "engine.h"

float world_ground_y(const AABB *world, int n, V3 p) {
    float best = 0.0f;
    for (int i = 0; i < n; i++) {
        const AABB *a = &world[i];
        if (p.x >= a->min.x && p.x <= a->max.x &&
            p.z >= a->min.z && p.z <= a->max.z && a->max.y <= p.y + 0.01f) {
            if (a->max.y > best) best = a->max.y;
        }
    }
    return best;
}

static bool overlaps(const Body *b, const AABB *a, V3 p) {
    return p.x + b->radius > a->min.x && p.x - b->radius < a->max.x &&
           p.z + b->radius > a->min.z && p.z - b->radius < a->max.z &&
           p.y < a->max.y && p.y + b->height > a->min.y;
}

void grapple_fire(Body *b, V3 anchor) {
    b->grappling = true;
    b->grapple_anchor = anchor;
    b->grapple_len = v3_len(v3_sub(anchor, v3(b->pos.x, b->pos.y + 1.4f, b->pos.z)));
}
void grapple_release(Body *b) { b->grappling = false; }

void physics_step(Body *b, const AABB *world, int n, float dt) {
    b->vel.y -= 22.0f * dt;                    /* gravity, game-weighty */
    if (b->vel.y < -30.0f) b->vel.y = -30.0f;

    /* grapple: rope constraint around the anchor + pendulum pull */
    if (b->grappling) {
        V3 hand = v3(b->pos.x, b->pos.y + 1.4f, b->pos.z);
        V3 to = v3_sub(hand, b->grapple_anchor);
        float d = v3_len(to);
        if (d > b->grapple_len) {
            V3 dir = v3_norm(to);
            float vr = v3_dot(b->vel, dir);
            if (vr > 0) b->vel = v3_sub(b->vel, v3_scale(dir, vr));
            b->vel.y += 6.0f * dt;             /* reel lift */
        }
    }

    /* axis-separated integrate + resolve */
    b->pos.x += b->vel.x * dt;
    for (int i = 0; i < n; i++)
        if (overlaps(b, &world[i], b->pos)) {
            b->pos.x = (b->vel.x > 0) ? world[i].min.x - b->radius
                                      : world[i].max.x + b->radius;
            b->vel.x = 0;
        }
    b->pos.z += b->vel.z * dt;
    for (int i = 0; i < n; i++)
        if (overlaps(b, &world[i], b->pos)) {
            b->pos.z = (b->vel.z > 0) ? world[i].min.z - b->radius
                                      : world[i].max.z + b->radius;
            b->vel.z = 0;
        }
    b->pos.y += b->vel.y * dt;
    b->grounded = false;
    float gy = world_ground_y(world, n, v3(b->pos.x, b->pos.y + 0.5f, b->pos.z));
    if (b->pos.y <= gy) {
        b->pos.y = gy;
        if (b->vel.y < 0) b->vel.y = 0;
        b->grounded = true;
    }
    if (b->pos.y < 0) { b->pos.y = 0; b->vel.y = 0; b->grounded = true; }
}
