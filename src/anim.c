/* Animation: keyframed pose clips with sampling and blending.
 * Includes idle, tactical patrol walk, sprint run, stealth crouch, and the
 * two-handed Center-Axis-Relock sidearm ready stance (CLIP_AIM). */
#include "engine.h"

/* Pose field order:
 * swing, arm_l, arm_r, leg_l, leg_r, knee_l, knee_r, lean, head_yaw,
 * elbow_l, elbow_r, crouch, aim, head_pitch, torso_yaw */

static const Keyframe IDLE_KEYS[] = {
    { 0.0f, { 0, -0.10f, -0.22f,  0.03f, -0.03f, 0.05f, 0.05f, 0.03f,  0.00f, 0.28f, 0.52f, 0.00f, 0.12f, 0.04f,  0.00f } },
    { 0.5f, { 0, -0.13f, -0.25f,  0.03f, -0.03f, 0.07f, 0.07f, 0.05f,  0.03f, 0.32f, 0.55f, 0.01f, 0.12f, 0.05f, -0.02f } },
    { 1.0f, { 0, -0.10f, -0.22f,  0.03f, -0.03f, 0.05f, 0.05f, 0.03f,  0.00f, 0.28f, 0.52f, 0.00f, 0.12f, 0.04f,  0.00f } },
};

static const Keyframe WALK_KEYS[] = {
    { 0.00f, { 1, -0.48f,  0.34f,  0.50f, -0.50f, 0.12f, 0.52f, 0.09f,  0.02f, 0.42f, 0.58f, 0.02f, 0.08f, 0.04f, -0.05f } },
    { 0.25f, { 1, -0.06f, -0.10f, -0.04f,  0.04f, 0.34f, 0.22f, 0.07f,  0.00f, 0.38f, 0.52f, 0.04f, 0.08f, 0.03f,  0.00f } },
    { 0.50f, { 1,  0.36f, -0.50f, -0.50f,  0.50f, 0.52f, 0.12f, 0.09f, -0.02f, 0.42f, 0.62f, 0.02f, 0.08f, 0.04f,  0.05f } },
    { 0.75f, { 1, -0.06f, -0.10f,  0.04f, -0.04f, 0.22f, 0.34f, 0.07f,  0.00f, 0.38f, 0.52f, 0.04f, 0.08f, 0.03f,  0.00f } },
    { 1.00f, { 1, -0.48f,  0.34f,  0.50f, -0.50f, 0.12f, 0.52f, 0.09f,  0.02f, 0.42f, 0.58f, 0.02f, 0.08f, 0.04f, -0.05f } },
};

static const Keyframe RUN_KEYS[] = {
    { 0.00f, { 1, -0.88f,  0.72f,  0.88f, -0.88f, 0.22f, 0.92f, 0.24f,  0.04f, 0.85f, 0.95f, 0.06f, 0.04f, -0.04f, -0.10f } },
    { 0.25f, { 1, -0.08f, -0.12f,  0.00f,  0.00f, 0.62f, 0.45f, 0.20f,  0.00f, 0.78f, 0.88f, 0.10f, 0.04f, -0.02f,  0.00f } },
    { 0.50f, { 1,  0.72f, -0.88f, -0.88f,  0.88f, 0.92f, 0.22f, 0.24f, -0.04f, 0.95f, 0.85f, 0.06f, 0.04f, -0.04f,  0.10f } },
    { 0.75f, { 1, -0.08f, -0.12f,  0.00f,  0.00f, 0.45f, 0.62f, 0.20f,  0.00f, 0.78f, 0.88f, 0.10f, 0.04f, -0.02f,  0.00f } },
    { 1.00f, { 1, -0.88f,  0.72f,  0.88f, -0.88f, 0.22f, 0.92f, 0.24f,  0.04f, 0.85f, 0.95f, 0.06f, 0.04f, -0.04f, -0.10f } },
};

