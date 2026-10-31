/* Animation: keyframed pose clips with sampling and blending.
 * Walk/run are gait clips — legs and arms in counter-swing. */
#include "engine.h"

static Pose P(float swing, float al, float ar, float ll, float lr,
              float kl, float kr, float lean, float hy) {
    Pose p = { swing, al, ar, ll, lr, kl, kr, lean, hy };
    return p;
}
static const Keyframe IDLE_KEYS[] = {
    { 0.0f, { 0, 0.03f, -0.03f, 0.02f, -0.02f, 0, 0, 0.02f, 0 } },
    { 1.0f, { 0, 0.05f, -0.05f, 0.02f, -0.02f, 0, 0, 0.03f, 0 } },
    { 2.0f, { 0, 0.03f, -0.03f, 0.02f, -0.02f, 0, 0, 0.02f, 0 } },
};
static const Keyframe WALK_KEYS[] = {
    { 0.00f, { 1, -0.45f, 0.45f, 0.55f, -0.55f, 0.15f, 0.45f, 0.08f, 0 } },
    { 0.25f, { 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.08f, 0 } },
    { 0.50f, { 1, 0.45f, -0.45f, -0.55f, 0.55f, 0.45f, 0.15f, 0.08f, 0 } },
    { 0.75f, { 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.35f, 0.08f, 0 } },
    { 1.00f, { 1, -0.45f, 0.45f, 0.55f, -0.55f, 0.15f, 0.45f, 0.08f, 0 } },
};
static const Keyframe RUN_KEYS[] = {
    { 0.00f, { 1, -0.85f, 0.85f, 0.95f, -0.95f, 0.3f, 0.8f, 0.22f, 0 } },
    { 0.25f, { 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.7f, 0.22f, 0 } },
    { 0.50f, { 1, 0.85f, -0.85f, -0.95f, 0.95f, 0.8f, 0.3f, 0.22f, 0 } },
    { 0.75f, { 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.7f, 0.22f, 0 } },
    { 1.00f, { 1, -0.85f, 0.85f, 0.95f, -0.95f, 0.3f, 0.8f, 0.22f, 0 } },
};
const Clip CLIP_IDLE = { IDLE_KEYS, 3, 2.0f };
const Clip CLIP_WALK = { WALK_KEYS, 5, 1.0f };
const Clip CLIP_RUN  = { RUN_KEYS, 5, 1.0f };
/* silence the unused-builder warning path in strict builds */
static Pose (*const _p_ref)(float, float, float, float, float, float, float, float, float) = P;

Pose anim_blend(Pose a, Pose b, float w) {
    Pose o;
    o.swing = lerpf(a.swing, b.swing, w);
    o.arm_l = lerpf(a.arm_l, b.arm_l, w);
    o.arm_r = lerpf(a.arm_r, b.arm_r, w);
    o.leg_l = lerpf(a.leg_l, b.leg_l, w);
    o.leg_r = lerpf(a.leg_r, b.leg_r, w);
    o.knee_l = lerpf(a.knee_l, b.knee_l, w);
    o.knee_r = lerpf(a.knee_r, b.knee_r, w);
    o.lean = lerpf(a.lean, b.lean, w);
    o.head_yaw = lerpf(a.head_yaw, b.head_yaw, w);
    return o;
}

Pose anim_sample(const Clip *clip, float t) {
    (void)_p_ref;
    float lt = fmodf(t, clip->dur);
    if (lt < 0) lt += clip->dur;
    for (int i = 0; i + 1 < clip->nkeys; i++) {
        float t0 = clip->keys[i].t * clip->dur;
        float t1 = clip->keys[i + 1].t * clip->dur;
        if (lt >= t0 && lt <= t1) {
            float w = (t1 > t0) ? (lt - t0) / (t1 - t0) : 0;
            return anim_blend(clip->keys[i].pose, clip->keys[i + 1].pose, w);
        }
    }
    return clip->keys[clip->nkeys - 1].pose;
}