static const Keyframe CROUCH_KEYS[] = {
    { 0.00f, { 0.5f, -0.62f, -0.58f, -0.68f, -0.52f, 1.18f, 1.05f, 0.26f, 0.0f, 0.58f, 0.82f, 0.85f, 0.65f, 0.06f, -0.06f } },
    { 0.50f, { 0.5f, -0.66f, -0.62f, -0.52f, -0.68f, 1.05f, 1.18f, 0.28f, 0.0f, 0.60f, 0.85f, 0.88f, 0.65f, 0.08f, -0.06f } },
    { 1.00f, { 0.5f, -0.62f, -0.58f, -0.68f, -0.52f, 1.18f, 1.05f, 0.26f, 0.0f, 0.58f, 0.82f, 0.85f, 0.65f, 0.06f, -0.06f } },
};

/* Two-handed Center-Axis-Relock pistol ready stance (matches DeBrief banner!) */
static const Keyframe AIM_KEYS[] = {
    { 0.00f, { 0, -1.02f, -0.88f, -0.08f,  0.10f, 0.16f, 0.12f, 0.10f,  0.22f, 0.65f, 0.98f, 0.05f, 1.0f, 0.16f, -0.12f } },
    { 0.50f, { 0, -1.04f, -0.90f, -0.08f,  0.10f, 0.18f, 0.14f, 0.11f,  0.20f, 0.66f, 0.99f, 0.06f, 1.0f, 0.17f, -0.13f } },
    { 1.00f, { 0, -1.02f, -0.88f, -0.08f,  0.10f, 0.16f, 0.12f, 0.10f,  0.22f, 0.65f, 0.98f, 0.05f, 1.0f, 0.16f, -0.12f } },
};

const Clip CLIP_IDLE   = { IDLE_KEYS,   3, 2.4f };
const Clip CLIP_WALK   = { WALK_KEYS,   5, 1.0f };
const Clip CLIP_RUN    = { RUN_KEYS,    5, 0.72f };
const Clip CLIP_CROUCH = { CROUCH_KEYS, 3, 1.4f };
const Clip CLIP_AIM    = { AIM_KEYS,    3, 2.8f };

Pose anim_blend(Pose a, Pose b, float w) {
    Pose o;
    o.swing      = lerpf(a.swing,      b.swing,      w);
    o.arm_l      = lerpf(a.arm_l,      b.arm_l,      w);
    o.arm_r      = lerpf(a.arm_r,      b.arm_r,      w);
    o.leg_l      = lerpf(a.leg_l,      b.leg_l,      w);
    o.leg_r      = lerpf(a.leg_r,      b.leg_r,      w);
    o.knee_l     = lerpf(a.knee_l,     b.knee_l,     w);
    o.knee_r     = lerpf(a.knee_r,     b.knee_r,     w);
    o.lean       = lerpf(a.lean,       b.lean,       w);
    o.head_yaw   = lerpf(a.head_yaw,   b.head_yaw,   w);
    o.elbow_l    = lerpf(a.elbow_l,    b.elbow_l,    w);
    o.elbow_r    = lerpf(a.elbow_r,    b.elbow_r,    w);
    o.crouch     = lerpf(a.crouch,     b.crouch,     w);
    o.aim        = lerpf(a.aim,        b.aim,        w);
    o.head_pitch = lerpf(a.head_pitch, b.head_pitch, w);
    o.torso_yaw  = lerpf(a.torso_yaw,  b.torso_yaw,  w);
    return o;
}

Pose anim_sample(const Clip *clip, float t) {
    float lt = fmodf(t, clip->dur);
    if (lt < 0.0f) lt += clip->dur;
    for (int i = 0; i + 1 < clip->nkeys; i++) {
        float t0 = clip->keys[i].t * clip->dur;
        float t1 = clip->keys[i + 1].t * clip->dur;
        if (lt >= t0 && lt <= t1) {
            float w = (t1 > t0) ? (lt - t0) / (t1 - t0) : 0.0f;
            return anim_blend(clip->keys[i].pose, clip->keys[i + 1].pose, w);
        }
    }
    return clip->keys[clip->nkeys - 1].pose;
}
